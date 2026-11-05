#include "xviewer.h"
#include <QApplication>
#include <QCommandLineParser>
#include <QTimer>
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
    parser.addOption({"layout", "Preview layout: 1, 4, 9 or 16.", "count", "9"});
    parser.addOption({"smoke-test", "Open the application and exit automatically after one second."});
    parser.process(app);
    bool valid = false;
    const int layout = parser.value("layout").toInt(&valid);
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
