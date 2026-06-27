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

    return 0;
}
