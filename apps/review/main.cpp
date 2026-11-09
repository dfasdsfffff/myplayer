#include "review_window.h"

#include <QApplication>
#include <QFileInfo>
#include <QMessageBox>
#include <QString>
#include <QStringList>

#include <exception>

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    const QStringList arguments = app.arguments();
    app.setOrganizationName(QStringLiteral("MyPlayer"));
    app.setApplicationName(QStringLiteral("Review"));

    try {
        review::ReviewWindow window;
        window.show();
        if (arguments.size() > 2) {
            QMessageBox::warning(&window, QObject::tr("参数错误"), QObject::tr("只能指定一个视频或标注工程文件。"));
            return 1;
        }
        if (arguments.size() == 2) {
            const QString file = QFileInfo(arguments.at(1)).absoluteFilePath();
            const bool opened = file.endsWith(QStringLiteral(".review.json"), Qt::CaseInsensitive)
                ? window.openProject(file) : window.openVideo(file);
            if (!opened) {
                return 1;
            }
        }
        return app.exec();
    } catch (const std::exception& e) {
        QMessageBox::critical(nullptr, QObject::tr("无法启动审片工具"), QString::fromUtf8(e.what()));
        return 1;
    }
}
