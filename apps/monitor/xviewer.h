#ifndef XVIEWER_H
#define XVIEWER_H

#include <QWidget>
#include <QDateTime>
#include <QStringList>
#include <array>
#include <map>
#include <vector>

class XCameraWidget;
class XCameraRecord;
class QMenu;
class QLabel;
class QSlider;
class QPushButton;
class QTimeEdit;
namespace player_sdk_qt { class QtPlayer; class VideoWidget; }

namespace Ui {
	class XViewer;
}
class XViewer : public QWidget {
	Q_OBJECT
public:
	explicit XViewer(QWidget* parent = nullptr, bool loadConfiguration = true);
	~XViewer();
	bool OpenPreview(const QStringList& sources, int layout);
signals:
protected:
	void mouseMoveEvent(QMouseEvent* ev) override;
	void mousePressEvent(QMouseEvent* ev) override;
	void mouseReleaseEvent(QMouseEvent* ev) override;
	void contextMenuEvent(QContextMenuEvent*) override;
	// 显示预览窗口
	void view(int count);
	// 刷新列表
	void RefreshCams();
	//
	void SetCam(int index);
	//
private slots:
	void MaxWindow();
	void NormalWindow();
	void view1();
	void view4();
	void view9();
	void view16();
	// 新增
	void AddCam();
	// 修改
	void SetCam();
	// 删除
	void DelCam();
	// 开始勾选摄像头录制（已有任务保持运行）
	void StartRecord();
	// 停止勾选摄像头录制
	void StopSelectedRecord();
	// 停止全部摄像头录制
	void StopRecord();
	// 处理tab的预览按钮的点击事件
	void Preview();
	// 处理tab的回放按钮的点击事件
	void Playback();
	// 处理选中相机
	void SelectCamera(const QModelIndex& index);
	// 处理日历被点击事件
	void SelectDate(QDate date);
	// 处理播放列表的双击事件
	void PlayVideo(const QModelIndex& index);

private:
    QWidget* live_workspace_ = nullptr;
    QWidget* live_tools_ = nullptr;
    QLabel* layout_summary_ = nullptr;
    QLabel* device_summary_ = nullptr;
    std::array<QWidget*, 16> channel_cards_{};
    int current_layout_ = 4;
	void RefreshRecordingState();
	void RefreshRecordings();
    void SetupPlaybackUi();
    void ResetPlayback();
    QWidget* date_filters_ = nullptr;
    player_sdk_qt::QtPlayer* playback_player_ = nullptr;
    player_sdk_qt::VideoWidget* playback_view_ = nullptr;
    QLabel* playback_file_ = nullptr;
    QLabel* playback_clock_ = nullptr;
    QLabel* scan_status_ = nullptr;
    QSlider* playback_position_ = nullptr;
    QPushButton* playback_pause_ = nullptr;
    QTimeEdit* filter_from_ = nullptr;
    QTimeEdit* filter_to_ = nullptr;
    qint64 playback_duration_ = 0;
    QString playback_path_;
	QLabel* recording_summary_ = nullptr;
	QLabel* recording_path_ = nullptr;
	QLabel* recording_empty_ = nullptr;
	Ui::XViewer* ui;
	std::array<XCameraWidget*, 16> cam_wids{};
	std::map<int, XCameraRecord*> records;
	struct XCamVideo { QString filepath; QDateTime datetime; };
	std::map<QDate, std::vector<XCamVideo>> cam_videos;
	QMenu* left_menu_ = nullptr;
};

#endif // XVIEWER_H
