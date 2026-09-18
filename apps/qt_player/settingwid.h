#pragma once

#include "app_preferences.h"

#include <QWidget>
#include "ui_settingwid.h"

class SettingWid : public QWidget
{
    Q_OBJECT

public:
    SettingWid(QWidget *parent = Q_NULLPTR);
    ~SettingWid();
    void SetPreferences(const AppPreferences& preferences);
    AppPreferences Preferences() const;

signals:
    void SigPreferencesApplied(const AppPreferences& preferences);

private slots:
    void Apply();
    void Accept();
    void Reject();

private:
    Ui::SettingWid ui;
};
