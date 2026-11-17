#pragma once
#include <QByteArray>
#include <QString>
class QMainWindow;
namespace review {
struct ReviewSettings {
    QByteArray geometry, layout;
    int volume{100};
    double speed{1.0};
    bool follow{true}, stopOnScrub{false};
    static ReviewSettings load(const QString& file = {});
    void save(const QString& file = {}) const;
    void restoreWindow(QMainWindow* window) const;
};
}
