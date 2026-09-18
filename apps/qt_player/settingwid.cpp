#include "settingwid.h"

#include <QSignalBlocker>

SettingWid::SettingWid(QWidget *parent)
    : QWidget(parent)
{
    ui.setupUi(this);
    connect(ui.applyButton, &QPushButton::clicked, this, &SettingWid::Apply);
    connect(ui.okButton, &QPushButton::clicked, this, &SettingWid::Accept);
    connect(ui.cancelButton, &QPushButton::clicked, this, &SettingWid::Reject);
    SetPreferences(AppPreferences{});
}

SettingWid::~SettingWid()
{
}

void SettingWid::SetPreferences(const AppPreferences& preferences)
{
    const AppPreferences sanitized = SanitizePreferences(preferences);
    const QSignalBlocker volumeBlocker(ui.volumeSpinBox);
    ui.volumeSpinBox->setValue(sanitized.volume);
    ui.speedSpinBox->setValue(sanitized.speed);
    ui.loopPolicyComboBox->setCurrentIndex(static_cast<int>(sanitized.loopPolicy));
    ui.resumePlaybackCheckBox->setChecked(sanitized.resumePlayback);
    ui.reconnectAttemptsSpinBox->setValue(sanitized.reconnectAttempts);
    ui.connectTimeoutSpinBox->setValue(sanitized.connectTimeoutMs);
    ui.readTimeoutSpinBox->setValue(sanitized.readTimeoutMs);
    ui.rtspTransportComboBox->setCurrentIndex(static_cast<int>(sanitized.rtspTransport));
}

AppPreferences SettingWid::Preferences() const
{
    AppPreferences preferences;
    preferences.volume = ui.volumeSpinBox->value();
    preferences.speed = ui.speedSpinBox->value();
    preferences.loopPolicy = static_cast<VideoLoopPolicy>(ui.loopPolicyComboBox->currentIndex());
    preferences.resumePlayback = ui.resumePlaybackCheckBox->isChecked();
    preferences.reconnectAttempts = ui.reconnectAttemptsSpinBox->value();
    preferences.connectTimeoutMs = ui.connectTimeoutSpinBox->value();
    preferences.readTimeoutMs = ui.readTimeoutSpinBox->value();
    preferences.rtspTransport = static_cast<RtspTransport>(ui.rtspTransportComboBox->currentIndex());
    return SanitizePreferences(preferences);
}

void SettingWid::Apply()
{
    emit SigPreferencesApplied(Preferences());
}

void SettingWid::Accept()
{
    Apply();
    close();
}

void SettingWid::Reject()
{
    close();
}
