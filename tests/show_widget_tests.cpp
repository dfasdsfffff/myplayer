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

    return 0;
}
