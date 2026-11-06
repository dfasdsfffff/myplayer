#include <player_sdk_qt/qt_player.h>
#include <player_sdk_qt/video_widget.h>
#include <QApplication>
#include <QTimer>
int main(int argc,char** argv) {
    QApplication app(argc,argv);
    player_sdk_qt::QtPlayer player;
    player_sdk_qt::VideoWidget view; view.setPlayer(&player); view.show();
    QTimer::singleShot(50,&app,&QApplication::quit);
    return app.exec();
}
