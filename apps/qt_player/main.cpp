#include "mainwid.h"
#include "playback_runtime.h"
#include <QApplication>
#include <QFontDatabase>
#include <QDebug>
#include <cstdlib>
#include <cstring>
//#undef main

int main(int argc, char *argv[])
{
//    qDebug() << "123";
    QApplication a(argc, argv);
    bool smokeTest = false;
    for (int index = 1; index < argc; ++index) {
        if (std::strcmp(argv[index], "--smoke-test") == 0) {
            smokeTest = true;
            break;
        }
    }
    QCoreApplication::setApplicationName("MyPlayer");
    QCoreApplication::setApplicationVersion("1.0.0");
    QCoreApplication::setOrganizationName("MyPlayer");
    
    //使用第三方字库，用来作为UI图片 ://res/fa-solid-900.ttf
    QFontDatabase::addApplicationFont("://res/fontawesome-webfont.ttf");
    //QFontDatabase::addApplicationFont("://res/fa-solid-900.ttf");

    if (smokeTest)
    {
        auto runtime = PlaybackRuntime::Create();
        std::_Exit(runtime ? EXIT_SUCCESS : EXIT_FAILURE);
    }

    MainWid w;
    if (w.Init() == false)
    {
        return -1;
    }
    w.show();

    return a.exec();
}
