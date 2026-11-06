#include "xviewer.h"
#include "xcamera_config.h"
#include <QApplication>
#include <QCommandLineParser>
#include <QTimer>
#include <QSettings>
#include <iostream>

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QCoreApplication::setApplicationName("MyPlayer Monitor");
    QCoreApplication::setApplicationVersion("1.0.0");
    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("基于 play_core 的多路视频监控"));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addOption({"source", "Open a local file or RTSP/HTTP source; may be repeated.", "location"});
    parser.addOption({"layout", "Preview layout: 1, 4, 9 or 16.", "count", "4"});
    parser.addOption({"smoke-test", "Open the application and exit automatically after one second."});
    parser.addOption({"migrate-config", "Load or migrate camera configuration to the installation config directory, then exit."});
    parser.process(app);
    if (parser.isSet("migrate-config")) {
        if (!XCameraConfig::Instance()->LoadDefault()) {
            std::cerr << "Configuration load/migration failed; existing files were preserved.\n";
            return 1;
        }
        std::cout << "Configuration: " << XCameraConfig::DefaultPath().toStdString() << '\n';
        std::cout << "Devices: " << XCameraConfig::Instance()->GetCamCount() << '\n';
        return 0;
    }
    bool valid = false;
    const int savedLayout = QSettings(QStringLiteral("MyPlayer"), QStringLiteral("Monitor")).value(QStringLiteral("live/layout"), 4).toInt();
    int layout = savedLayout == 1 || savedLayout == 4 || savedLayout == 9 || savedLayout == 16 ? savedLayout : 4;
    valid = true;
    if (parser.isSet("layout")) layout = parser.value("layout").toInt(&valid);
    if (!valid || (layout != 1 && layout != 4 && layout != 9 && layout != 16) ||
        parser.values("source").size() > layout) {
        std::cerr << "Invalid layout or too many sources\n";
        return 1;
    }
    XViewer viewer;
    viewer.setWindowTitle(QStringLiteral("MyPlayer 多路监控"));
    viewer.show();
    if (!viewer.OpenPreview(parser.values("source"), layout)) return 1;
    if (parser.isSet("smoke-test")) QTimer::singleShot(1000, &app, &QApplication::quit);
    return app.exec();
}
