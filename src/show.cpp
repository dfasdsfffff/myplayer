/*
 * @file 	show.cpp
 * @date 	2018/01/22 23:07
 *
 * @author 	itisyang
 * @Contact	itisyang@gmail.com
 *
 * @brief 	显示控件
 * @note
 */

#include <QDebug>
#include <QPixmap>
#include <mutex>

#include "show.h"
#include "ui_show.h"

#include "globalhelper.h"
#include "videoctl.h"

std::mutex g_show_rect_mutex;

Show::Show(QWidget *parent) : QWidget(parent),
                              ui(new Ui::Show),
                              m_stActionGroup(this),
                              m_stMenu(this)
{
    ui->setupUi(this);

    // 加载样式（使用统一设计系统）
    setStyleSheet(GlobalHelper::GetThemeStr("://res/qss/show.css"));
    setAcceptDrops(true);

    // 防止过度刷新显示
    this->setAttribute(Qt::WA_OpaquePaintEvent);
    // ui->label->setAttribute(Qt::WA_OpaquePaintEvent);

    ui->label->setUpdatesEnabled(true);

    this->setMouseTracking(true);

    m_nLastFrameWidth = 0; ///< 记录视频宽高
    m_nLastFrameHeight = 0;

    m_stActionGroup.addAction("全屏");
    m_stActionGroup.addAction("暂停");
    m_stActionGroup.addAction("停止");

    m_stMenu.addActions(m_stActionGroup.actions());
}

Show::~Show()
{
    delete ui;
}

bool Show::Init()
{
    if (ConnectSignalSlots() == false)
    {
        return false;
    }

    // ui->label->setUpdatesEnabled(false);

    return true;
}

void Show::OnFrameDimensionsChanged(int nFrameWidth, int nFrameHeight)
{
    m_nLastFrameWidth = nFrameWidth;
    m_nLastFrameHeight = nFrameHeight;

    ChangeShow();
}

void Show::OnVideoFrame(const QImage& image)
{
    if (image.isNull())
        return;

    ui->label->setPixmap(QPixmap::fromImage(image).scaled(
        ui->label->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
}

void Show::ChangeShow()
{
    std::lock_guard<std::mutex> locker(g_show_rect_mutex);

    if (m_nLastFrameWidth == 0 && m_nLastFrameHeight == 0)
    {
        ui->label->setGeometry(0, 0, width(), height());
    }
    else
    {
        float aspect_ratio;
        int width, height, x, y;
        int scr_width = this->width();
        int scr_height = this->height();

        aspect_ratio = (float)m_nLastFrameWidth / (float)m_nLastFrameHeight;

        height = scr_height;
        width = lrint(height * aspect_ratio) & ~1;
        if (width > scr_width)
        {
            width = scr_width;
            height = lrint(width / aspect_ratio) & ~1;
        }
        x = (scr_width - width) / 2;
        y = (scr_height - height) / 2;

        ui->label->setGeometry(x, y, width, height);
    }
}

void Show::dragEnterEvent(QDragEnterEvent *event)
{
    //    if(event->mimeData()->hasFormat("text/uri-list"))
    //    {
    //        event->acceptProposedAction();
    //    }
    event->acceptProposedAction();
}

void Show::resizeEvent(QResizeEvent *event)
{
    Q_UNUSED(event);

    ChangeShow();
}

void Show::keyReleaseEvent(QKeyEvent *event)
{
    switch (event->key())
    {
    case Qt::Key_Return: // 全屏
        SigFullScreen();
        break;
    case Qt::Key_Left: // 后退5s
        emit SigSeekBack();
        break;
    case Qt::Key_Right: // 前进5s
        emit SigSeekForward();
        break;
    case Qt::Key_Up: // 增加10音量
        emit SigAddVolume();
        break;
    case Qt::Key_Down: // 减少10音量
        emit SigSubVolume();
        break;
    case Qt::Key_Space: // 减少10音量
        emit SigPlayOrPause();
        break;

    default:
        QWidget::keyPressEvent(event);
        break;
    }
}

// void Show::contextMenuEvent(QContextMenuEvent* event)
// {
//     //m_stMenu.exec(event->globalPos());
//     qDebug() << "Show::contextMenuEvent";
// }
void Show::mousePressEvent(QMouseEvent *event)
{
    if (event->buttons() & Qt::RightButton)
    {
        emit SigShowMenu();
    }

    QWidget::mousePressEvent(event);
}

void Show::OnDisplayMsg(QString strMsg)
{
    Q_UNUSED(strMsg);
}

void Show::OnPlay(QString strFile)
{
    if (strFile.isEmpty()) {
        qWarning() << "Warning: strFile is empty, not starting playback.";
        return;
    }
    
    // 使用UTF-8编码转换，更适合Windows下的中文路径
    std::string s = strFile.toUtf8().constData();
    
    // 再次检查转换后的字符串是否为空
    if (s.empty()) {
        qWarning() << "Warning: Converted std::string is empty, not starting playback.";
        return;
    }
    
    VideoCtl::GetInstance()->StartPlay(s);
}

void Show::OnStopFinished()
{
    update();
}

void Show::OnTimerShowCursorUpdate()
{
    // qDebug() << "Show::OnTimerShowCursorUpdate()";
    // setCursor(Qt::BlankCursor);
}

void Show::OnActionsTriggered(QAction *action)
{
    QString strAction = action->text();
    if (strAction == "全屏")
    {
        emit SigFullScreen();
    }
    else if (strAction == "停止")
    {
        emit SigStop();
    }
    else if (strAction == "暂停" || strAction == "播放")
    {
        emit SigPlayOrPause();
    }
}

bool Show::ConnectSignalSlots()
{
    QList<bool> listRet;
    bool bRet;

    bRet = connect(this, &Show::SigPlay, this, &Show::OnPlay);
    listRet.append(bRet);

    timerShowCursor.setInterval(2000);
    bRet = connect(&timerShowCursor, &QTimer::timeout, this, &Show::OnTimerShowCursorUpdate);
    listRet.append(bRet);

    connect(&m_stActionGroup, &QActionGroup::triggered, this, &Show::OnActionsTriggered);

    for (bool bReturn : listRet)
    {
        if (bReturn == false)
        {
            return false;
        }
    }

    return true;
}

void Show::dropEvent(QDropEvent *event)
{
    QList<QUrl> urls = event->mimeData()->urls();
    if (urls.isEmpty())
    {
        return;
    }

    for (QUrl url : urls)
    {
        QString strFileName = url.toLocalFile();
        emit SigOpenFile(strFileName);
        break;
    }

    // emit SigPlay(urls.first().toLocalFile());
}
