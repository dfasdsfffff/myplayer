#include "medialist.h"
#include "playlist.h"

#include <QApplication>
#include <QAction>
#include <QDir>
#include <QFile>
#include <QStandardPaths>

#include <iostream>

namespace {

bool Expect(bool condition, const char* message)
{
    if (!condition)
        std::cerr << "FAILED: " << message << '\n';
    return condition;
}

QString LocationAt(const MediaList* list, int row)
{
    const QListWidgetItem* item = list->item(row);
    return item ? item->data(Qt::UserRole).toString() : QString{};
}

void NotifyMutation(MediaList* list, int preferredRow)
{
    QMetaObject::invokeMethod(list, "SigListMutated", Qt::DirectConnection, Q_ARG(int, preferredRow));
}

QAction* FindAction(MediaList* list, const QString& text)
{
    const QList<QAction*> actions = list->findChildren<QAction*>();
    for (QAction* action : actions)
    {
        if (action->text() == text)
            return action;
    }
    return nullptr;
}

} // namespace

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    QStandardPaths::setTestModeEnabled(true);
    const QString configFile = QDir(QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation))
        .filePath("player_config.ini");
    QFile::remove(configFile);

    const QString a = QStringLiteral("https://example.test/a.mp4");
    const QString b = QStringLiteral("https://example.test/b.mp4");
    const QString c = QStringLiteral("https://example.test/c.mp4");
    const QString d = QStringLiteral("https://example.test/d.mp4");

    Playlist playlist;
    if (!Expect(playlist.Init(), "playlist initializes"))
        return 1;
    playlist.OnAddFile(a);
    playlist.OnAddFileAndPlay(b);
    playlist.OnAddFile(c);
    playlist.OnAddFile(d);

    auto* list = playlist.findChild<MediaList*>(QStringLiteral("List"));
    if (!Expect(list && list->count() == 4, "playlist exposes four media rows"))
        return 1;

    QStringList played;
    QObject::connect(&playlist, &Playlist::SigPlay, [&played](const QString& location) {
        played.append(location);
    });

    QListWidgetItem* moved = list->takeItem(1);
    list->insertItem(3, moved);
    NotifyMutation(list, 3);

    if (!Expect(playlist.currentLocation() == b && playlist.rowForLocation(b) == 3,
            "current location tracks B after reorder"))
        return 1;
    if (!Expect(playlist.adjacentLocation(1) == a && playlist.adjacentLocation(-1) == d,
            "adjacent locations are resolved from B's current row"))
        return 1;

    playlist.OnForwardPlay();
    if (!Expect(played == QStringList{a}, "next follows moved current location and wraps to A"))
        return 1;
    playlist.OnBackwardPlay();
    if (!Expect(played == QStringList{a, b}, "previous returns to moved B"))
        return 1;

    QAction* removeAction = FindAction(list, QStringLiteral("移除选择项"));
    if (!Expect(removeAction, "playlist exposes remove-selected action"))
        return 1;

    list->clearSelection();
    list->item(0)->setSelected(true);
    removeAction->trigger();
    if (!Expect(LocationAt(list, 2) == b, "B remains after removing an earlier row"))
        return 1;

    list->clearSelection();
    list->item(2)->setSelected(true);
    removeAction->trigger();

    if (!Expect(playlist.currentLocation() == d,
            "removing current B selects the nearest remaining row D"))
        return 1;

    playlist.OnBackwardPlay();
    playlist.OnForwardPlay();
    if (!Expect(played == QStringList{a, b, c, d},
            "removing current B selects nearest D, then previous/next remain valid"))
        return 1;

    playlist.OnRandomPlay();
    if (!Expect(played.size() == 5 && (played.last() == c || played.last() == d) &&
            playlist.currentLocation() == played.last(),
            "random navigation selects a valid remaining location"))
        return 1;

    while (list->count() > 0)
        delete list->takeItem(0);
    NotifyMutation(list, 0);
    if (!Expect(playlist.currentLocation().isEmpty(), "clearing the list clears current location"))
        return 1;
    playlist.OnForwardPlay();
    if (!Expect(played.size() == 5, "navigation on an empty list emits nothing"))
        return 1;

    QFile::remove(configFile);
    return 0;
}
