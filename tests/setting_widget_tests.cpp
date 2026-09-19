#include "settingwid.h"

#include <QApplication>
#include <QDoubleSpinBox>
#include <QComboBox>
#include <QPushButton>

#include <iostream>

namespace {

bool Expect(bool condition, const char* message)
{
    if (!condition)
        std::cerr << "FAILED: " << message << '\n';
    return condition;
}

} // namespace

int main(int argc, char* argv[])
{
    QApplication application(argc, argv);
    SettingWid widget;
    int appliedCount = 0;
    AppPreferences applied;
    QObject::connect(&widget, &SettingWid::SigPreferencesApplied, [&] (const AppPreferences& value) {
        ++appliedCount;
        applied = value;
    });

    auto* volume = widget.findChild<QDoubleSpinBox*>("volumeSpinBox");
    auto* apply = widget.findChild<QPushButton*>("applyButton");
    auto* ok = widget.findChild<QPushButton*>("okButton");
    auto* cancel = widget.findChild<QPushButton*>("cancelButton");
    auto* hardwareDecode = widget.findChild<QComboBox*>("hardwareDecodeComboBox");
    if (!Expect(volume && apply && ok && cancel && hardwareDecode, "settings window exposes playback controls and actions"))
        return 1;

    volume->setValue(2.0);
    hardwareDecode->setCurrentIndex(static_cast<int>(HardwareDecodePreference::Disabled));
    apply->click();
    if (!Expect(appliedCount == 1 && applied.volume == 1.0 &&
                applied.hardwareDecode == HardwareDecodePreference::Disabled,
                "Apply emits sanitized preferences including hardware decode mode"))
        return 1;
    if (!Expect(widget.isVisible() == false, "Apply does not force the settings window open"))
        return 1;

    widget.show();
    ok->click();
    if (!Expect(appliedCount == 2 && !widget.isVisible(), "OK applies preferences and closes"))
        return 1;

    widget.show();
    cancel->click();
    if (!Expect(appliedCount == 2 && !widget.isVisible(), "Cancel closes without applying"))
        return 1;

    return 0;
}
