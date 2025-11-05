#include <QApplication>
#include <QWidget>
#include <QLabel>
#include <QPushButton>
#include <QComboBox>
#include <QTextEdit>
#include <QClipboard>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QTextStream>
#include <QFileInfo>
#include <QFile>
#include <QDir>
#include <QProcess>
#include <QProcessEnvironment>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QFutureWatcher>
#include <QtConcurrent>
#include <optional>

static inline QString _(const char* u8) { return QString::fromUtf8(u8); }

static QString appDir() { return QCoreApplication::applicationDirPath(); }
static QString shazamRunnerPath() { return QDir(appDir()).filePath("shazam_runner.py"); }
static QString audioDeviceFilePath() { return QDir(appDir()).filePath("audio_device.txt"); }

static QString findPython() {
    const QString fromEnv = qEnvironmentVariable("SHAZAM_PYTHON");
    if (!fromEnv.trimmed().isEmpty() && QFileInfo::exists(fromEnv)) return fromEnv;
#ifdef Q_OS_WIN
    {
        QProcess p; p.setProgram("python"); p.setArguments({"--version"});
        p.start(); if (p.waitForStarted(2000) && p.waitForFinished(2000) && p.exitCode()==0) return "python";
    }
    {
        QProcess p; p.setProgram("py"); p.setArguments({"-3", "--version"});
        p.start(); if (p.waitForStarted(2000) && p.waitForFinished(2000) && p.exitCode()==0) return "py";
    }
    return "python";
#else
    return "python3";
#endif
}

static const QString APP_TITLE = _("Shazam");

struct Hit { QString title; QString artist; QString url; };
struct RecognizeResult { std::optional<Hit> hit; QString error; };

static void cleanSnippets() {
    const QStringList roots{ QDir::tempPath(), QCoreApplication::applicationDirPath(), QFileInfo(shazamRunnerPath()).absolutePath() };
    for (const QString& r : roots) {
        QDir d(r);
        for (const auto& f : d.entryList(QStringList() << "snippet.wav" << "snippet*.wav", QDir::Files))
            QFile::remove(d.filePath(f));
    }
}

static QString runProcess(const QString& program, const QStringList& args, int timeoutMs, QString* errOut = nullptr) {
    QProcess p;
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert("PYTHONIOENCODING", "utf-8");
    p.setProcessEnvironment(env);
    p.setProgram(program);
    p.setArguments(args);
    p.setProcessChannelMode(QProcess::MergedChannels);
    p.setWorkingDirectory(QDir::tempPath());
    p.start();
    if (!p.waitForStarted(timeoutMs)) { if (errOut) *errOut = _("Не удалось запустить процесс"); return {}; }
    if (!p.waitForFinished(timeoutMs)) { p.kill(); if (errOut) *errOut = _("Таймаут выполнения"); cleanSnippets(); return {}; }
    const QString out = QString::fromUtf8(p.readAll());
    if (p.exitStatus() != QProcess::NormalExit || p.exitCode() != 0) { if (errOut) *errOut = out.isEmpty() ? _("Процесс завершился с ошибкой") : out; }
    cleanSnippets();
    return out;
}

static QStringList listDevices() {
    try {
        QString err;
        const QString out = runProcess(findPython(), {shazamRunnerPath(), "--list-devices"}, 10000, &err);
        if (out.isEmpty()) return {};
        const auto doc = QJsonDocument::fromJson(out.trimmed().toUtf8());
        if (!doc.isObject()) return {};
        const auto obj = doc.object();
        QStringList names;
        auto append = [&](const char* k){
            const auto v = obj.value(QLatin1String(k));
            if (v.isArray()) for (const auto& it : v.toArray()) if (it.isString()) names << it.toString();
        };
        append("soundcard_mics"); append("sd_devices");
        QSet<QString> seen; QStringList uniq; for (const auto& s : names) if (!seen.contains(s)) { uniq << s; seen.insert(s); }
        return uniq;
    } catch (...) { return {}; }
}

static RecognizeResult recognizeTrack(int seconds, const std::optional<QString>& device) {
    RecognizeResult res;
    if (!QFileInfo::exists(shazamRunnerPath())) { res.error = "runner_missing"; return res; }
    QStringList cmd{shazamRunnerPath(), "--seconds", QString::number(seconds)};
    if (device && !device->trimmed().isEmpty()) cmd << "--device" << *device;
    QString err;
    const QString out = runProcess(findPython(), cmd, 40000, &err);
    if (out.isEmpty() && !err.isEmpty()) { res.error = err; return res; }
    const auto doc = QJsonDocument::fromJson(out.trimmed().toUtf8());
    if (!doc.isObject() || !doc.object().value("match").toBool(false)) { res.error = "no_match"; return res; }
    const auto o = doc.object();
    res.hit = Hit{ o.value("title").toString(), o.value("artist").toString(), o.value("url").toString() };
    return res;
}

class ShazamApp : public QWidget {
public:
    ShazamApp() {
        setWindowTitle(APP_TITLE);
        resize(900, 560);
        setStyleSheet(
            "QWidget{background:#0f1115;color:#cbd2e0;}"
            "QLabel{color:#cbd2e0;font-family:'Segoe UI';font-size:11pt;}"
            ".header{color:#8ab4ff;font-weight:700;font-size:14pt;}"
            "QPushButton{background:#24364a;color:#EAF2FF;padding:6px;border-radius:8px;}"
            "QComboBox{background:#131722;color:#cbd2e0;}"
            "QTextEdit{background:#0d1018;color:#92a0b4;border:0px;font-family:Consolas;font-size:10pt;}"
        );
        auto* root = new QVBoxLayout(this); root->setContentsMargins(10,10,10,10);

        auto* top = new QHBoxLayout();
        auto* lblDev = new QLabel(_("Устройство:"));
        device_ = new QComboBox(); device_->setMinimumWidth(360); device_->addItems(listDevices());
        QObject::connect(device_, &QComboBox::currentTextChanged, this, &ShazamApp::saveDevice);
        auto* listen = new QPushButton(_("🎧 Слушать"));
        QObject::connect(listen, &QPushButton::clicked, this, &ShazamApp::listenNow);
        top->addWidget(lblDev); top->addWidget(device_); top->addSpacing(8); top->addWidget(listen); top->addStretch();
        root->addLayout(top);

        auto* info = new QVBoxLayout();
        auto* headNow = new QLabel(_("🎵 Текущий трек")); headNow->setProperty("class","header"); info->addWidget(headNow,0,Qt::AlignLeft);
        title_ = new QLabel(_("—")); QFont f=title_->font(); f.setPointSize(14); f.setBold(true); title_->setFont(f); info->addWidget(title_,0,Qt::AlignLeft);

        auto* copyRow = new QHBoxLayout();
        auto* copyTitle  = new QPushButton(_("📋 Копировать название"));
        auto* copyArtist = new QPushButton(_("👤 Копировать исполнителя"));
        auto* copyBoth   = new QPushButton(_("🧩 Копировать всё"));
        QObject::connect(copyTitle,  &QPushButton::clicked, this, [this]{ copySelection("title"); });
        QObject::connect(copyArtist, &QPushButton::clicked, this, [this]{ copySelection("artist"); });
        QObject::connect(copyBoth,   &QPushButton::clicked, this, [this]{ copySelection("both"); });
        copyRow->addWidget(copyTitle); copyRow->addWidget(copyArtist); copyRow->addWidget(copyBoth); copyRow->addStretch();
        info->addLayout(copyRow);

        auto* headQuick = new QLabel(_("⚡ Быстрые фразы")); headQuick->setProperty("class","header"); info->addWidget(headQuick,0,Qt::AlignLeft);
        auto* quick = new QHBoxLayout();
        addPhrase(quick, _("Откуда OP"));
        addPhrase(quick, _("Откуда ED"));
        addPhrase(quick, _("Откуда OST"));
        addPhrase(quick, _("Дискография"));
        addPhrase(quick, _("Состав группы"));
        addPhrase(quick, _("Год релиза"));
        quick->addStretch(); info->addLayout(quick);

        phrase_ = new QLabel(""); phrase_->setProperty("class","header"); phrase_->setStyleSheet("color:#a0ffa0;"); info->addWidget(phrase_,0,Qt::AlignLeft);
        root->addLayout(info);

        auto* logHead = new QLabel(_("🛠 Логи")); logHead->setProperty("class","header"); root->addWidget(logHead,0,Qt::AlignLeft);
        log_ = new QTextEdit(); log_->setReadOnly(true); log_->setMinimumHeight(160); root->addWidget(log_,1);

        loadSavedDevice();
    }

private:
    QComboBox* device_ = nullptr;
    QLabel* title_ = nullptr;
    QLabel* phrase_ = nullptr;
    QTextEdit* log_ = nullptr;
    std::optional<Hit> lastHit_;
    std::unique_ptr<QFutureWatcher<RecognizeResult>> watcher_;

    void addPhrase(QHBoxLayout* row, const QString& text) {
        auto* b = new QPushButton(text);
        QObject::connect(b, &QPushButton::clicked, this, [this,text]{ handlePhrase(text); });
        row->addWidget(b);
    }

    void log(const QString& s) {
        log_->append(s);
        auto c = log_->textCursor(); c.movePosition(QTextCursor::End); log_->setTextCursor(c);
    }

    void saveLine(const QString& s) {
        const QString path = QCoreApplication::applicationDirPath() + "/phrases.txt";
        QFile f(path);
        if (f.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) { QTextStream ts(&f); ts.setEncoding(QStringConverter::Utf8); ts << s << "\n"; }
    }

    void copyAndStore(const QString& s) {
        if (s.isEmpty()) return;
        QGuiApplication::clipboard()->setText(s);
        log(_("✓ Скопировано: ") + s);
        saveLine(s);
    }

    void saveDevice(const QString& dev) {
        QFile f(audioDeviceFilePath());
        if (f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) { QTextStream ts(&f); ts.setEncoding(QStringConverter::Utf8); ts << dev.trimmed(); }
    }

    void listenNow() {
        log(_("🎧 Слушаю (") + selectedDevice() + _(")..."));
        auto fut = QtConcurrent::run([this]{
            const QString d = device_->currentText().trimmed();
            std::optional<QString> opt; if (!d.isEmpty()) opt = d;
            return recognizeTrack(4, opt);
        });
        watcher_.reset(new QFutureWatcher<RecognizeResult>());
        QObject::connect(watcher_.get(), &QFutureWatcher<RecognizeResult>::finished, this, [this]{
            const auto res = watcher_->future().result();
            if (!res.hit.has_value()) log(_("⚠ Не найдено (") + (res.error.isEmpty()? "unknown" : res.error) + _(")"));
            else { lastHit_ = res.hit; title_->setText(QString("%1 — %2").arg(lastHit_->title, lastHit_->artist)); log(_("🎵 Найдено: ") + QString("%1 — %2").arg(lastHit_->title, lastHit_->artist)); }
        });
        watcher_->setFuture(fut);
    }

    void copySelection(const QString& part) {
        if (!lastHit_.has_value()) { log(_("⚠ Нет данных для копирования")); return; }
        QString s;
        if (part=="title") s = lastHit_->title;
        else if (part=="artist") s = lastHit_->artist;
        else s = QString("%1 — %2").arg(lastHit_->artist, lastHit_->title);
        copyAndStore(s);
    }

    void handlePhrase(const QString& p) {
        if (!lastHit_.has_value()) { log(_("⚠ Сначала распознай трек")); return; }
        const QString s = QString("%1 — %2 %3").arg(lastHit_->title, lastHit_->artist, p);
        phrase_->setText(p);
        log(s);
        copyAndStore(s);
    }

    void loadSavedDevice() {
        QFile f(audioDeviceFilePath());
        if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
            QTextStream ts(&f); ts.setEncoding(QStringConverter::Utf8);
            const QString saved = ts.readAll().trimmed();
            if (!saved.isEmpty()) { const int i = device_->findText(saved); if (i >= 0) device_->setCurrentIndex(i); }
        }
    }

    QString selectedDevice() const {
        const QString d = device_->currentText().trimmed();
        return d.isEmpty() ? _("default") : d;
    }
};

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    ShazamApp w; w.show();
    return app.exec();
}
