#pragma once

#include "review_document.h"

#include <QHash>
#include <QMainWindow>
#include <play_core/player.h>

class QAction;
class QCloseEvent;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QSlider;
class QStackedWidget;
class QTableWidget;
class QTimer;
class QUndoStack;

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
    void offerRecovery();

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
    void refreshDirtyState();
    void rebuildNotes(int selectedNote = -1);
    int selectedNote() const;
    bool confirmDiscard();
    bool saveProject(bool saveAs);
    void exportProjectCsv();
    void addNote();
    void editNote();
    void deleteNote();
    void seekNote();
    void selectAdjacentNote(int direction);
    void togglePlayback();
    void seekFromSlider();
    void seekRelative(qint64 offsetMs);
    void relinkMedia();
    void applyPlaybackSettings();
    bool submit(const play_core::Request& request, const QString& action);
    void playbackError(const QString& message);
    void showError(const QString& title, const QString& message);
    bool canControlMedia() const;
    bool canSeek() const;
    qint64 sliderPositionMs() const;
    void pushDocumentChange(const QString& text, const ReviewDocument& before,
                            const ReviewDocument& after, int beforeSelection, int afterSelection);
    void applyHistoryState(const ReviewDocument& document, int selectedNote);
    QString recoveryPath() const;
    void scheduleRecovery();
    void writeRecovery();
    void removeRecovery();

    ReviewDocument document_;
    QString projectPath_;
    bool dirty_{false};
    bool baselineDirty_{false};
    quint64 documentGeneration_{0};
    player_sdk_qt::QtPlayer* player_{nullptr};
    player_sdk_qt::VideoWidget* video_{nullptr};
    QLabel* videoPlaceholder_{nullptr};
    QStackedWidget* videoStack_{nullptr};
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
    QSlider* volumeSlider_{nullptr};
    QComboBox* speedCombo_{nullptr};
    QPushButton* playPauseButton_{nullptr};
    QLabel* timeLabel_{nullptr};
    QLabel* mediaLabel_{nullptr};
    QLabel* statusLabel_{nullptr};
    QLabel* errorLabel_{nullptr};
    QLabel* emptyNotesLabel_{nullptr};
    QLabel* saveStateLabel_{nullptr};
    QLineEdit* noteSearch_{nullptr};
    QComboBox* noteCategoryFilter_{nullptr};
    QTableWidget* notesTable_{nullptr};
    QUndoStack* undoStack_{nullptr};
    QTimer* recoveryTimer_{nullptr};
    QAction* saveAction_{nullptr};
    QAction* saveAsAction_{nullptr};
    QAction* exportAction_{nullptr};
    QAction* relinkAction_{nullptr};
    QAction* undoAction_{nullptr};
    QAction* redoAction_{nullptr};
    QAction* addNoteAction_{nullptr};
    QAction* editNoteAction_{nullptr};
    QAction* deleteNoteAction_{nullptr};
    QAction* seekNoteAction_{nullptr};
    QAction* previousNoteAction_{nullptr};
    QAction* nextNoteAction_{nullptr};
};

} // namespace review
