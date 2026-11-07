#define SDL_MAIN_HANDLED
#include "core_video_widget.h"
#include <QApplication>
#include <exception>
#include <iostream>

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    try {
        CoreVideoWidget preview;
        if (preview.open(QStringLiteral("missing.mp4"), true)) return 1;
    } catch (const std::exception& error) {
        std::cerr << "Preview construction threw: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
