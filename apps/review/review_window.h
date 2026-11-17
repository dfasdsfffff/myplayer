#pragma once

#include "review_document.h"
#include "review_playback_controller.h"
#include <QPointF>

#include <QHash>
#include <QMainWindow>
#include <play_core/player.h>

class QAction;
class QDockWidget;
class QToolBar;
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
class QListWidget;
class QSpinBox;

namespace player_sdk_qt {
class QtPlayer;
class VideoWidget;
}

namespace review {
class ReviewTimeline;
class ReviewCompare;


class ReviewWindow final : public QMainWindow {
    Q_OBJECT
public:
    explicit ReviewWindow(QWidget* parent = nullptr, const QString& settingsFile = {});
    ~ReviewWindow() override;

    bool openVideo(const QString& path);
    bool openProject(const QString& path);
    void offerRecovery();
    bool addMedia(const QString& path);
    bool activateMedia(int index);
    bool compareMedia(int index);
    int mediaCount() const { return static_cast<int>(assets_.size()); }

protected:
    void closeEvent(QCloseEvent* event) override;
    bool eventFilter(QObject*, QEvent*) override;

private:
    struct Asset {
        QString path;
        QString project;
        qint64 position = 0;
    };
    void buildMediaTools();
    void rememberCurrentMedia();
    void refreshMediaList();
    void removeSelectedMedia();
    void updateCompareView();
    QList<Asset> assets_;
    int activeAsset_ = -1;
    int secondaryAsset_ = -1;
    QListWidget* mediaList_ = nullptr;
    ReviewCompare* compare_ = nullptr;
    QComboBox* compareMode_ = nullptr;
    QSpinBox* compareOffset_ = nullptr;
    bool comparisonEnabled_ = false;
    bool activatingMedia_ = false;
    void buildUi();
    void bindPlayer();
    void retirePlayer();
    bool startMedia(bool preserveRange = false);
    void buildReviewTools();
    void setPresentation(bool enabled);
    void saveSettings();
    void updateRange();
    void navigate(qint64 target, bool resume);
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
    void seekRelative(qint64 offsetMs);
    void stepFrame(int direction);
    void jumpToTime();
    void exportFrame();
    void showMediaInfo();
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

    ReviewPlaybackController playbackController_;
    QString settingsFile_;
    QDockWidget* notesDock_{nullptr};
    QToolBar* viewToolbar_{nullptr};
    QAction* followAction_{nullptr};
    QAction* stopScrubAction_{nullptr};
    QAction* presentationAction_{nullptr};
    QAction* inAction_{nullptr};
    QAction* outAction_{nullptr};
    QLabel* rangeLabel_{nullptr};
    QLabel* zoomLabel_{nullptr};
    QByteArray defaultLayout_, presentationLayout_;
    bool presentation_{false}, wasFullScreen_{false}, panningView_{false};
    QPointF lastViewMouse_;
    qint64 startupPosition_{0};
    bool startupPending_{false};
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
    ReviewTimeline* positionSlider_{nullptr};
    QSlider* volumeSlider_{nullptr};
    QComboBox* speedCombo_{nullptr};
    QPushButton* playPauseButton_{nullptr};
    QLabel* timeLabel_{nullptr};
    QLabel* mediaLabel_{nullptr};
    QLabel* statusLabel_{nullptr};
    QLabel* errorLabel_{nullptr};
    QLabel* emptyNotesLabel_{nullptr};
    QLabel* saveStateLabel_{nullptr};
    QLabel* notesCountLabel_{nullptr};
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
    QAction* jumpAction_{nullptr};
    QAction* previousFrameAction_{nullptr};
    QAction* nextFrameAction_{nullptr};
    play_core::OperationId stepOperation_{0};
    QAction* startAction_{nullptr};
    QAction* endAction_{nullptr};
    QAction* loopAction_{nullptr};
    QAction* exportFrameAction_{nullptr};
    QAction* mediaInfoAction_{nullptr};
    std::shared_ptr<const play_core::MediaInfo> mediaInfo_;
    bool hasVideoFrame_{false};
    QAction* nextNoteAction_{nullptr};
};

} // namespace review
