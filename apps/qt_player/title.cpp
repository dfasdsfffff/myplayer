/*
 * @file 	title.cpp
 * @date 	2018/01/22 23:08
 *
 * @author 	itisyang
 * @Contact	itisyang@gmail.com
 *
 * @brief 	播放器标题栏
 * @note
 */

#include <QPainter>
#include <QFileInfo>
#include <QFontMetrics>
#include <QMessageBox>
#include <QFileDialog>

#include "title.h"
#include "player_icons.h"
#include "media_location_privacy.h"
#include "media_format_registry.h"
#include "ui_title.h"

#include "globalhelper.h"

Title::Title(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::Title),
    m_stActionGroup(this),
    m_stMenu(this)
{
    ui->setupUi(this);

    connect(ui->CloseBtn, &QPushButton::clicked, this, &Title::SigCloseBtnClicked);
    connect(ui->MinBtn, &QPushButton::clicked, this, &Title::SigMinBtnClicked);
    connect(ui->MaxBtn, &QPushButton::clicked, this, &Title::SigMaxBtnClicked);
    connect(ui->FullScreenBtn, &QPushButton::clicked, this, &Title::SigFullScreenBtnClicked);
    connect(ui->MenuBtn, &QPushButton::clicked, this, &Title::SigShowMenu);
 
    connect(ui->TitleSettingBtn, &QPushButton::clicked, this, &Title::SigShowSetting);
    ui->TitleSettingBtn->setToolTip("设置");
    m_stMenu.addAction("最大化", this, &Title::SigMaxBtnClicked);
    m_stMenu.addAction("最小化", this, &Title::SigMinBtnClicked);
    m_stMenu.addAction("退出", this, &Title::SigCloseBtnClicked);

    QMenu* stMenu = m_stMenu.addMenu("打开");
    stMenu->addAction("打开文件", this, &Title::OpenFile);

    ui->MenuBtn->setToolTip("显示主菜单");
    ui->MinBtn->setToolTip("最小化");
    ui->MaxBtn->setToolTip("最大化");
    ui->CloseBtn->setToolTip("关闭");
    ui->FullScreenBtn->setToolTip("全屏");
}

Title::~Title()
{
    delete ui;
}

bool Title::Init()
{
    if (InitUi() == false)
    {
        return false;
    }

    return true;
}

bool Title::InitUi()
{
    OnStopFinished();

    //保证窗口不被绘制上的部分透明
    setAttribute(Qt::WA_StyledBackground);
    ui->MovieNameLab->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
    ui->ResolutionBadge->setFixedHeight(19);
    ui->CodecBadge->setFixedHeight(19);

    setStyleSheet(GlobalHelper::GetThemeStr("://res/qss/title.css"));

    PlayerIcons::set(ui->MaxBtn, PlayerIcons::Symbol::Maximize);
    PlayerIcons::set(ui->MinBtn, PlayerIcons::Symbol::Minimize);
    PlayerIcons::set(ui->CloseBtn, PlayerIcons::Symbol::Close);

    PlayerIcons::set(ui->FullScreenBtn, PlayerIcons::Symbol::Fullscreen);
    
    PlayerIcons::set(ui->MenuBtn, PlayerIcons::Symbol::Menu);
    PlayerIcons::set(ui->TitleSettingBtn, PlayerIcons::Symbol::Settings);
    //ui->LogoLab->setToolTip("显示主菜单");

    if (about.Init() == false)
    {
        return false;
    }

    return true;
}

void Title::OpenFile()
{
    QString strFileName = QFileDialog::getOpenFileName(this, "打开文件", QDir::homePath(), 
        MediaOpenDialogFilter());

    emit SigOpenFile(strFileName);
}

void Title::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
}

void Title::mouseDoubleClickEvent(QMouseEvent *event)
{
    if(event->button() == Qt::LeftButton)
    {
        emit SigDoubleClicked();
    }
}

void Title::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    ChangeMovieNameShow();
}

void Title::ChangeMovieNameShow()
{
    const QFontMetrics metrics(ui->MovieNameLab->font());
    const int available = qMin(420, qMax(40, width() - 650));
    ui->MovieNameLab->setMaximumWidth(available);
    ui->MovieNameLab->setText(metrics.elidedText(m_strMovieName, Qt::ElideRight, available));
}

void Title::OnChangeMaxBtnStyle(bool bIfMax)
{
    if (bIfMax)
    {
        PlayerIcons::set(ui->MaxBtn, PlayerIcons::Symbol::Restore);
        ui->MaxBtn->setToolTip("还原");
    }
    else
    {
        PlayerIcons::set(ui->MaxBtn, PlayerIcons::Symbol::Maximize);
        ui->MaxBtn->setToolTip("最大化");
    }
}

void Title::OnPlay(QString strMovieName)
{
    m_strMovieName = strMovieName.contains("://") ? SafeMediaLocationForDisplay(strMovieName) : QFileInfo(strMovieName).fileName();
    ui->MovieNameLab->setToolTip(m_strMovieName);
    ChangeMovieNameShow();
}

void Title::OnStopFinished()
{
    m_strMovieName = QStringLiteral("打开文件开始播放");
    ui->MovieNameLab->setToolTip({});
    ChangeMovieNameShow();
    SetMediaDetails({}, {});
}

void Title::SetMediaDetails(const QString& resolution, const QString& codec)
{
    ui->ResolutionBadge->setText(resolution);
    ui->ResolutionBadge->setVisible(!resolution.isEmpty());
    ui->CodecBadge->setText(codec);
    ui->CodecBadge->setVisible(!codec.isEmpty());
}
