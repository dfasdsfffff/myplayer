#pragma once

#include "review_document.h"

#include <QHash>
#include <QMainWindow>
#include <play_core/player.h>

class QAction;
class QCloseEvent;
class QLabel;
class QPushButton;
class QSlider;
class QTableWidget;

namespace player_sdk_qt {
class QtPlayer;
class VideoWidget;
}

namespace review {

class ReviewWindow final : public QMainWindow {
    Q_OBJECT
public:
    explicit ReviewWindow(QWidget* parent = nullptr);
    ~ReviewWindow() override;

    bool openVideo(const QString& path);
    bool openProject(const QString& path);

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    void buildUi();
    void bindPlayer();
    void retirePlayer();
    bool startMedia();
    void resetPlayback();
    void updateControls();
    void updateTimeDisplay();
    void updateTitle();
    void setDirty(bool dirty);
    void rebuildNotes(int selectedNote = -1);
    int selectedNote() const;
    bool confirmDiscard();
    bool saveProject(bool saveAs);
    void exportProjectCsv();
    void addNote();
    void editNote();
    void deleteNote();
    void seekNote();
    void togglePlayback();
    void seekFromSlider();
    bool submit(const play_core::Request& request, const QString& action);
    void playbackError(const QString& message);
    void showError(const QString& title, const QString& message);
    bool canControlMedia() const;
    bool canSeek() const;
    qint64 sliderPositionMs() const;

    ReviewDocument document_;
    QString projectPath_;
    bool dirty_{false};
    quint64 documentGeneration_{0};
    player_sdk_qt::QtPlayer* player_{nullptr};
    player_sdk_qt::VideoWidget* video_{nullptr};
    quint64 playerGeneration_{0};
    play_core::SessionId session_{0};
    play_core::OperationId openOperation_{0};
    QHash<quint64, QString> pendingOperations_;
    play_core::PlayerState state_{play_core::PlayerState::Closed};
    qint64 positionMs_{0};
    qint64 durationMs_{0};
    bool seekableVideo_{false};
    bool openFailed_{false};
    QSlider* positionSlider_{nullptr};
    QPushButton* playPauseButton_{nullptr};
    QLabel* timeLabel_{nullptr};
    QLabel* mediaLabel_{nullptr};
    QLabel* statusLabel_{nullptr};
    QLabel* errorLabel_{nullptr};
    QTableWidget* notesTable_{nullptr};
    QAction* saveAction_{nullptr};
    QAction* saveAsAction_{nullptr};
    QAction* exportAction_{nullptr};
    QAction* addNoteAction_{nullptr};
    QAction* editNoteAction_{nullptr};
    QAction* deleteNoteAction_{nullptr};
    QAction* seekNoteAction_{nullptr};
};

} // namespace review
