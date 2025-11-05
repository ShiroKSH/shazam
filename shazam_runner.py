import argparse, asyncio, json, traceback, numpy as np
import soundcard as sc
try:
    import sounddevice as sd
    _HAS_SD = True
except Exception:
    _HAS_SD = False
from scipy.io.wavfile import write
from shazamio import Shazam

SAMPLE_RATE = 44100
RMS_SILENCE = 8e-4

def list_devices():
    outs_sc = []
    outs_sd = []
    try:
        outs_sc = [s.name for s in sc.all_speakers()]
    except Exception:
        pass
    if _HAS_SD:
        try:
            for d in sd.query_devices():
                if (d.get("max_output_channels", 0) or 0) > 0 and d.get("name"):
                    outs_sd.append(d["name"])
        except Exception:
            pass
    print(json.dumps({"soundcard_mics": outs_sc, "sd_devices": outs_sd}, ensure_ascii=False))

def _sd_output_index_by_name(name):
    if not (_HAS_SD and name):
        return None
    for i, d in enumerate(sd.query_devices()):
        if (d.get("max_output_channels", 0) or 0) > 0 and (d.get("name") or "").strip() == name.strip():
            return i
    return None

def record_soundcard(seconds=3, rate=SAMPLE_RATE, device=None):
    try:
        if device:
            mic = sc.get_microphone(id=device, include_loopback=True)
        else:
            spk = sc.default_speaker()
            mic = sc.get_microphone(id=str(spk.name), include_loopback=True)
        if mic is None:
            return None, "sc_no_mic"
        with mic.recorder(samplerate=rate) as rec:
            data = rec.record(numframes=rate*seconds)
        return data.astype(np.float32), None
    except Exception as e:
        return None, str(e) or "sc_error"

def record_sounddevice(seconds=3, rate=SAMPLE_RATE, device=None):
    if not _HAS_SD:
        return None, "sd_missing"
    try:
        dev_idx = _sd_output_index_by_name(device) if device else None
        data = sd.rec(
            int(seconds*rate),
            samplerate=rate,
            channels=2,
            dtype="float32",
            device=dev_idx,
            blocking=True,
            latency="low",
            extra_settings=sd.WasapiSettings(loopback=True)
        )
        return data.astype(np.float32), None
    except Exception as e:
        return None, str(e) or "sd_error"

def rms_of(data: np.ndarray) -> float:
    mono = data.mean(axis=1).astype(np.float32)
    return float(np.sqrt(np.mean(mono**2)) + 1e-12)

def write_pcm16(path: str, data: np.ndarray, rate=SAMPLE_RATE):
    mono = data.mean(axis=1)
    mono = np.clip(mono, -1.0, 1.0)
    pcm16 = (mono * 32767.0).astype(np.int16)
    write(path, rate, pcm16)
    return path

def record_system(seconds=3, rate=SAMPLE_RATE, out="snippet.wav", device=None):
    data, err = record_soundcard(seconds, rate, device)
    backend = "soundcard"
    if data is None:
        data, err = record_sounddevice(seconds, rate, device)
        backend = "sounddevice"
        if data is None:
            raise RuntimeError(err or "no_audio_backend")
    r = rms_of(data)
    write_pcm16(out, data, rate)
    return out, backend, r

async def _shazam_recognize(path):
    s = Shazam()
    try:
        if hasattr(s, "recognize"):
            return await s.recognize(path)
        else:
            return await s.recognize_song(path)
    except Exception as e:
        return {"error": str(e)}

async def main():
    ap = argparse.ArgumentParser(add_help=False)
    ap.add_argument("--seconds", type=int, default=3)
    ap.add_argument("--rate", type=int, default=SAMPLE_RATE)
    ap.add_argument("--outfile", type=str, default="snippet.wav")
    ap.add_argument("--device", type=str, default=None)
    ap.add_argument("--list-devices", action="store_true")
    args = ap.parse_args()

    if args.list_devices:
        list_devices()
        return

    try:
        wav, backend, rms = record_system(args.seconds, args.rate, args.outfile, args.device)

        if rms < RMS_SILENCE:
            print(json.dumps({
                "match": False,
                "low_rms": True,
                "rms": rms,
                "backend": backend,
                "wav": wav
            }, ensure_ascii=False))
            return

        res = await _shazam_recognize(wav)

        out = {"match": False, "rms": rms, "backend": backend, "wav": wav}
        if isinstance(res, dict) and res.get("track"):
            t = res["track"]
            out.update({
                "match": True,
                "title": t.get("title"),
                "artist": t.get("subtitle"),
                "url": t.get("url"),
                "share": (t.get("share") or {}).get("subject"),
            })
        elif isinstance(res, dict) and res.get("matches"):
            m = res.get("matches")[0] if res.get("matches") else {}
            out.update({
                "match": bool(m),
                "title": (m.get("track") or {}).get("title"),
                "artist": (m.get("track") or {}).get("subtitle"),
                "url": (m.get("track") or {}).get("url"),
            })
        print(json.dumps(out, ensure_ascii=False))

    except Exception:
        t = traceback.format_exc()
        print(json.dumps({"match": False, "error": t}, ensure_ascii=False))

if __name__ == "__main__":
    asyncio.run(main())
