/*
 * @file 	mainwid.cpp
 * @date 	2018/03/10 22:26
 *
 * @author 	itisyang
 * @Contact	itisyang@gmail.com
 *
 * @brief 	主窗口
 * @note
 */
#include <QFile>
#include <QPainter>
#include <QtMath>
#include <QDebug>
#include <QAbstractItemView>
#include <QMimeData>
#include <QSizeGrip>
#include <QWindow>
#include <QScreen>
#include <QRect>
#include <QFileDialog>
#include <QJsonDocument>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QJsonParseError>
#include <QApplication>
#include <QStatusBar>
#include <QFileInfo>
#include <QKeySequence>


#include "mainwid.h"
#include "ui_mainwid.h"
#include "globalhelper.h"
#include "videoctl.h"
#include "enums.h"

const int FULLSCREEN_CTRLBAR_HIDE_DELAY = 2000; // 控制面板隐藏延迟（毫秒）
const int CTRLBAR_ANIMATION_DURATION = 1000;    // 动画持续时间（毫秒）
const int MAX_RECENT_FILES = 10;

MainWid::MainWid(QMainWindow* parent) :
	QMainWindow(parent),
	ui(new Ui::MainWid),
	m_nShadowWidth(0),
	m_stMenu(this),
	m_stPlaylist(this),
	m_stTitle(this),
	m_bMoveDrag(false),
	m_stActFullscreen(this)
{
	ui->setupUi(this);
	//无边框、无系统菜单、 任务栏点击最小化
	setWindowFlags(Qt::FramelessWindowHint /*| Qt::WindowSystemMenuHint*/ | Qt::WindowMinimizeButtonHint);
	//设置任务栏图标
	this->setWindowIcon(QIcon("://res/player.png"));
	//加载样式（使用统一设计系统）
	QString qss = GlobalHelper::GetThemeStr("://res/qss/mainwid.css");
	setStyleSheet(qss);

	// 追踪鼠标 用于播放时隐藏鼠标
	this->setMouseTracking(true);

	m_bFullScreenPlay = false;

	m_stCtrlBarAnimationTimer.setInterval(FULLSCREEN_CTRLBAR_HIDE_DELAY);


}

MainWid::~MainWid()
{
	delete ui;
}

bool MainWid::Init()
{
	// 恢复上次保存的窗口状态
	QByteArray geometry, windowState;
	GlobalHelper::RestoreWindowState(geometry, windowState);
	if (!geometry.isEmpty())
	{
		restoreGeometry(geometry);
	}
	if (!windowState.isEmpty())
	{
		restoreState(windowState);
	}

	// 加载播放设置（音量、循环模式、播放速度）
	double volume = 1.0;
	int loopPolicy = 0;
	double speed = 1.0;
	GlobalHelper::LoadPlaySettings(volume, loopPolicy, speed);

	// 去除播放列表标题栏自带的边框
	QWidget* em = new QWidget(this);
	ui->PlaylistWid->setTitleBarWidget(em);
	ui->PlaylistWid->setWidget(&m_stPlaylist);
	//ui->PlaylistWid->setFixedWidth(100);

	// 去除标题栏自带的边框
	QWidget* emTitle = new QWidget(this);
	ui->TitleWid->setTitleBarWidget(emTitle);
	ui->TitleWid->setWidget(&m_stTitle);

	//连接自定义信号与槽
	if (ConnectSignalSlots() == false)
	{
		return false;
	}

	if (ui->CtrlBarWid->Init() == false ||
		m_stPlaylist.Init() == false ||
		ui->ShowWid->Init() == false ||
		m_stTitle.Init() == false)
	{
		return false;
	}


	m_stCtrlbarAnimationShow = new QPropertyAnimation(ui->CtrlBarWid, "geometry");
	m_stCtrlbarAnimationHide = new QPropertyAnimation(ui->CtrlBarWid, "geometry");

	if (m_stAboutWidget.Init() == false)
	{
		return false;
	}



	InitMenu();


	return true;
}

void MainWid::paintEvent(QPaintEvent* event)
{
	Q_UNUSED(event);
}


void MainWid::enterEvent(QEvent* event)
{
	Q_UNUSED(event);

}

void MainWid::leaveEvent(QEvent* event)
{
	Q_UNUSED(event);

}

bool MainWid::ConnectSignalSlots()
{
	// 创建 VideoCtl 的 Qt 桥接，将原生 Signal 转发为 Qt signals
	m_pVideoCtlBridge = new VideoCtlBridge(this);
	m_pVideoCtlBridge->attach(VideoCtl::GetInstance());

	//连接信号与槽
	connect(&m_stTitle, &Title::SigCloseBtnClicked, this, &MainWid::OnCloseBtnClicked);
	connect(&m_stTitle, &Title::SigMaxBtnClicked, this, &MainWid::OnMaxBtnClicked);
	connect(&m_stTitle, &Title::SigMinBtnClicked, this, &MainWid::OnMinBtnClicked);
	connect(&m_stTitle, &Title::SigDoubleClicked, this, &MainWid::OnMaxBtnClicked);
	connect(&m_stTitle, &Title::SigFullScreenBtnClicked, this, &MainWid::OnFullScreenPlay);
	connect(&m_stTitle, &Title::SigOpenFile, &m_stPlaylist, &Playlist::OnAddFileAndPlay);
	connect(&m_stTitle, &Title::SigShowMenu, this, &MainWid::OnShowMenu);

	connect(&m_stPlaylist, &Playlist::SigPlay, this, &MainWid::OnPlayFile);

	connect(ui->ShowWid, &Show::SigOpenFile, &m_stPlaylist, &Playlist::OnAddFileAndPlay);
	connect(ui->ShowWid, &Show::SigFullScreen, this, &MainWid::OnFullScreenPlay);
	connect(ui->ShowWid, &Show::SigPlayOrPause, this, []() { VideoCtl::GetInstance()->OnPause(); });
	connect(ui->ShowWid, &Show::SigStop, this, []() { VideoCtl::GetInstance()->OnStop(); });
	connect(ui->ShowWid, &Show::SigShowMenu, this, &MainWid::OnShowMenu);
	connect(ui->ShowWid, &Show::SigSeekForward, this, []() { VideoCtl::GetInstance()->OnSeekForward(); });
	connect(ui->ShowWid, &Show::SigSeekBack, this, []() { VideoCtl::GetInstance()->OnSeekBack(); });
	connect(ui->ShowWid, &Show::SigAddVolume, this, []() { VideoCtl::GetInstance()->OnAddVolume(); });
	connect(ui->ShowWid, &Show::SigSubVolume, this, []() { VideoCtl::GetInstance()->OnSubVolume(); });

	connect(ui->CtrlBarWid, &CtrlBar::SigShowOrHidePlaylist, this, &MainWid::OnShowOrHidePlaylist);
	connect(ui->CtrlBarWid, &CtrlBar::SigPlaySeek, this, [](double d) { VideoCtl::GetInstance()->OnPlaySeek(d); });
	connect(ui->CtrlBarWid, &CtrlBar::SigPlayVolume, this, [](double d) { VideoCtl::GetInstance()->OnPlayVolume(d); });
	connect(ui->CtrlBarWid, &CtrlBar::SigPlayOrPause, this, []() { VideoCtl::GetInstance()->OnPause(); });
	connect(ui->CtrlBarWid, &CtrlBar::SigStop, this, []() { VideoCtl::GetInstance()->OnStop(); });
	connect(ui->CtrlBarWid, &CtrlBar::SigPlayLoopPolicyChanged, this, [](VideoLoopPolicy p) { VideoCtl::GetInstance()->set_play_loop_policy(p); });
	connect(ui->CtrlBarWid, &CtrlBar::SigBackwardPlay, &m_stPlaylist, &Playlist::OnBackwardPlay);
	connect(ui->CtrlBarWid, &CtrlBar::SigForwardPlay, &m_stPlaylist, &Playlist::OnForwardPlay);
	connect(ui->CtrlBarWid, &CtrlBar::SigShowMenu, this, &MainWid::OnShowMenu);
	connect(ui->CtrlBarWid, &CtrlBar::SigShowSetting, this, &MainWid::OnShowSettingWid);
	connect(ui->CtrlBarWid, &CtrlBar::SigSpeedChanged, this, &MainWid::OnSpeedChanged);

	connect(this, &MainWid::SigShowMax, &m_stTitle, &Title::OnChangeMaxBtnStyle);
	connect(this, &MainWid::SigSeekForward, this, []() { VideoCtl::GetInstance()->OnSeekForward(); });
	connect(this, &MainWid::SigSeekBack, this, []() { VideoCtl::GetInstance()->OnSeekBack(); });
	connect(this, &MainWid::SigAddVolume, this, []() { VideoCtl::GetInstance()->OnAddVolume(); });
	connect(this, &MainWid::SigSubVolume, this, []() { VideoCtl::GetInstance()->OnSubVolume(); });
	connect(this, &MainWid::SigOpenFile, &m_stPlaylist, &Playlist::OnAddFileAndPlay);


	// VideoCtl→UI 方向：通过 bridge 转发（bridge 已保证主线程投递，无需 Qt::QueuedConnection）
	connect(m_pVideoCtlBridge, &VideoCtlBridge::SigVideoTotalSeconds, ui->CtrlBarWid, &CtrlBar::OnVideoTotalSeconds);
	connect(m_pVideoCtlBridge, &VideoCtlBridge::SigVideoPlaySeconds, ui->CtrlBarWid, &CtrlBar::OnVideoPlaySeconds);
	connect(m_pVideoCtlBridge, &VideoCtlBridge::SigVideoVolume, ui->CtrlBarWid, &CtrlBar::OnVideopVolume);
	connect(m_pVideoCtlBridge, &VideoCtlBridge::SigPauseStat, ui->CtrlBarWid, &CtrlBar::OnPauseStat);
	connect(m_pVideoCtlBridge, &VideoCtlBridge::SigStopFinished, ui->CtrlBarWid, &CtrlBar::OnStopFinished);
	connect(m_pVideoCtlBridge, &VideoCtlBridge::SigStopFinished, ui->ShowWid, &Show::OnStopFinished);
	connect(m_pVideoCtlBridge, &VideoCtlBridge::SigFrameDimensionsChanged, ui->ShowWid, &Show::OnFrameDimensionsChanged);
	connect(m_pVideoCtlBridge, &VideoCtlBridge::SigStopFinished, &m_stTitle, &Title::OnStopFinished);
	connect(m_pVideoCtlBridge, &VideoCtlBridge::SigStartPlay, &m_stTitle, &Title::OnPlay);
	// 播放完成，自动播放下一首
	connect(m_pVideoCtlBridge, &VideoCtlBridge::SigPlayNextOne, &m_stPlaylist, &Playlist::OnForwardPlay);
	//
	connect(m_pVideoCtlBridge, &VideoCtlBridge::SigRandomPlayOne, &m_stPlaylist, &Playlist::OnRandomPlay);

	connect(&m_stCtrlBarAnimationTimer, &QTimer::timeout, this, &MainWid::OnCtrlBarAnimationTimeOut);


	connect(&m_stActFullscreen, &QAction::triggered, this, &MainWid::OnFullScreenPlay);



	return true;
}


void MainWid::keyReleaseEvent(QKeyEvent* event)
{
	// 	    // 是否按下Ctrl键      特殊按键
	//     if(event->modifiers() == Qt::ControlModifier){
	//         // 是否按下M键    普通按键  类似
	//         if(event->key() == Qt::Key_M)
	//             ···
	//     }
	switch (event->key())
	{
	case Qt::Key_Return://全屏
		OnFullScreenPlay();
		break;
	case Qt::Key_Left://后退5s
		emit SigSeekBack();
		break;
	case Qt::Key_Right://前进5s
		emit SigSeekForward();
		break;
	case Qt::Key_Up://增加10音量
		emit SigAddVolume();
		break;
	case Qt::Key_Down://减少10音量
		emit SigSubVolume();
		break;
	case Qt::Key_Space://减少10音量
		emit SigPlayOrPause();
		break;

	default:
		break;
	}
}


void MainWid::mousePressEvent(QMouseEvent* event)
{
	if (event->buttons() & Qt::LeftButton)
	{
		if (ui->TitleWid->geometry().contains(event->pos()))
		{
			m_bMoveDrag = true;
			m_DragPosition = event->globalPos() - this->pos();
		}
	}

	QWidget::mousePressEvent(event);
}

void MainWid::mouseReleaseEvent(QMouseEvent* event)
{
	m_bMoveDrag = false;

	QWidget::mouseReleaseEvent(event);
}

void MainWid::mouseMoveEvent(QMouseEvent* event)
{
	if (m_bMoveDrag)
	{
		move(event->globalPos() - m_DragPosition);
	}

	QWidget::mouseMoveEvent(event);
}

void MainWid::contextMenuEvent(QContextMenuEvent* event)
{
	m_stMenu.exec(event->globalPos());
}

void MainWid::OnFullScreenPlay()
{
	if (m_bFullScreenPlay == false)
	{
		m_bFullScreenPlay = true;
		m_stActFullscreen.setChecked(true);
		//脱离父窗口后才能设置
		ui->ShowWid->setWindowFlags(Qt::Window);
		//多屏情况下，在当前屏幕全屏
		QScreen* pStCurScreen = screen();
		ui->ShowWid->windowHandle()->setScreen(pStCurScreen);

		ui->ShowWid->showFullScreen();


		QRect stScreenRect = pStCurScreen->geometry();
		int nCtrlBarHeight = ui->CtrlBarWid->height();
		int nX = ui->ShowWid->x();
		m_stCtrlBarAnimationShow = QRect(nX, stScreenRect.height() - nCtrlBarHeight, stScreenRect.width(), nCtrlBarHeight);
		m_stCtrlBarAnimationHide = QRect(nX, stScreenRect.height(), stScreenRect.width(), nCtrlBarHeight);

		m_stCtrlbarAnimationShow->setStartValue(m_stCtrlBarAnimationHide);
		m_stCtrlbarAnimationShow->setEndValue(m_stCtrlBarAnimationShow);
		m_stCtrlbarAnimationShow->setDuration(CTRLBAR_ANIMATION_DURATION);

		m_stCtrlbarAnimationHide->setStartValue(m_stCtrlBarAnimationShow);
		m_stCtrlbarAnimationHide->setEndValue(m_stCtrlBarAnimationHide);
		m_stCtrlbarAnimationHide->setDuration(CTRLBAR_ANIMATION_DURATION);

		ui->CtrlBarWid->setWindowFlags(Qt::FramelessWindowHint | Qt::Window);
		ui->CtrlBarWid->windowHandle()->setScreen(pStCurScreen);
		ui->CtrlBarWid->raise();
		ui->CtrlBarWid->setWindowOpacity(0.5);
		ui->CtrlBarWid->showNormal();
		ui->CtrlBarWid->windowHandle()->setScreen(pStCurScreen);

		m_stCtrlbarAnimationShow->start();
		m_bFullscreenCtrlBarShow = true;

		// 安装事件过滤器，使用事件驱动替代定时器轮询
		ui->CtrlBarWid->installEventFilter(this);
		ui->ShowWid->installEventFilter(this);
		QApplication::instance()->installEventFilter(this);

		this->setFocus();
	}
	else
	{
		m_bFullScreenPlay = false;
		m_stActFullscreen.setChecked(false);

		m_stCtrlbarAnimationShow->stop(); //快速切换时，动画还没结束导致控制面板消失
		m_stCtrlbarAnimationHide->stop();
		ui->CtrlBarWid->setWindowOpacity(1);
		ui->CtrlBarWid->setWindowFlags(Qt::SubWindow);

		ui->ShowWid->setWindowFlags(Qt::SubWindow);

		ui->CtrlBarWid->showNormal();
		ui->ShowWid->showNormal();

		// 移除事件过滤器
		ui->CtrlBarWid->removeEventFilter(this);
		ui->ShowWid->removeEventFilter(this);
		QApplication::instance()->removeEventFilter(this);

		this->setFocus();
	}
}

void MainWid::OnCtrlBarAnimationTimeOut()
{
	QApplication::setOverrideCursor(Qt::BlankCursor);
}

// 新：使用事件过滤器处理全屏模式下的鼠标事件（替代定时器轮询）
bool MainWid::eventFilter(QObject* watched, QEvent* event)
{
	if (!m_bFullScreenPlay)
	{
		return QMainWindow::eventFilter(watched, event);
	}

	if (event->type() == QEvent::MouseMove)
	{
		QMouseEvent* mouseEvent = static_cast<QMouseEvent*>(event);
		QPoint globalPos = mouseEvent->globalPos();

		// 检查鼠标是否在控制面板区域
		if (m_stCtrlBarAnimationShow.contains(globalPos))
		{
			// 鼠标在控制栏附近，显示控制栏
			if (!ui->CtrlBarWid->geometry().contains(globalPos))
			{
				// 需要显示控制栏
				ui->CtrlBarWid->raise();
				m_stCtrlbarAnimationShow->start();
				m_stCtrlbarAnimationHide->stop();
				stCtrlBarHideTimer.stop();
				QApplication::restoreOverrideCursor();
			}
			else
			{
				// 鼠标在控制面板上，保持显示
				m_bFullscreenCtrlBarShow = true;
				QApplication::restoreOverrideCursor();
			}
		}
		else
		{
			// 鼠标远离控制栏，准备隐藏
			if (m_bFullscreenCtrlBarShow)
			{
				m_bFullscreenCtrlBarShow = false;
				stCtrlBarHideTimer.singleShot(FULLSCREEN_CTRLBAR_HIDE_DELAY, this, &MainWid::OnCtrlBarHideTimeOut);
			}
		}
	}
	else if (event->type() == QEvent::Leave)
	{
		// 鼠标离开窗口，准备隐藏控制栏
		if (m_bFullscreenCtrlBarShow)
		{
			m_bFullscreenCtrlBarShow = false;
			stCtrlBarHideTimer.singleShot(FULLSCREEN_CTRLBAR_HIDE_DELAY, this, &MainWid::OnCtrlBarHideTimeOut);
		}
	}

	return QMainWindow::eventFilter(watched, event);
}

void MainWid::OnCtrlBarHideTimeOut()
{
	if (m_bFullScreenPlay)
	{
		m_stCtrlbarAnimationHide->start();
	}
}

void MainWid::OnShowMenu()
{
	RefreshRecentFilesMenu();
	m_stMenu.exec(cursor().pos());
}

void MainWid::OnShowAbout()
{
	m_stAboutWidget.move(cursor().pos().x() - m_stAboutWidget.width() / 2, cursor().pos().y() - m_stAboutWidget.height() / 2);
	m_stAboutWidget.show();
}

void MainWid::OpenFile()
{
	QString strFileName = QFileDialog::getOpenFileName(this, "打开文件", QDir::homePath(),
		"视频文件(*.mkv *.rmvb *.mp4 *.avi *.flv *.wmv *.3gp)");

	if (!strFileName.isEmpty())
		emit SigOpenFile(strFileName);
}

void MainWid::OnPlayFile(QString strFileName)
{
	AddRecentFile(strFileName);
	ui->ShowWid->OnPlay(strFileName);
}

void MainWid::OnOpenRecentFile()
{
	QAction* action = qobject_cast<QAction*>(sender());
	if (!action)
		return;

	const QString strFileName = action->data().toString();
	if (strFileName.isEmpty())
		return;

	m_stPlaylist.OnAddFileAndPlay(strFileName);
}

void MainWid::OnClearRecentFiles()
{
	QStringList emptyList;
	GlobalHelper::SaveRecentFiles(emptyList);
	RefreshRecentFilesMenu();
}

void MainWid::OnShowSettingWid()
{
	m_stSettingWid.show();
}

void MainWid::InitMenu()
{
	QString menu_json_file_name = ":/res/menu.json";
	QByteArray ba_json;
	QFile json_file(menu_json_file_name);
	if (json_file.open(QIODevice::ReadOnly))
	{
		ba_json = json_file.readAll();
		json_file.close();
	}

	QJsonDocument json_doc = QJsonDocument::fromJson(ba_json);

	if (json_doc.isObject())
	{
		QJsonObject json_obj = json_doc.object();
		MenuJsonParser(json_obj, &m_stMenu);
	}

	m_pRecentFilesMenu = m_stMenu.addMenu("最近打开");
	RefreshRecentFilesMenu();
}

void MainWid::MenuJsonParser(QJsonObject& json_obj, QMenu* menu)
{
	QJsonObject::iterator it = json_obj.begin();
	QJsonObject::iterator end = json_obj.end();
	while (it != end)
	{
		QString key = it.key();
		auto value = it.value();
		if (value.isObject())
		{
			QMenu* sub_menu = menu->addMenu(key);
			QJsonObject obj = value.toObject();
			MenuJsonParser(obj, sub_menu);
		}
		else
		{
			QString value_str = value.toString();
			QStringList value_info = value_str.split("/");
			if (value_info.size() == 2)
			{
				QString hot_key = value_info[1];
				if (hot_key.length() > 0)
				{
					key = key + "\t" + hot_key;
				}
				QAction* action = menu->addAction(key);
				QString fun_str = value_info[0];
				ConnectMenuAction(action, it.key(), fun_str, hot_key);
			}
		}

		it++;
	}
}

void MainWid::ConnectMenuAction(QAction* action, const QString& actionText, const QString& functionName, const QString& hotKey)
{
	Q_UNUSED(actionText);

	if (!action)
		return;

	if (!hotKey.isEmpty())
		action->setShortcut(QKeySequence(hotKey));

	if (functionName == "OpenFile")
		connect(action, &QAction::triggered, this, &MainWid::OpenFile);
	else if (functionName == "OnCloseBtnClicked")
		connect(action, &QAction::triggered, this, &MainWid::OnCloseBtnClicked);
	else if (hotKey == "F1")
		connect(action, &QAction::triggered, this, &MainWid::OnShowAbout);
	else if (hotKey == "F5")
		connect(action, &QAction::triggered, this, &MainWid::OnShowSettingWid);
	else if (hotKey == "F6")
		connect(action, &QAction::triggered, this, &MainWid::OnShowOrHidePlaylist);
	else if (hotKey == "Enter" || hotKey == "Ctrl+Enter")
		connect(action, &QAction::triggered, this, &MainWid::OnFullScreenPlay);
	else if (hotKey == "Alt+F4")
		connect(action, &QAction::triggered, this, &MainWid::OnCloseBtnClicked);
	else
		action->setEnabled(false);
}

QMenu* MainWid::AddMenuFun(QString menu_title, QMenu* menu)
{
	QMenu* menu_t = new QMenu(this);
	menu_t->setTitle(menu_title);
	menu->addMenu(menu_t);
	return menu_t;
}

void MainWid::AddActionFun(QString action_title, QMenu* menu, void(MainWid::* slot_addr)())
{
	QAction* action = new QAction(this);;
	action->setText(action_title);
	menu->addAction(action);
	connect(action, &QAction::triggered, this, slot_addr);
}

void MainWid::AddRecentFile(const QString& strFileName)
{
	QFileInfo fileInfo(strFileName);
	if (strFileName.isEmpty() || !fileInfo.exists() || !fileInfo.isFile())
		return;

	const QString canonicalPath = fileInfo.canonicalFilePath();
	QStringList recentFiles;
	GlobalHelper::GetRecentFiles(recentFiles);

	recentFiles.removeAll(canonicalPath);
	recentFiles.prepend(canonicalPath);

	while (recentFiles.size() > MAX_RECENT_FILES)
		recentFiles.removeLast();

	GlobalHelper::SaveRecentFiles(recentFiles);
	RefreshRecentFilesMenu();
}

void MainWid::RefreshRecentFilesMenu()
{
	if (!m_pRecentFilesMenu)
		return;

	m_pRecentFilesMenu->clear();

	QStringList recentFiles;
	GlobalHelper::GetRecentFiles(recentFiles);

	QStringList validRecentFiles;
	for (const QString& filePath : recentFiles)
	{
		QFileInfo fileInfo(filePath);
		if (!fileInfo.exists() || !fileInfo.isFile())
			continue;

		validRecentFiles.append(fileInfo.canonicalFilePath());
		QAction* action = m_pRecentFilesMenu->addAction(fileInfo.fileName(), this, &MainWid::OnOpenRecentFile);
		action->setData(fileInfo.canonicalFilePath());
		action->setToolTip(fileInfo.canonicalFilePath());
	}

	if (validRecentFiles != recentFiles)
		GlobalHelper::SaveRecentFiles(validRecentFiles);

	if (!validRecentFiles.isEmpty())
		m_pRecentFilesMenu->addSeparator();

	QAction* clearAction = m_pRecentFilesMenu->addAction("清空最近打开", this, &MainWid::OnClearRecentFiles);
	clearAction->setEnabled(!validRecentFiles.isEmpty());
}

void MainWid::OnCloseBtnClicked()
{
	// 保存窗口状态
	GlobalHelper::SaveWindowState(saveGeometry(), saveState());

	// 保存播放列表
	QStringList playlist;
	m_stPlaylist.GetPlaylist(playlist);
	GlobalHelper::SavePlaylist(playlist);

	// 保存播放设置
	double volume = 1.0;
	int loopPolicy = 0;
	double speed = 1.0;
	// 从控制栏获取当前音量和循环模式
	volume = ui->CtrlBarWid->GetVolume();
	loopPolicy = ui->CtrlBarWid->GetLoopPolicy();
	speed = ui->CtrlBarWid->GetSpeed();
	GlobalHelper::SavePlaySettings(volume, loopPolicy, speed);
	VideoCtl::GetInstance()->OnStopAndWait();
	this->close();
}

void MainWid::OnMinBtnClicked()
{
	this->showMinimized();
}

void MainWid::OnMaxBtnClicked()
{
	if (isMaximized())
	{
		showNormal();
		emit SigShowMax(false);
	}
	else
	{
		showMaximized();
		emit SigShowMax(true);
	}
}

void MainWid::OnShowOrHidePlaylist()
{
	if (ui->PlaylistWid->isHidden())
	{
		ui->PlaylistWid->show();
	}
	else
	{
		ui->PlaylistWid->hide();
	}
}

void MainWid::OnSpeedChanged(double speed)
{
	if (speed < 0)return;
	VideoCtl::GetInstance()->set_play_speed(speed);
}
