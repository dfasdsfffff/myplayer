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

namespace Ui {
	class XViewer;
}
class XViewer : public QWidget {
	Q_OBJECT
public:
	explicit XViewer(QWidget* parent = nullptr);
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
	// 开始全部摄像头录制
	void StartRecord();
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
	Ui::XViewer* ui;
	std::array<XCameraWidget*, 16> cam_wids{};
	std::vector<XCameraRecord*> records;
	struct XCamVideo { QString filepath; QDateTime datetime; };
	std::map<QDate, std::vector<XCamVideo>> cam_videos;
	QMenu* left_menu_ = nullptr;
};

#endif // XVIEWER_H
