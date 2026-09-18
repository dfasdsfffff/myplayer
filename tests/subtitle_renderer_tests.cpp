#include "subtitle_frame.h"
#include "subtitle_renderer.h"

#define SDL_MAIN_HANDLED
#include <SDL.h>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>

namespace {

bool Expect(bool condition, const char* message)
{
    if (!condition)
        std::cerr << "FAILED: " << message << '\n';
    return condition;
}

std::filesystem::path WriteAssFixture()
{
    const std::filesystem::path path = std::filesystem::temp_directory_path() / "myplayer-subtitle-renderer-test.ass";
    std::ofstream output(path, std::ios::binary);
    output << "[Script Info]\nScriptType: v4.00+\n\n"
              "[V4+ Styles]\n"
              "Format: Name,Fontname,Fontsize,PrimaryColour,SecondaryColour,OutlineColour,BackColour,"
              "Bold,Italic,Underline,StrikeOut,ScaleX,ScaleY,Spacing,Angle,BorderStyle,Outline,Shadow,"
              "Alignment,MarginL,MarginR,MarginV,Encoding\n"
              "Style: Default,Arial,24,&H00FFFFFF,&H000000FF,&H00000000,&H64000000,0,0,0,0,100,100,0,0,1,1,0,7,10,10,10,1\n\n"
              "[Events]\n"
              "Format: Layer,Start,End,Style,Name,MarginL,MarginR,MarginV,Effect,Text\n"
              "Dialogue: 0,0:00:01.00,0:00:03.00,Default,,0,0,0,,External subtitle\n";
    return path;
}

std::filesystem::path WriteSrtFixture()
{
    const std::filesystem::path path = std::filesystem::temp_directory_path() / "myplayer-subtitle-renderer-test.srt";
    std::ofstream output(path, std::ios::binary);
    output << "1\n00:00:01,000 --> 00:00:03,000\nExternal subtitle\n\n";
    return path;
}

bool HasVisiblePixel(SDL_Renderer* renderer, int width, int height)
{
    std::vector<std::uint32_t> pixels(static_cast<std::size_t>(width) * height);
    if (SDL_RenderReadPixels(renderer, nullptr, SDL_PIXELFORMAT_ARGB8888, pixels.data(), width * 4) != 0)
        return false;
    for (const std::uint32_t pixel : pixels) {
        if ((pixel & 0x00ffffffU) != 0)
            return true;
    }
    return false;
}

} // namespace

int main()
{
    constexpr int width = 320;
    constexpr int height = 180;
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        std::cerr << "FAILED: SDL video initialization: " << SDL_GetError() << '\n';
        return 1;
    }

    SDL_Surface* surface = SDL_CreateRGBSurfaceWithFormat(0, width, height, 32, SDL_PIXELFORMAT_ARGB8888);
    SDL_Renderer* sdlRenderer = surface ? SDL_CreateSoftwareRenderer(surface) : nullptr;
    if (!Expect(sdlRenderer != nullptr, "software renderer should be available")) {
        SDL_FreeSurface(surface);
        SDL_Quit();
        return 1;
    }

    SubtitleRenderer renderer;
    const std::filesystem::path externalFile = WriteAssFixture();
    if (!Expect(renderer.loadExternalFile(externalFile.string()), "external ASS file should load into its own track") ||
        !Expect(renderer.hasExternalSubtitle(), "external track should be observable after loading"))
        return 1;

    SDL_SetRenderDrawColor(sdlRenderer, 0, 0, 0, 255);
    SDL_RenderClear(sdlRenderer);
    renderer.render(sdlRenderer, QSize(width, height), 0.5);
    if (!Expect(!HasVisiblePixel(sdlRenderer, width, height), "external cue should not render before its start time"))
        return 1;

    SDL_SetRenderDrawColor(sdlRenderer, 0, 0, 0, 255);
    SDL_RenderClear(sdlRenderer);
    renderer.render(sdlRenderer, QSize(width, height), 1.5);
    if (!Expect(HasVisiblePixel(sdlRenderer, width, height), "external cue should render during its active interval"))
        return 1;

    auto embedded = std::make_shared<SubtitleFrame>();
    embedded->startSeconds = 0.0;
    embedded->endSeconds = 10.0;
    embedded->bitmaps.push_back({20, 20, 1, 1, 4, {0, 0, 255, 255}});
    renderer.setFrame(embedded);
    renderer.clearExternalFile();
    if (!Expect(!renderer.hasExternalSubtitle(), "external track should unload independently"))
        return 1;

    SDL_SetRenderDrawColor(sdlRenderer, 0, 0, 0, 255);
    SDL_RenderClear(sdlRenderer);
    renderer.render(sdlRenderer, QSize(width, height), 1.5);
    if (!Expect(HasVisiblePixel(sdlRenderer, width, height), "unloading external subtitles must preserve the embedded bitmap cue"))
        return 1;

    const std::filesystem::path srtFile = WriteSrtFixture();
    renderer.clear();
    if (!Expect(renderer.loadExternalFile(srtFile.string()), "external SRT file should load into its own track"))
        return 1;
    SDL_SetRenderDrawColor(sdlRenderer, 0, 0, 0, 255);
    SDL_RenderClear(sdlRenderer);
    renderer.render(sdlRenderer, QSize(width, height), 1.5);
    if (!Expect(HasVisiblePixel(sdlRenderer, width, height), "external SRT cue should render during its active interval"))
        return 1;

    std::error_code error;
    std::filesystem::remove(externalFile, error);
    std::filesystem::remove(srtFile, error);
    SDL_DestroyRenderer(sdlRenderer);
    SDL_FreeSurface(surface);
    SDL_Quit();
    return 0;
}
