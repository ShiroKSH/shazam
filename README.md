# Shazam (Qt + Python runner)

Небольшое Qt-приложение для распознавания треков. Записывает 3–4 сек системного аудио (loopback) и дергает `shazamio`.


## Зависимости

### Runtime
- **Qt 6** (Widgets, Concurrent) + MinGW toolchain.
- **Python 3.9+** (в PATH или укажи переменную `SHAZAM_PYTHON`).
- Питон-пакеты:
  ```bash
  pip install -r requirements.txt
