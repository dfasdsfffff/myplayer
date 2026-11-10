#include "review_window.h"

#include <player_sdk_qt/qt_player.h>
#include <player_sdk_qt/video_widget.h>

#include <QAbstractItemView>
#include <QAction>
#include <QCloseEvent>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGuiApplication>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QScreen>
#include <QSignalBlocker>
#include <QSlider>
#include <QSplitter>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QStatusBar>
#include <QTableWidget>
#include <QTimer>
#include <QToolBar>
#include <QUndoCommand>
#include <QUndoStack>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>
#include <exception>
#include <functional>
#include <utility>
#include <vector>

namespace review {
namespace {
constexpr int SliderSteps = 1000000;
constexpr int MaxNotes = 10000;
constexpr qsizetype MaxNoteText = 10000;
constexpr qint64 MaxPositionMs = 9007199254740991LL;

QString absolutePath(const QString& path)
{
    return QDir::cleanPath(QFileInfo(path).absoluteFilePath());
}

bool samePath(const QString& left, const QString& right)
{
    if (left.isEmpty() || right.isEmpty())
        return false;
    const auto normalized = [](const QString& path) {
        const QFileInfo info(path);
        const QString canonical = info.canonicalFilePath();
        return canonical.isEmpty() ? absolutePath(path) : canonical;
    };
#ifdef Q_OS_WIN
    return normalized(left).compare(normalized(right), Qt::CaseInsensitive) == 0;
#else
    return normalized(left) == normalized(right);
#endif
}

QString chooseFile(QWidget* parent, const QString& name, const QString& title,
                   const QString& filter, bool save, const QString& initial = {},
                   const QString& suffix = {})
{
    QFileDialog dialog(parent, title);
    dialog.setObjectName(name);
    dialog.setOption(QFileDialog::DontUseNativeDialog);
    dialog.setNameFilter(filter);
    dialog.setAcceptMode(save ? QFileDialog::AcceptSave : QFileDialog::AcceptOpen);
    dialog.setFileMode(save ? QFileDialog::AnyFile : QFileDialog::ExistingFile);
    dialog.setDefaultSuffix(suffix);
    if (!initial.isEmpty())
        dialog.selectFile(initial);
    if (dialog.exec() != QDialog::Accepted || dialog.selectedFiles().isEmpty())
        return {};
    return absolutePath(dialog.selectedFiles().constFirst());
}

bool parseTimecode(const QString& text, qint64& milliseconds)
{
    static const QRegularExpression expression(
        QStringLiteral(R"(^\s*(\d+):([0-5]\d):([0-5]\d)\.(\d{3})\s*$)"));
    const QRegularExpressionMatch match = expression.match(text);
    if (!match.hasMatch())
        return false;
    bool hoursOk = false;
    const quint64 hours = match.captured(1).toULongLong(&hoursOk);
    if (!hoursOk || hours > static_cast<quint64>(MaxPositionMs / 3600000))
        return false;
    const quint64 value = hours * 3600000ULL
        + match.captured(2).toUInt() * 60000ULL
        + match.captured(3).toUInt() * 1000ULL
        + match.captured(4).toUInt();
    if (value > static_cast<quint64>(MaxPositionMs))
        return false;
    milliseconds = static_cast<qint64>(value);
    return true;
}

bool editNoteDialog(QWidget* parent, ReviewNote& note, bool adding)
{
    QDialog dialog(parent);
    dialog.setObjectName(QStringLiteral("noteDialog"));
    dialog.setWindowTitle(adding ? QObject::tr("添加标注") : QObject::tr("编辑标注"));
    dialog.resize(480, 350);
    auto* layout = new QVBoxLayout(&dialog);
    auto* form = new QFormLayout;
    auto* time = new QLineEdit(formatTime(note.positionMs), &dialog);
    time->setObjectName(QStringLiteral("noteTimeEdit"));
    time->setToolTip(QObject::tr("格式：小时:分钟:秒.毫秒，例如 00:03:17.420"));
    form->addRow(QObject::tr("时间"), time);
    auto* category = new QComboBox(&dialog);
    category->setObjectName(QStringLiteral("noteCategoryCombo"));
    category->addItems({QStringLiteral("问题"), QStringLiteral("建议"),
                        QStringLiteral("重点"), QStringLiteral("其他")});
    category->setCurrentText(note.category);
    form->addRow(QObject::tr("类别"), category);
    auto* text = new QPlainTextEdit(&dialog);
    text->setObjectName(QStringLiteral("noteTextEdit"));
    text->setPlainText(note.text);
    text->setTabChangesFocus(true);
    form->addRow(QObject::tr("文字"), text);
    layout->addLayout(form);
    auto* validation = new QLabel(&dialog);
    validation->setObjectName(QStringLiteral("noteValidationLabel"));
    validation->setWordWrap(true);
    layout->addWidget(validation);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel,
                                         &dialog);
    buttons->setObjectName(QStringLiteral("noteButtons"));
    buttons->button(QDialogButtonBox::Ok)->setObjectName(QStringLiteral("noteAcceptButton"));
    buttons->button(QDialogButtonBox::Cancel)->setObjectName(QStringLiteral("noteCancelButton"));
    layout->addWidget(buttons);
    const auto validate = [time, text, validation, buttons] {
        qint64 ignored = 0;
        const QString value = text->toPlainText();
        const bool invalidTime = !parseTimecode(time->text(), ignored);
        const bool empty = value.trimmed().isEmpty();
        const bool tooLong = value.size() > MaxNoteText;
        validation->setText(invalidTime ? QObject::tr("时间格式无效，请使用 小时:分钟:秒.毫秒。")
                                  : empty ? QObject::tr("请输入标注文字。")
                                  : tooLong ? QObject::tr("文字最多 10000 个 UTF-16 单元。")
                                            : QString());
        buttons->button(QDialogButtonBox::Ok)->setEnabled(!invalidTime && !empty && !tooLong);
    };
    QObject::connect(time, &QLineEdit::textChanged, &dialog, validate);
    QObject::connect(text, &QPlainTextEdit::textChanged, &dialog, validate);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    validate();
    text->setFocus();
    if (dialog.exec() != QDialog::Accepted)
        return false;
    if (!parseTimecode(time->text(), note.positionMs))
        return false;
    note.category = category->currentText();
    note.text = text->toPlainText();
    return true;
}

QString requestError(const play_core::PlayerError& error)
{
    return error.message.empty() ? QObject::tr("SDK 拒绝了此操作。")
                                 : QString::fromStdString(error.message);
}

class DocumentCommand final : public QUndoCommand {
public:
    using Apply = std::function<void(const ReviewDocument&, int)>;

    DocumentCommand(QString text, ReviewDocument before, ReviewDocument after,
                    int beforeSelection, int afterSelection, Apply apply)
        : before_(std::move(before)), after_(std::move(after)),
          beforeSelection_(beforeSelection), afterSelection_(afterSelection),
          apply_(std::move(apply))
    {
        setText(std::move(text));
    }

    void undo() override { apply_(before_, beforeSelection_); }
    void redo() override { apply_(after_, afterSelection_); }

private:
    ReviewDocument before_;
    ReviewDocument after_;
    int beforeSelection_ = -1;
    int afterSelection_ = -1;
    Apply apply_;
};
} // namespace

ReviewWindow::ReviewWindow(QWidget* parent)
    : QMainWindow(parent), undoStack_(new QUndoStack(this)), recoveryTimer_(new QTimer(this))
{
    setObjectName(QStringLiteral("reviewWindow"));
    recoveryTimer_->setInterval(2000);
    recoveryTimer_->setSingleShot(true);
    connect(recoveryTimer_, &QTimer::timeout, this, &ReviewWindow::writeRecovery);
    connect(undoStack_, &QUndoStack::indexChanged, this, [this] { refreshDirtyState(); });
    buildUi();
    player_ = new player_sdk_qt::QtPlayer(this);
    player_->setObjectName(QStringLiteral("reviewPlayer"));
    bindPlayer();
    undoStack_->setClean();
    resetPlayback();
    updateTitle();
}

ReviewWindow::~ReviewWindow()
{
    retirePlayer();
}

void ReviewWindow::buildUi()
{
    if (const auto* display = QGuiApplication::primaryScreen()) {
        const QRect available = display->availableGeometry();
        resize(std::min(1280, std::max(1, available.width() - 32)),
               std::min(820, std::max(1, available.height() - 32)));
        move(available.center() - rect().center());
    } else {
        resize(1280, 820);
    }
    setMinimumSize(1000, 680);
    setStyleSheet(QStringLiteral(R"(
        QMainWindow, QWidget#reviewCentralWidget { background:#10171f; color:#cad5df; font-family:"Microsoft YaHei UI"; font-size:12px; }
        QWidget#reviewHeader { background:#181e25; }
        QWidget#reviewPlayerPanel, QWidget#reviewNotesPanel { background:#171f29; }
        QWidget#mediaHeader, QWidget#playbackPanel { background:#19232e; border:1px solid #303b48; }
        QLabel { background:transparent; }
        QLabel#brandLabel { color:#edf2f7; font-size:17px; font-weight:bold; }
        QLabel#sectionLabel, QLabel#notesTitle { color:#e4edf5; font-size:15px; font-weight:bold; }
        QLabel#mutedLabel, QLabel#playbackStatusLabel, QLabel#emptyNotesLabel { color:#8395a7; }
        QLabel#playbackErrorLabel, QLabel#noteValidationLabel { color:#ff9690; }
        QPushButton, QToolButton { color:#bfcdd9; background:#24303d; border:1px solid #303b48; border-radius:5px; padding:6px 10px; }
        QPushButton:hover, QToolButton:hover { background:#303e4c; border-color:#526575; }
        QPushButton:checked, QPushButton#primaryButton, QToolButton#primaryButton { background:#233c31; border-color:#56d6a0; color:#78e1b2; }
        QPushButton:disabled, QToolButton:disabled { color:#718497; background:#1c2631; border-color:#283441; }
        QLineEdit, QComboBox, QPlainTextEdit { background:#222d39; color:#dce6ef; border:1px solid #303b48; border-radius:5px; padding:7px; selection-background-color:#284d3e; }
        QComboBox::drop-down { border:none; }
        QSlider::groove:horizontal { height:4px; background:#303b48; border-radius:2px; }
        QSlider::sub-page:horizontal { background:#56d6a0; border-radius:2px; }
        QSlider::handle:horizontal { width:12px; margin:-5px 0; background:#78e1b2; border-radius:6px; }
        QTableWidget { background:#171f29; alternate-background-color:#1c2631; color:#cad5df; border:1px solid #2b3643; gridline-color:#2b3643; outline:none; }
        QTableWidget::item { padding:7px; }
        QTableWidget::item:selected { background:#233c31; color:#78e1b2; }
        QHeaderView::section { background:#222d39; color:#9baabb; border:none; border-right:1px solid #303b48; padding:7px; font-weight:bold; }
        QMenuBar { background:#181e25; color:#9baabb; }
        QMenuBar::item:selected, QMenu::item:selected { background:#233c31; color:#78e1b2; }
        QMenu { background:#222d39; color:#dce6ef; border:1px solid #303b48; }
        QMenu::item { padding:7px 24px; }
        QToolBar { background:transparent; border:none; spacing:6px; }
        QStatusBar { background:#151d27; color:#9baabb; }
        QSplitter::handle { background:#10171f; width:8px; }
    )"));

    auto* fileMenu = menuBar()->addMenu(tr("文件(&F)"));
    auto* openVideoAction = fileMenu->addAction(tr("打开视频…"));
    openVideoAction->setObjectName(QStringLiteral("openVideoAction"));
    openVideoAction->setShortcut(QKeySequence::Open);
    connect(openVideoAction, &QAction::triggered, this, [this] {
        const QString path = chooseFile(this, QStringLiteral("openVideoDialog"), tr("打开视频"),
            tr("视频文件 (*.mp4 *.mkv *.avi *.mov *.webm *.wmv *.m4v *.ts);;所有文件 (*)"), false);
        if (!path.isEmpty())
            openVideo(path);
    });
    auto* openProjectAction = fileMenu->addAction(tr("打开标注工程…"));
    openProjectAction->setObjectName(QStringLiteral("openProjectAction"));
    connect(openProjectAction, &QAction::triggered, this, [this] {
        const QString path = chooseFile(this, QStringLiteral("openProjectDialog"), tr("打开标注工程"),
            tr("审片工程 (*.review.json);;JSON 文件 (*.json)"), false);
        if (!path.isEmpty())
            openProject(path);
    });
    relinkAction_ = fileMenu->addAction(tr("重新关联视频…"));
    relinkAction_->setObjectName(QStringLiteral("relinkMediaAction"));
    connect(relinkAction_, &QAction::triggered, this, &ReviewWindow::relinkMedia);
    fileMenu->addSeparator();
    saveAction_ = fileMenu->addAction(tr("保存"));
    saveAction_->setObjectName(QStringLiteral("saveProjectAction"));
    saveAction_->setShortcut(QKeySequence::Save);
    connect(saveAction_, &QAction::triggered, this, [this] { saveProject(false); });
    saveAsAction_ = fileMenu->addAction(tr("另存为…"));
    saveAsAction_->setObjectName(QStringLiteral("saveProjectAsAction"));
    saveAsAction_->setShortcut(QKeySequence::SaveAs);
    connect(saveAsAction_, &QAction::triggered, this, [this] { saveProject(true); });
    exportAction_ = fileMenu->addAction(tr("导出 CSV…"));
    exportAction_->setObjectName(QStringLiteral("exportCsvAction"));
    connect(exportAction_, &QAction::triggered, this, &ReviewWindow::exportProjectCsv);
    fileMenu->addSeparator();
    auto* quitAction = fileMenu->addAction(tr("退出"));
    quitAction->setShortcut(QKeySequence::Quit);
    connect(quitAction, &QAction::triggered, this, &QWidget::close);

    auto* editMenu = menuBar()->addMenu(tr("编辑(&E)"));
    undoAction_ = undoStack_->createUndoAction(this, tr("撤销"));
    undoAction_->setObjectName(QStringLiteral("undoAction"));
    undoAction_->setShortcut(QKeySequence::Undo);
    redoAction_ = undoStack_->createRedoAction(this, tr("重做"));
    redoAction_->setObjectName(QStringLiteral("redoAction"));
    redoAction_->setShortcut(QKeySequence::Redo);
    editMenu->addActions({undoAction_, redoAction_});

    auto* noteMenu = menuBar()->addMenu(tr("标注(&N)"));
    addNoteAction_ = noteMenu->addAction(tr("添加标注"));
    addNoteAction_->setObjectName(QStringLiteral("addNoteAction"));
    addNoteAction_->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_M));
    connect(addNoteAction_, &QAction::triggered, this, &ReviewWindow::addNote);
    editNoteAction_ = noteMenu->addAction(tr("编辑标注"));
    editNoteAction_->setObjectName(QStringLiteral("editNoteAction"));
    connect(editNoteAction_, &QAction::triggered, this, &ReviewWindow::editNote);
    deleteNoteAction_ = noteMenu->addAction(tr("删除标注"));
    deleteNoteAction_->setObjectName(QStringLiteral("deleteNoteAction"));
    deleteNoteAction_->setShortcut(QKeySequence::Delete);
    connect(deleteNoteAction_, &QAction::triggered, this, &ReviewWindow::deleteNote);
    seekNoteAction_ = noteMenu->addAction(tr("暂停并定位到标注"));
    seekNoteAction_->setObjectName(QStringLiteral("seekNoteAction"));
    connect(seekNoteAction_, &QAction::triggered, this, &ReviewWindow::seekNote);
    previousNoteAction_ = noteMenu->addAction(tr("上一条标注"));
    previousNoteAction_->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_Up));
    connect(previousNoteAction_, &QAction::triggered, this, [this] { selectAdjacentNote(-1); });
    nextNoteAction_ = noteMenu->addAction(tr("下一条标注"));
    nextNoteAction_->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_Down));
    connect(nextNoteAction_, &QAction::triggered, this, [this] { selectAdjacentNote(1); });

    auto* playbackMenu = menuBar()->addMenu(tr("播放(&P)"));
    auto* toggleAction = playbackMenu->addAction(tr("播放/暂停"));
    toggleAction->setShortcut(QKeySequence(Qt::Key_Space));
    toggleAction->setShortcutContext(Qt::WindowShortcut);
    connect(toggleAction, &QAction::triggered, this, &ReviewWindow::togglePlayback);
    auto* backAction = playbackMenu->addAction(tr("后退 5 秒"));
    backAction->setShortcut(QKeySequence(Qt::Key_Left));
    connect(backAction, &QAction::triggered, this, [this] { seekRelative(-5000); });
    auto* forwardAction = playbackMenu->addAction(tr("前进 5 秒"));
    forwardAction->setShortcut(QKeySequence(Qt::Key_Right));
    connect(forwardAction, &QAction::triggered, this, [this] { seekRelative(5000); });

    auto* central = new QWidget(this);
    central->setObjectName(QStringLiteral("reviewCentralWidget"));
    auto* rootLayout = new QVBoxLayout(central);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(0);

    auto* header = new QWidget(central);
    header->setObjectName(QStringLiteral("reviewHeader"));
    header->setFixedHeight(64);
    auto* headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(20, 0, 20, 0);
    headerLayout->setSpacing(12);
    auto* mark = new QLabel(QStringLiteral("M"), header);
    mark->setFixedSize(30, 30);
    mark->setAlignment(Qt::AlignCenter);
    mark->setStyleSheet(QStringLiteral("background:#56d6a0;color:#10271d;border-radius:8px;font-size:18px;font-weight:bold;"));
    headerLayout->addWidget(mark);
    auto* brand = new QLabel(QStringLiteral("MyPlayer Review"), header);
    brand->setObjectName(QStringLiteral("brandLabel"));
    headerLayout->addWidget(brand);
    auto* section = new QLabel(tr("审片工作区"), header);
    section->setStyleSheet(QStringLiteral("color:#6fe0af;font-weight:bold;padding-left:12px;"));
    headerLayout->addWidget(section);
    headerLayout->addStretch();
    saveStateLabel_ = new QLabel(tr("已保存"), header);
    saveStateLabel_->setObjectName(QStringLiteral("mutedLabel"));
    headerLayout->addWidget(saveStateLabel_);
    auto* undoButton = new QPushButton(QStringLiteral("↶"), header);
    undoButton->setFixedWidth(36);
    connect(undoButton, &QPushButton::clicked, undoAction_, &QAction::trigger);
    headerLayout->addWidget(undoButton);
    auto* redoButton = new QPushButton(QStringLiteral("↷"), header);
    redoButton->setFixedWidth(36);
    connect(redoButton, &QPushButton::clicked, redoAction_, &QAction::trigger);
    headerLayout->addWidget(redoButton);
    auto* exportButton = new QPushButton(tr("导出 CSV"), header);
    connect(exportButton, &QPushButton::clicked, exportAction_, &QAction::trigger);
    headerLayout->addWidget(exportButton);
    auto* openButton = new QPushButton(tr("＋ 打开视频"), header);
    openButton->setObjectName(QStringLiteral("primaryButton"));
    connect(openButton, &QPushButton::clicked, openVideoAction, &QAction::trigger);
    headerLayout->addWidget(openButton);
    rootLayout->addWidget(header);

    auto* body = new QSplitter(Qt::Horizontal, central);
    body->setObjectName(QStringLiteral("reviewSplitter"));
    body->setChildrenCollapsible(false);
    body->setContentsMargins(16, 16, 16, 12);

    auto* playback = new QWidget(body);
    playback->setObjectName(QStringLiteral("reviewPlayerPanel"));
    playback->setMinimumWidth(560);
    auto* playbackLayout = new QVBoxLayout(playback);
    playbackLayout->setContentsMargins(0, 0, 0, 0);
    playbackLayout->setSpacing(10);
    auto* mediaHeader = new QWidget(playback);
    mediaHeader->setObjectName(QStringLiteral("mediaHeader"));
    mediaHeader->setFixedHeight(44);
    auto* mediaHeaderLayout = new QHBoxLayout(mediaHeader);
    mediaHeaderLayout->setContentsMargins(12, 0, 12, 0);
    mediaLabel_ = new QLabel(tr("未选择媒体"), mediaHeader);
    mediaLabel_->setObjectName(QStringLiteral("mediaPathLabel"));
    mediaLabel_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    mediaHeaderLayout->addWidget(mediaLabel_, 1);
    auto* relinkButton = new QPushButton(tr("重新关联"), mediaHeader);
    connect(relinkButton, &QPushButton::clicked, relinkAction_, &QAction::trigger);
    mediaHeaderLayout->addWidget(relinkButton);
    playbackLayout->addWidget(mediaHeader);

    videoStack_ = new QStackedWidget(playback);
    videoPlaceholder_ = new QLabel(tr("打开本地视频，开始审片\nCtrl+O 打开视频 · Ctrl+M 添加标注"), videoStack_);
    videoPlaceholder_->setObjectName(QStringLiteral("videoPlaceholder"));
    videoPlaceholder_->setAlignment(Qt::AlignCenter);
    videoPlaceholder_->setWordWrap(true);
    videoPlaceholder_->setStyleSheet(QStringLiteral("background:#080d12;color:#8395a7;border:1px solid #26313d;"));
    videoStack_->addWidget(videoPlaceholder_);
    video_ = new player_sdk_qt::VideoWidget(videoStack_);
    video_->setObjectName(QStringLiteral("reviewVideoWidget"));
    video_->setMinimumSize(480, 270);
    videoStack_->addWidget(video_);
    videoStack_->setCurrentWidget(videoPlaceholder_);
    playbackLayout->addWidget(videoStack_, 1);

    auto* playbackPanel = new QWidget(playback);
    playbackPanel->setObjectName(QStringLiteral("playbackPanel"));
    auto* controlsLayout = new QVBoxLayout(playbackPanel);
    controlsLayout->setContentsMargins(14, 10, 14, 10);
    controlsLayout->setSpacing(8);
    positionSlider_ = new QSlider(Qt::Horizontal, playbackPanel);
    positionSlider_->setObjectName(QStringLiteral("positionSlider"));
    positionSlider_->setAccessibleName(tr("播放进度"));
    positionSlider_->setRange(0, SliderSteps);
    controlsLayout->addWidget(positionSlider_);
    auto* controls = new QHBoxLayout;
    playPauseButton_ = new QPushButton(tr("播放"), playbackPanel);
    playPauseButton_->setObjectName(QStringLiteral("primaryButton"));
    connect(playPauseButton_, &QPushButton::clicked, this, &ReviewWindow::togglePlayback);
    controls->addWidget(playPauseButton_);
    auto* backButton = new QPushButton(tr("−5s"), playbackPanel);
    connect(backButton, &QPushButton::clicked, this, [this] { seekRelative(-5000); });
    controls->addWidget(backButton);
    auto* forwardButton = new QPushButton(tr("+5s"), playbackPanel);
    connect(forwardButton, &QPushButton::clicked, this, [this] { seekRelative(5000); });
    controls->addWidget(forwardButton);
    timeLabel_ = new QLabel(playbackPanel);
    timeLabel_->setObjectName(QStringLiteral("playbackTimeLabel"));
    controls->addWidget(timeLabel_);
    controls->addStretch();
    controls->addWidget(new QLabel(tr("音量"), playbackPanel));
    volumeSlider_ = new QSlider(Qt::Horizontal, playbackPanel);
    volumeSlider_->setObjectName(QStringLiteral("volumeSlider"));
    volumeSlider_->setRange(0, 100);
    volumeSlider_->setValue(100);
    volumeSlider_->setFixedWidth(90);
    controls->addWidget(volumeSlider_);
    speedCombo_ = new QComboBox(playbackPanel);
    speedCombo_->setObjectName(QStringLiteral("speedCombo"));
    for (double rate : {0.5, 0.75, 1.0, 1.25, 1.5, 2.0})
        speedCombo_->addItem(QString::number(rate, 'f', rate == 0.75 || rate == 1.25 ? 2 : 1) + QStringLiteral("×"), rate);
    speedCombo_->setCurrentIndex(speedCombo_->findData(1.0));
    controls->addWidget(speedCombo_);
    controlsLayout->addLayout(controls);
    playbackLayout->addWidget(playbackPanel);

    auto* annotation = new QWidget(body);
    annotation->setObjectName(QStringLiteral("reviewNotesPanel"));
    annotation->setMinimumWidth(360);
    auto* annotationLayout = new QVBoxLayout(annotation);
    annotationLayout->setContentsMargins(14, 14, 14, 10);
    annotationLayout->setSpacing(10);
    auto* titleRow = new QHBoxLayout;
    auto* notesTitle = new QLabel(tr("审片标注"), annotation);
    notesTitle->setObjectName(QStringLiteral("notesTitle"));
    titleRow->addWidget(notesTitle);
    titleRow->addStretch();
    annotationLayout->addLayout(titleRow);
    auto* noteTools = new QToolBar(annotation);
    noteTools->setObjectName(QStringLiteral("noteToolBar"));
    noteTools->addActions({addNoteAction_, editNoteAction_, deleteNoteAction_, previousNoteAction_, nextNoteAction_});
    annotationLayout->addWidget(noteTools);
    auto* filterRow = new QHBoxLayout;
    noteSearch_ = new QLineEdit(annotation);
    noteSearch_->setObjectName(QStringLiteral("noteSearchEdit"));
    noteSearch_->setPlaceholderText(tr("搜索标注内容"));
    filterRow->addWidget(noteSearch_, 1);
    noteCategoryFilter_ = new QComboBox(annotation);
    noteCategoryFilter_->setObjectName(QStringLiteral("noteCategoryFilter"));
    noteCategoryFilter_->addItems({tr("全部类别"), QStringLiteral("问题"), QStringLiteral("建议"),
                                   QStringLiteral("重点"), QStringLiteral("其他")});
    filterRow->addWidget(noteCategoryFilter_);
    annotationLayout->addLayout(filterRow);
    emptyNotesLabel_ = new QLabel(annotation);
    emptyNotesLabel_->setObjectName(QStringLiteral("emptyNotesLabel"));
    emptyNotesLabel_->setAlignment(Qt::AlignCenter);
    emptyNotesLabel_->setWordWrap(true);
    annotationLayout->addWidget(emptyNotesLabel_);
    notesTable_ = new QTableWidget(0, 3, annotation);
    notesTable_->setObjectName(QStringLiteral("notesTable"));
    notesTable_->setHorizontalHeaderLabels({tr("时间"), tr("类别"), tr("标注内容")});
    notesTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    notesTable_->setSelectionMode(QAbstractItemView::SingleSelection);
    notesTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    notesTable_->setWordWrap(false);
    notesTable_->setAlternatingRowColors(true);
    notesTable_->verticalHeader()->hide();
    notesTable_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    notesTable_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    notesTable_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    notesTable_->setContextMenuPolicy(Qt::ActionsContextMenu);
    notesTable_->addActions({editNoteAction_, deleteNoteAction_, seekNoteAction_});
    annotationLayout->addWidget(notesTable_, 1);

    body->setStretchFactor(0, 3);
    body->setStretchFactor(1, 2);
    rootLayout->addWidget(body, 1);
    statusLabel_ = new QLabel(central);
    statusLabel_->setObjectName(QStringLiteral("playbackStatusLabel"));
    statusLabel_->setWordWrap(true);
    statusLabel_->setContentsMargins(18, 3, 18, 3);
    rootLayout->addWidget(statusLabel_);
    errorLabel_ = new QLabel(central);
    errorLabel_->setObjectName(QStringLiteral("playbackErrorLabel"));
    errorLabel_->setWordWrap(true);
    errorLabel_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    errorLabel_->setContentsMargins(18, 3, 18, 3);
    errorLabel_->hide();
    rootLayout->addWidget(errorLabel_);
    setCentralWidget(central);
    statusBar()->setObjectName(QStringLiteral("reviewStatusBar"));

    connect(notesTable_, &QTableWidget::itemSelectionChanged, this, &ReviewWindow::updateControls);
    connect(notesTable_, &QTableWidget::cellDoubleClicked, this, [this](int, int) {
        if (seekNoteAction_->isEnabled())
            seekNoteAction_->trigger();
    });
    connect(noteSearch_, &QLineEdit::textChanged, this, [this] { rebuildNotes(selectedNote()); });
    connect(noteCategoryFilter_, &QComboBox::currentIndexChanged, this, [this] { rebuildNotes(selectedNote()); });
    connect(positionSlider_, &QSlider::sliderReleased, this, &ReviewWindow::seekFromSlider);
    connect(positionSlider_, &QSlider::valueChanged, this, [this] {
        if (positionSlider_->isSliderDown())
            updateTimeDisplay();
        else
            seekFromSlider();
    });
    connect(volumeSlider_, &QSlider::valueChanged, this, [this](int value) {
        if (canControlMedia())
            submit(player_->setVolume(value / 100.0), tr("调整音量"));
    });
    connect(speedCombo_, &QComboBox::currentIndexChanged, this, [this] {
        if (canControlMedia())
            submit(player_->setPlaybackRate(speedCombo_->currentData().toDouble()), tr("调整倍速"));
    });
}

void ReviewWindow::bindPlayer()
{
    video_->setPlayer(player_);
    const quint64 generation = playerGeneration_;
    connect(player_, &player_sdk_qt::QtPlayer::stateChanged, this,
            [this, generation](play_core::PlayerState state) {
        if (generation != playerGeneration_)
            return;
        state_ = state;
        switch (state) {
        case play_core::PlayerState::Closed: statusLabel_->setText(tr("媒体已关闭")); break;
        case play_core::PlayerState::Opening: statusLabel_->setText(tr("正在打开视频…")); break;
        case play_core::PlayerState::Ready:
            statusLabel_->setText(tr("视频已就绪"));
            applyPlaybackSettings();
            break;
        case play_core::PlayerState::Playing: statusLabel_->setText(tr("正在播放")); break;
        case play_core::PlayerState::Paused: statusLabel_->setText(tr("已暂停")); break;
        case play_core::PlayerState::Reconnecting: statusLabel_->setText(tr("正在重新连接…")); break;
        case play_core::PlayerState::Stopped: statusLabel_->setText(tr("已停止")); break;
        case play_core::PlayerState::Closing: statusLabel_->setText(tr("正在关闭媒体…")); break;
        case play_core::PlayerState::Ended: statusLabel_->setText(tr("播放结束，可重新播放")); break;
        case play_core::PlayerState::Failed:
            openFailed_ = true;
            statusLabel_->setText(tr("播放失败，标注仍可编辑和保存"));
            break;
        }
        updateControls();
    });
    connect(player_, &player_sdk_qt::QtPlayer::mediaInfoChanged, this,
            [this, generation](std::shared_ptr<const play_core::MediaInfo> info) {
        if (generation != playerGeneration_)
            return;
        seekableVideo_ = false;
        durationMs_ = 0;
        if (info) {
            const bool hasVideo = info->width > 0 && info->height > 0;
            seekableVideo_ = hasVideo && info->seekable;
            if (info->duration)
                durationMs_ = std::max<qint64>(0, info->duration->count());
            if (!hasVideo)
                playbackError(tr("媒体不含视频轨道，无法添加或定位标注。"));
            else if (!info->seekable)
                playbackError(tr("该视频不可定位，无法添加或定位标注。"));
        }
        updateTimeDisplay();
        updateControls();
    });
    connect(player_, &player_sdk_qt::QtPlayer::positionChanged, this,
            [this, generation](qint64 milliseconds) {
        if (generation != playerGeneration_)
            return;
        positionMs_ = std::max<qint64>(0, milliseconds);
        if (!positionSlider_->isSliderDown()) {
            const QSignalBlocker blocker(positionSlider_);
            const long double fraction = durationMs_ > 0
                ? static_cast<long double>(std::min(positionMs_, durationMs_)) / durationMs_ : 0;
            positionSlider_->setValue(static_cast<int>(fraction * SliderSteps));
        }
        updateTimeDisplay();
    });
    connect(player_, &player_sdk_qt::QtPlayer::volumeChanged, this,
            [this, generation](double volume) {
        if (generation != playerGeneration_)
            return;
        const QSignalBlocker blocker(volumeSlider_);
        volumeSlider_->setValue(qRound(std::clamp(volume, 0.0, 1.0) * 100.0));
    });
    connect(player_, &player_sdk_qt::QtPlayer::operationFinished, this,
            [this, generation](const play_core::OperationResult& result) {
        if (generation != playerGeneration_ || result.session != session_)
            return;
        const QString action = pendingOperations_.take(result.operation);
        if (result.status == play_core::OperationStatus::Failed) {
            if (result.operation == openOperation_) {
                openFailed_ = true;
                updateControls();
            }
            playbackError(tr("%1失败：%2").arg(action.isEmpty() ? tr("播放操作") : action,
                                                   requestError(result.error)));
        }
    });
    connect(player_, &player_sdk_qt::QtPlayer::errorOccurred, this,
            [this, generation](const QString& message) {
        if (generation == playerGeneration_)
            playbackError(message);
    });
}

void ReviewWindow::retirePlayer()
{
    ++playerGeneration_;
    if (player_) {
        disconnect(player_, nullptr, this, nullptr);
        video_->setPlayer(nullptr);
        delete player_;
        player_ = nullptr;
    }
    pendingOperations_.clear();
}

void ReviewWindow::resetPlayback()
{
    session_ = 0;
    openOperation_ = 0;
    state_ = play_core::PlayerState::Closed;
    positionMs_ = 0;
    durationMs_ = 0;
    seekableVideo_ = false;
    openFailed_ = false;
    pendingOperations_.clear();
    {
        const QSignalBlocker blocker(positionSlider_);
        positionSlider_->setSliderDown(false);
        positionSlider_->setValue(0);
    }
    errorLabel_->clear();
    errorLabel_->hide();
    videoStack_->setCurrentWidget(videoPlaceholder_);
    statusLabel_->setText(tr("请选择本地视频或标注工程"));
    updateTimeDisplay();
    updateControls();
}

bool ReviewWindow::startMedia()
{
    if (session_ != 0 || state_ != play_core::PlayerState::Closed)
        retirePlayer();
    resetPlayback();
    const QFileInfo media(document_.mediaPath);
    if (!media.isFile() || !media.isReadable()) {
        statusLabel_->setText(tr("媒体缺失或不可读：%1。可重新关联视频，标注仍可编辑、保存和导出。")
                                 .arg(QDir::toNativeSeparators(document_.mediaPath)));
        return false;
    }
    try {
        if (!player_) {
            player_ = new player_sdk_qt::QtPlayer(this);
            player_->setObjectName(QStringLiteral("reviewPlayer"));
            bindPlayer();
        }
    } catch (const std::exception& exception) {
        openFailed_ = true;
        showError(tr("无法初始化播放器"), QString::fromUtf8(exception.what()));
        updateControls();
        return false;
    } catch (...) {
        openFailed_ = true;
        showError(tr("无法初始化播放器"), tr("SDK 初始化发生未知异常。"));
        updateControls();
        return false;
    }
    state_ = play_core::PlayerState::Opening;
    statusLabel_->setText(tr("正在打开视频…"));
    updateControls();
    const auto request = player_->open(QUrl::fromLocalFile(document_.mediaPath));
    session_ = request.session;
    openOperation_ = request.operation;
    if (!submit(request, tr("打开视频"))) {
        openFailed_ = true;
        state_ = play_core::PlayerState::Failed;
        updateControls();
        return false;
    }
    videoStack_->setCurrentWidget(video_);
    submit(player_->play(), tr("播放"));
    return true;
}

bool ReviewWindow::openVideo(const QString& path)
{
    if (path.isEmpty())
        return false;
    const QString media = absolutePath(path);
    const QFileInfo info(media);
    if (!info.isFile() || !info.isReadable()) {
        showError(tr("无法打开视频"), tr("视频不存在或不可读：%1").arg(QDir::toNativeSeparators(media)));
        return false;
    }
    if (!confirmDiscard())
        return false;
    removeRecovery();
    document_ = ReviewDocument{media, {}};
    ++documentGeneration_;
    projectPath_.clear();
    undoStack_->clear();
    baselineDirty_ = true;
    mediaLabel_->setText(info.fileName());
    mediaLabel_->setToolTip(QDir::toNativeSeparators(media));
    rebuildNotes();
    refreshDirtyState();
    return startMedia();
}

bool ReviewWindow::openProject(const QString& path)
{
    if (path.isEmpty())
        return false;
    ReviewDocument loaded;
    QString error;
    const QString project = absolutePath(path);
    if (!loadDocument(project, loaded, error)) {
        showError(tr("无法载入工程"), error);
        return false;
    }
    if (!confirmDiscard())
        return false;
    removeRecovery();
    document_ = std::move(loaded);
    ++documentGeneration_;
    projectPath_ = project;
    undoStack_->clear();
    baselineDirty_ = false;
    undoStack_->setClean();
    mediaLabel_->setText(QFileInfo(document_.mediaPath).fileName());
    mediaLabel_->setToolTip(QDir::toNativeSeparators(document_.mediaPath));
    rebuildNotes();
    refreshDirtyState();
    startMedia();
    return true;
}

void ReviewWindow::offerRecovery()
{
    const QString path = recoveryPath();
    if (!QFileInfo::exists(path))
        return;
    ReviewRecovery recovery;
    QString error;
    if (!loadRecovery(path, recovery, error)) {
        QMessageBox::warning(this, tr("自动恢复不可用"),
                             tr("发现恢复文件，但无法读取：\n%1\n文件已保留：%2")
                                 .arg(error, QDir::toNativeSeparators(path)));
        return;
    }
    QMessageBox dialog(QMessageBox::Question, tr("恢复未保存工程"),
                       tr("检测到上次未正常保存的审片内容，是否恢复？"),
                       QMessageBox::Yes | QMessageBox::No, this);
    dialog.setObjectName(QStringLiteral("recoveryDialog"));
    dialog.button(QMessageBox::Yes)->setText(tr("恢复"));
    dialog.button(QMessageBox::No)->setText(tr("放弃"));
    if (dialog.exec() != QMessageBox::Yes) {
        removeRecovery();
        return;
    }
    document_ = std::move(recovery.document);
    projectPath_ = std::move(recovery.projectPath);
    ++documentGeneration_;
    undoStack_->clear();
    baselineDirty_ = true;
    mediaLabel_->setText(QFileInfo(document_.mediaPath).fileName());
    mediaLabel_->setToolTip(QDir::toNativeSeparators(document_.mediaPath));
    rebuildNotes();
    refreshDirtyState();
    startMedia();
    statusBar()->showMessage(tr("已恢复未保存内容。"), 5000);
}

bool ReviewWindow::canControlMedia() const
{
    if (!player_ || !session_ || openFailed_)
        return false;
    return state_ == play_core::PlayerState::Ready || state_ == play_core::PlayerState::Playing
        || state_ == play_core::PlayerState::Paused || state_ == play_core::PlayerState::Stopped;
}

bool ReviewWindow::canSeek() const
{
    return canControlMedia() && seekableVideo_;
}

void ReviewWindow::updateControls()
{
    const bool hasDocument = !document_.mediaPath.isEmpty();
    const bool selected = selectedNote() >= 0;
    const bool replay = player_ && session_ && state_ == play_core::PlayerState::Ended && !openFailed_;
    playPauseButton_->setEnabled(canControlMedia() || replay);
    playPauseButton_->setText(state_ == play_core::PlayerState::Playing ? tr("暂停")
                              : replay ? tr("重新播放") : tr("播放"));
    positionSlider_->setEnabled(canSeek() && durationMs_ > 0);
    volumeSlider_->setEnabled(canControlMedia());
    speedCombo_->setEnabled(canControlMedia());
    addNoteAction_->setEnabled(canSeek() && document_.notes.size() < MaxNotes);
    editNoteAction_->setEnabled(selected);
    deleteNoteAction_->setEnabled(selected);
    seekNoteAction_->setEnabled(selected && canSeek());
    previousNoteAction_->setEnabled(notesTable_->rowCount() > 0);
    nextNoteAction_->setEnabled(notesTable_->rowCount() > 0);
    saveAction_->setEnabled(hasDocument);
    saveAsAction_->setEnabled(hasDocument);
    exportAction_->setEnabled(hasDocument);
    relinkAction_->setEnabled(hasDocument);
    const bool noVisibleNotes = notesTable_->rowCount() == 0;
    emptyNotesLabel_->setVisible(noVisibleNotes);
    if (noVisibleNotes) {
        const bool filtered = !noteSearch_->text().trimmed().isEmpty() || noteCategoryFilter_->currentIndex() > 0;
        emptyNotesLabel_->setText(filtered ? tr("没有匹配的标注。")
            : !hasDocument ? tr("暂无标注")
            : canSeek() ? tr("点击“添加标注”或按 Ctrl+M，记录当前画面。")
                        : tr("媒体不可定位；可重新关联视频，已有标注仍可编辑。"));
    }
    notesTable_->setToolTip(canSeek() ? tr("双击标注可暂停并定位。")
                                     : tr("当前无法定位；已有标注仍可编辑、删除、保存和导出。"));
}

qint64 ReviewWindow::sliderPositionMs() const
{
    return static_cast<qint64>(static_cast<long double>(durationMs_) * positionSlider_->value() / SliderSteps);
}

void ReviewWindow::updateTimeDisplay()
{
    const qint64 shown = positionSlider_->isSliderDown() ? sliderPositionMs() : positionMs_;
    timeLabel_->setText(tr("%1 / %2").arg(formatTime(shown),
                                         durationMs_ > 0 ? formatTime(durationMs_) : tr("未知时长")));
}


void ReviewWindow::refreshDirtyState()
{
    dirty_ = baselineDirty_ || !undoStack_->isClean();
    updateTitle();
    if (saveStateLabel_)
        saveStateLabel_->setText(dirty_ ? tr("● 等待自动保存") : tr("● 已保存"));
    if (dirty_)
        scheduleRecovery();
    else
        removeRecovery();
}

void ReviewWindow::updateTitle()
{
    const QString name = !projectPath_.isEmpty() ? QFileInfo(projectPath_).fileName()
                         : !document_.mediaPath.isEmpty() ? QFileInfo(document_.mediaPath).fileName()
                                                        : tr("未命名工程");
    setWindowTitle(tr("%1[*] — MyPlayer Review").arg(name));
    setWindowModified(dirty_);
}

int ReviewWindow::selectedNote() const
{
    const int row = notesTable_->currentRow();
    if (row < 0 || !notesTable_->item(row, 0) || !notesTable_->item(row, 0)->isSelected())
        return -1;
    const int index = notesTable_->item(row, 0)->data(Qt::UserRole).toInt();
    return index >= 0 && index < document_.notes.size() ? index : -1;
}

void ReviewWindow::rebuildNotes(int selected)
{
    const QString search = noteSearch_ ? noteSearch_->text().trimmed() : QString();
    const QString category = noteCategoryFilter_ && noteCategoryFilter_->currentIndex() > 0
        ? noteCategoryFilter_->currentText() : QString();
    std::vector<int> order;
    order.reserve(static_cast<std::size_t>(document_.notes.size()));
    for (int index = 0; index < document_.notes.size(); ++index) {
        const auto& note = document_.notes.at(index);
        if ((!category.isEmpty() && note.category != category)
            || (!search.isEmpty() && !note.text.contains(search, Qt::CaseInsensitive)))
            continue;
        order.push_back(index);
    }
    std::stable_sort(order.begin(), order.end(), [this](int left, int right) {
        return document_.notes.at(left).positionMs < document_.notes.at(right).positionMs;
    });
    const QSignalBlocker blocker(notesTable_);
    notesTable_->clearContents();
    notesTable_->setRowCount(static_cast<int>(order.size()));
    for (int row = 0; row < static_cast<int>(order.size()); ++row) {
        const int index = order[static_cast<std::size_t>(row)];
        const auto& note = document_.notes.at(index);
        auto* time = new QTableWidgetItem(formatTime(note.positionMs));
        time->setData(Qt::UserRole, index);
        notesTable_->setItem(row, 0, time);
        notesTable_->setItem(row, 1, new QTableWidgetItem(note.category));
        notesTable_->setItem(row, 2, new QTableWidgetItem(note.text));
        if (index == selected)
            notesTable_->selectRow(row);
    }
    updateControls();
}

bool ReviewWindow::confirmDiscard()
{
    if (!dirty_)
        return true;
    QMessageBox dialog(QMessageBox::Warning, tr("未保存的工程"),
                       tr("当前工程有未保存的更改。是否先保存？"),
                       QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, this);
    dialog.setObjectName(QStringLiteral("unsavedChangesDialog"));
    dialog.setDefaultButton(QMessageBox::Save);
    dialog.setEscapeButton(QMessageBox::Cancel);
    const int result = dialog.exec();
    if (result == QMessageBox::Save)
        return saveProject(false);
    if (result == QMessageBox::Discard) {
        removeRecovery();
        return true;
    }
    return false;
}

bool ReviewWindow::saveProject(bool saveAs)
{
    if (document_.mediaPath.isEmpty())
        return false;
    QString path = projectPath_;
    if (saveAs || path.isEmpty()) {
        const QString initial = path.isEmpty() ? document_.mediaPath + QStringLiteral(".review.json") : path;
        path = chooseFile(this, QStringLiteral("saveProjectDialog"), tr("保存标注工程"),
                          tr("审片工程 (*.review.json)"), true, initial, QStringLiteral("review.json"));
        if (path.isEmpty())
            return false;
    }
    QString error;
    if (!saveDocument(path, document_, error)) {
        showError(tr("保存失败"), error);
        return false;
    }
    projectPath_ = path;
    baselineDirty_ = false;
    undoStack_->setClean();
    refreshDirtyState();
    removeRecovery();
    statusBar()->showMessage(tr("工程已保存：%1").arg(QDir::toNativeSeparators(path)), 5000);
    return true;
}

void ReviewWindow::exportProjectCsv()
{
    if (document_.mediaPath.isEmpty())
        return;
    const QString initial = projectPath_.isEmpty() ? document_.mediaPath + QStringLiteral(".csv")
                                                  : projectPath_ + QStringLiteral(".csv");
    const QString path = chooseFile(this, QStringLiteral("exportCsvDialog"), tr("导出 CSV"),
                                    tr("CSV 文件 (*.csv)"), true, initial, QStringLiteral("csv"));
    if (path.isEmpty())
        return;
    if (samePath(path, projectPath_)) {
        showError(tr("导出失败"), tr("CSV 不能覆盖当前标注工程。请选择其他文件。"));
        return;
    }
    QString error;
    if (!exportCsv(path, document_, error)) {
        showError(tr("导出失败"), error);
        return;
    }
    statusBar()->showMessage(tr("CSV 已导出：%1").arg(QDir::toNativeSeparators(path)), 5000);
}

void ReviewWindow::addNote()
{
    if (!canSeek() || document_.notes.size() >= MaxNotes)
        return;
    ReviewNote note{positionMs_, QStringLiteral("问题"), {}};
    const quint64 generation = documentGeneration_;
    if (!editNoteDialog(this, note, true) || generation != documentGeneration_)
        return;
    ReviewDocument after = document_;
    after.notes.append(std::move(note));
    pushDocumentChange(tr("添加标注"), document_, after, selectedNote(), after.notes.size() - 1);
}

void ReviewWindow::editNote()
{
    const int index = selectedNote();
    if (index < 0)
        return;
    ReviewNote note = document_.notes.at(index);
    const quint64 generation = documentGeneration_;
    if (!editNoteDialog(this, note, false) || generation != documentGeneration_)
        return;
    const auto& original = document_.notes.at(index);
    if (note.positionMs == original.positionMs && note.category == original.category && note.text == original.text)
        return;
    ReviewDocument after = document_;
    after.notes[index] = std::move(note);
    pushDocumentChange(tr("编辑标注"), document_, after, index, index);
}

void ReviewWindow::deleteNote()
{
    const int index = selectedNote();
    if (index < 0)
        return;
    ReviewDocument after = document_;
    after.notes.removeAt(index);
    const int next = after.notes.isEmpty() ? -1 : std::min(index, static_cast<int>(after.notes.size()) - 1);
    pushDocumentChange(tr("删除标注"), document_, after, index, next);
}

void ReviewWindow::seekNote()
{
    const int index = selectedNote();
    if (index < 0 || !canSeek())
        return;
    errorLabel_->clear();
    errorLabel_->hide();
    if (!submit(player_->setPaused(true), tr("暂停")))
        return;
    submit(player_->seek(document_.notes.at(index).positionMs), tr("定位标注"));
}

void ReviewWindow::selectAdjacentNote(int direction)
{
    if (notesTable_->rowCount() <= 0)
        return;
    int row = notesTable_->currentRow();
    if (row < 0)
        row = direction > 0 ? 0 : notesTable_->rowCount() - 1;
    else
        row = std::clamp(row + direction, 0, notesTable_->rowCount() - 1);
    notesTable_->selectRow(row);
    if (canSeek())
        seekNote();
}

void ReviewWindow::togglePlayback()
{
    if (player_ && state_ == play_core::PlayerState::Ended) {
        startMedia();
        return;
    }
    if (!canControlMedia())
        return;
    errorLabel_->clear();
    errorLabel_->hide();
    if (state_ == play_core::PlayerState::Playing)
        submit(player_->setPaused(true), tr("暂停"));
    else if (state_ == play_core::PlayerState::Paused)
        submit(player_->setPaused(false), tr("恢复播放"));
    else
        submit(player_->play(), tr("播放"));
}

void ReviewWindow::seekFromSlider()
{
    if (!canSeek() || durationMs_ <= 0)
        return;
    errorLabel_->clear();
    errorLabel_->hide();
    submit(player_->seek(sliderPositionMs()), tr("定位"));
    updateTimeDisplay();
}

void ReviewWindow::seekRelative(qint64 offsetMs)
{
    if (!canSeek())
        return;
    const qint64 target = std::clamp(positionMs_ + offsetMs, qint64{0},
                                     durationMs_ > 0 ? durationMs_ : MaxPositionMs);
    submit(player_->seek(target), offsetMs < 0 ? tr("后退 5 秒") : tr("前进 5 秒"));
}

void ReviewWindow::relinkMedia()
{
    if (document_.mediaPath.isEmpty())
        return;
    const QString path = chooseFile(this, QStringLiteral("relinkMediaDialog"), tr("重新关联视频"),
        tr("视频文件 (*.mp4 *.mkv *.avi *.mov *.webm *.wmv *.m4v *.ts);;所有文件 (*)"), false,
        document_.mediaPath);
    if (path.isEmpty() || samePath(path, document_.mediaPath))
        return;
    ReviewDocument after = document_;
    after.mediaPath = path;
    pushDocumentChange(tr("重新关联视频"), document_, after, selectedNote(), selectedNote());
}

void ReviewWindow::applyPlaybackSettings()
{
    if (!canControlMedia())
        return;
    submit(player_->setVolume(volumeSlider_->value() / 100.0), tr("设置音量"));
    submit(player_->setPlaybackRate(speedCombo_->currentData().toDouble()), tr("设置倍速"));
}

bool ReviewWindow::submit(const play_core::Request& request, const QString& action)
{
    if (!request) {
        playbackError(tr("%1失败：%2").arg(action, requestError(request.error)));
        return false;
    }
    pendingOperations_.insert(request.operation, action);
    return true;
}

void ReviewWindow::playbackError(const QString& message)
{
    errorLabel_->setText(tr("错误：%1").arg(message));
    errorLabel_->show();
}

void ReviewWindow::showError(const QString& title, const QString& message)
{
    QMessageBox dialog(QMessageBox::Critical, title, message, QMessageBox::Ok, this);
    dialog.setObjectName(QStringLiteral("reviewErrorDialog"));
    dialog.setTextFormat(Qt::PlainText);
    dialog.exec();
}

void ReviewWindow::pushDocumentChange(const QString& text, const ReviewDocument& before,
                                      const ReviewDocument& after, int beforeSelection,
                                      int afterSelection)
{
    undoStack_->push(new DocumentCommand(text, before, after, beforeSelection, afterSelection,
        [this](const ReviewDocument& state, int selected) { applyHistoryState(state, selected); }));
}

void ReviewWindow::applyHistoryState(const ReviewDocument& document, int selected)
{
    const bool mediaChanged = !samePath(document_.mediaPath, document.mediaPath);
    document_ = document;
    ++documentGeneration_;
    mediaLabel_->setText(QFileInfo(document_.mediaPath).fileName());
    mediaLabel_->setToolTip(QDir::toNativeSeparators(document_.mediaPath));
    rebuildNotes(selected);
    if (mediaChanged)
        startMedia();
}

QString ReviewWindow::recoveryPath() const
{
    return QDir(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation))
        .filePath(QStringLiteral("review-recovery.json"));
}

void ReviewWindow::scheduleRecovery()
{
    if (!document_.mediaPath.isEmpty())
        recoveryTimer_->start();
}

void ReviewWindow::writeRecovery()
{
    if (!dirty_ || document_.mediaPath.isEmpty())
        return;
    QString error;
    if (!saveRecovery(recoveryPath(), ReviewRecovery{projectPath_, document_}, error)) {
        saveStateLabel_->setText(tr("● 自动恢复写入失败"));
        statusBar()->showMessage(tr("自动恢复写入失败：%1").arg(error), 8000);
        return;
    }
    saveStateLabel_->setText(tr("● 已自动备份"));
}

void ReviewWindow::removeRecovery()
{
    recoveryTimer_->stop();
    QFile::remove(recoveryPath());
}

void ReviewWindow::closeEvent(QCloseEvent* event)
{
    if (!confirmDiscard()) {
        event->ignore();
        return;
    }
    removeRecovery();
    event->accept();
}

} // namespace review
