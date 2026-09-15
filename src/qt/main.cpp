#include "Dashboard.h"

#include <QApplication>
#include <QCloseEvent>
#include <QCoreApplication>
#include <QFile>
#include <QFrame>
#include <QIcon>
#include <QMenu>
#include <QScrollArea>
#include <QStyle>
#include <QSystemTrayIcon>

static QIcon loadAppIcon()
{
    const char *candidates[] = {
        "/usr/share/icons/hicolor/128x128/apps/audiohawk.png",
        "/usr/local/share/icons/hicolor/128x128/apps/audiohawk.png",
        "data/icons/hicolor/128x128/apps/audiohawk.png",
        "../data/icons/hicolor/128x128/apps/audiohawk.png",
        "data/appicon/tray/audiohawk-48.png",
        "../data/appicon/tray/audiohawk-48.png",
        nullptr
    };
    for (int i = 0; candidates[i]; ++i) {
        if (QFile::exists(QString::fromUtf8(candidates[i])))
            return QIcon(QString::fromUtf8(candidates[i]));
    }
    return QIcon::fromTheme(QStringLiteral("audiohawk"),
                            qApp->style()->standardIcon(QStyle::SP_MediaVolume));
}

class MainWindow : public QScrollArea {
public:
    explicit MainWindow(AhCore *core, QSystemTrayIcon *tray, QWidget *parent = nullptr)
        : QScrollArea(parent)
        , m_core(core)
        , m_tray(tray)
    {
        setWidgetResizable(true);
        setFrameShape(QFrame::NoFrame);
        setWidget(new Dashboard(core));
        setWindowTitle(QStringLiteral("AudioHawk"));
        resize(720, 780);
        setStyleSheet(QStringLiteral("QScrollArea{background:#12151A; border:none;}"));
    }

    void setReallyQuit(bool v) { m_reallyQuit = v; }

protected:
    void closeEvent(QCloseEvent *event) override
    {
        if (m_reallyQuit) {
            if (m_core)
                ah_core_persist(m_core);
            event->accept();
            return;
        }

        /* Keep PipeWire EQ alive — only hide the UI. */
        hide();
        if (m_tray && m_tray->isVisible() && !m_toldBackground) {
            m_toldBackground = true;
            m_tray->showMessage(
                QStringLiteral("AudioHawk"),
                QStringLiteral("Running in the background — EQ stays active."),
                QSystemTrayIcon::Information,
                3500);
        }
        event->ignore();
    }

private:
    AhCore *m_core = nullptr;
    QSystemTrayIcon *m_tray = nullptr;
    bool m_reallyQuit = false;
    bool m_toldBackground = false;
};

static void loadStyle()
{
    const char *candidates[] = {
        "data/qss/audiohawk.qss",
        "../data/qss/audiohawk.qss",
        "/usr/share/audiohawk/audiohawk.qss",
        nullptr
    };

    for (int i = 0; candidates[i]; ++i) {
        QFile f(QString::fromUtf8(candidates[i]));
        if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
            continue;
        qApp->setStyleSheet(QString::fromUtf8(f.readAll()));
        return;
    }
}

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("AudioHawk"));
    QApplication::setOrganizationName(QStringLiteral("AudioHawk"));
    QApplication::setQuitOnLastWindowClosed(false);

    bool startBackground = false;
    const QStringList args = QCoreApplication::arguments();
    for (const QString &a : args) {
        if (a == QLatin1String("--background") ||
            a == QLatin1String("--minimized") ||
            a == QLatin1String("-b")) {
            startBackground = true;
            break;
        }
    }

    AhCore core{};
    if (ah_core_init(&core) != 0) {
        return 1;
    }

    loadStyle();

    QIcon appIcon = loadAppIcon();
    app.setWindowIcon(appIcon);

    QSystemTrayIcon tray;
    MainWindow window(&core, &tray);
    window.setWindowIcon(appIcon);

    auto *menu = new QMenu;
    QAction *showAct = menu->addAction(QStringLiteral("Show AudioHawk"));
    QAction *quitAct = menu->addAction(QStringLiteral("Quit AudioHawk"));
    QObject::connect(showAct, &QAction::triggered, &window, &QWidget::showNormal);
    QObject::connect(showAct, &QAction::triggered, &window, &QWidget::raise);
    QObject::connect(showAct, &QAction::triggered, &window, &QWidget::activateWindow);
    QObject::connect(quitAct, &QAction::triggered, [&]() {
        window.setReallyQuit(true);
        ah_core_persist(&core);
        app.quit();
    });

    if (QSystemTrayIcon::isSystemTrayAvailable()) {
        tray.setIcon(appIcon);
        tray.setToolTip(QStringLiteral("AudioHawk — A simple audio enhancer."));
        tray.setContextMenu(menu);
        QObject::connect(&tray, &QSystemTrayIcon::activated,
                         [&](QSystemTrayIcon::ActivationReason reason) {
                             if (reason == QSystemTrayIcon::Trigger ||
                                 reason == QSystemTrayIcon::DoubleClick) {
                                 window.showNormal();
                                 window.raise();
                                 window.activateWindow();
                             }
                         });
        tray.show();
    }

    if (!startBackground)
        window.show();

    const int rc = app.exec();
    ah_core_shutdown(&core);
    return rc;
}
