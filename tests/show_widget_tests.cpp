#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

namespace {

bool Expect(bool condition, const char* message)
{
    if (!condition)
        std::cerr << "FAILED: " << message << '\n';
    return condition;
}

std::string ReadFile(const char* path)
{
    std::ifstream file(path, std::ios::in | std::ios::binary);
    std::ostringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

} // namespace

int main()
{
    const std::string showCpp = ReadFile("apps/qt_player/show.cpp");
    const std::string mainWidCpp = ReadFile("apps/qt_player/mainwid.cpp");
    const std::string mainWidH = ReadFile("apps/qt_player/mainwid.h");
    const std::string designSystemQss = ReadFile("apps/qt_player/res/qss/design-system.css");
    const std::string rootCMake = ReadFile("CMakeLists.txt");
    const std::string aboutUi = ReadFile("apps/qt_player/about.ui");
    const std::string titleUi = ReadFile("apps/qt_player/title.ui");
    const std::string settingUi = ReadFile("apps/qt_player/settingwid.ui");
    
    const std::string aip = ReadFile("myplayer.aip");
    const std::string notice = ReadFile("NOTICE");
    const std::string releaseChecklist = ReadFile("docs/release-checklist.md");
    const std::string qaChecklist = ReadFile("docs/player-qa-checklist.md");
    const std::string portablePackager = ReadFile("scripts/package-portable.ps1");

    if (!Expect(!showCpp.empty(), "show.cpp should be readable from the repository root"))
        return 1;
    if (!Expect(showCpp.find("ui->label->setUpdatesEnabled(false)") == std::string::npos,
            "video surface should not disable Qt updates before SDL has a frame"))
        return 1;
    if (!Expect(showCpp.find("ui->label->setAutoFillBackground(true)") != std::string::npos,
            "video surface should fill a fallback background before the first frame"))
        return 1;
    if (!Expect(showCpp.find("QPalette::Window") != std::string::npos,
            "video surface should configure an explicit window background color"))
        return 1;
    if (!Expect(mainWidH.find("OpenNetworkStream") != std::string::npos,
            "main window should expose an open network stream action"))
        return 1;
    if (!Expect(mainWidCpp.find("Ctrl+U") != std::string::npos
                && mainWidCpp.find("OpenNetworkStream") != std::string::npos,
            "Ctrl+U menu action should open a network stream"))
        return 1;
    if (!Expect(mainWidCpp.find("m_stPlaylist.OnAddFileAndPlay(location)") != std::string::npos,
            "open network stream should add the URL to the playlist and play it"))
        return 1;
    if (!Expect(mainWidCpp.find("IsNetworkMediaLocation") != std::string::npos,
            "main window playback should split network streams from local file semantics"))
        return 1;
    if (!Expect(mainWidCpp.find("m_playbackController->play(MediaSource{location.toStdString()})") != std::string::npos,
            "network playlist items should play through MediaSource"))
        return 1;
    if (!Expect(designSystemQss.find("QInputDialog") != std::string::npos,
            "global design system should style URL input dialogs"))
        return 1;
    if (!Expect(designSystemQss.find("QInputDialog QLineEdit") != std::string::npos,
            "URL input dialogs should style their line edit"))
        return 1;
    if (!Expect(designSystemQss.find("QInputDialog QPushButton") != std::string::npos,
            "URL input dialogs should style their action buttons"))
        return 1;
    if (!Expect(rootCMake.find("project(myplayer VERSION 1.0.0 LANGUAGES CXX)") != std::string::npos,
            "CMake project should use the internal product name"))
        return 1;
    if (!Expect(rootCMake.find("OUTPUT_NAME_DEBUG \"myplayer_debug\"") != std::string::npos
                && rootCMake.find("OUTPUT_NAME_RELEASE \"myplayer\"") != std::string::npos,
            "Qt executable output should use myplayer names"))
        return 1;
    if (!Expect(rootCMake.find("add_executable(myplayer_imgui") != std::string::npos
                && rootCMake.find("OUTPUT_NAME_RELEASE \"myplayer_imgui\"") != std::string::npos,
            "ImGui executable target should use myplayer names"))
        return 1;
    if (!Expect(aboutUi.find("MyPlayer") != std::string::npos
                && titleUi.find("MyPlayer") != std::string::npos
                && settingUi.find("MyPlayer") != std::string::npos,
            "Qt visible titles should use MyPlayer"))
        return 1;
    if (!Expect(aip.find("Value=\"MyPlayer\"") != std::string::npos
                && aip.find("Value=\"myplayer\"") != std::string::npos,
            "installer metadata should use MyPlayer/myplayer names"))
        return 1;
    if (!Expect(aip.find("https://github.com/dfasdsfffff/playerdemo-master") != std::string::npos,
            "installer product links should point to the maintained MyPlayer repository"))
        return 1;
    if (!Expect(notice.find("MyPlayer") != std::string::npos
                && notice.find("itisyang/playerdemo") != std::string::npos
                && notice.find("https://github.com/dfasdsfffff/playerdemo-master") != std::string::npos
                && notice.find("GNU General Public License") != std::string::npos,
            "NOTICE should preserve product, upstream, and GPL attribution"))
        return 1;
    if (!Expect(releaseChecklist.find("GPL") != std::string::npos
                && releaseChecklist.find("source") != std::string::npos,
            "release checklist should document GPL source distribution"))
        return 1;
    if (!Expect(qaChecklist.find("format compatibility") != std::string::npos
                && qaChecklist.find("long-running playback") != std::string::npos
                && qaChecklist.find("network failure") != std::string::npos,
            "player QA checklist should cover commercial-readiness playback risks"))
        return 1;
    if (!Expect(portablePackager.find("MyPlayer-1.0.0-windows-x64.zip") != std::string::npos,
            "portable packager should produce the expected green package name"))
        return 1;
    if (!Expect(portablePackager.find("windeployqt") != std::string::npos
                && portablePackager.find("platforms") != std::string::npos,
            "portable packager should deploy Qt runtime and platform plugins"))
        return 1;
    if (!Expect(portablePackager.find("LICENSE") != std::string::npos
                && portablePackager.find("NOTICE") != std::string::npos
                && portablePackager.find("THIRD-PARTY-NOTICES.md") != std::string::npos,
            "portable packager should include license and notice files"))
        return 1;

    return 0;
}
