#include <player_sdk_qt/qt_player.h>
#include <player_sdk_qt/video_widget.h>
#include <QApplication>
#include <QDir>
#include <QMessageBox>
#include <QVBoxLayout>
int main(int argc,char** argv) {
    QApplication app(argc,argv);
    QWidget window; window.resize(960,540);
    auto* layout = new QVBoxLayout(&window);
    auto* player = new player_sdk_qt::QtPlayer(&window);
    auto* view = new player_sdk_qt::VideoWidget(&window);
    view->setPlayer(player); layout->addWidget(view);
    QObject::connect(player,&player_sdk_qt::QtPlayer::errorOccurred,&window,[&](const QString& error) {
        QMessageBox::warning(&window,"Playback error",error);
    });
    if (argc > 1) { player->open(QUrl::fromUserInput(QString::fromUtf8(argv[1]),QDir::currentPath(),QUrl::AssumeLocalFile)); player->play(); }
    window.show(); return app.exec();
}
