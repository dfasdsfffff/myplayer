#pragma once

#include <QSize>

#include <ass/ass.h>

#include <memory>
#include <string>
#include <vector>
#include <cstdint>

struct SDL_Renderer;
struct SubtitleFrame;

class SubtitleRenderer final {
public:
    SubtitleRenderer();
    ~SubtitleRenderer();

    SubtitleRenderer(const SubtitleRenderer&) = delete;
    SubtitleRenderer& operator=(const SubtitleRenderer&) = delete;

    void setFrame(std::shared_ptr<const SubtitleFrame> frame);
    void clear();
    bool loadExternalFile(const std::string& fileName);
    void clearExternalFile();
    bool hasExternalSubtitle() const;
    void render(SDL_Renderer* renderer, const QSize& videoSize, double clockSeconds);

private:
    struct CachedImage {
        int x{0};
        int y{0};
        int width{0};
        int height{0};
        std::vector<std::uint8_t> bgra;
    };

    struct RenderCache {
        ASS_Track* track{nullptr};
        int viewportWidth{0};
        int viewportHeight{0};
        long long timeBucket{-1};
        std::vector<CachedImage> images;
    };

    void renderBitmaps(SDL_Renderer* renderer, const QSize& videoSize);
    void renderAssTrack(SDL_Renderer* renderer, ASS_Track* track, const QSize& videoSize, double clockSeconds,
        RenderCache& cache);

    std::shared_ptr<const SubtitleFrame> m_frame;
    ASS_Library* m_library = nullptr;
    ASS_Renderer* m_renderer = nullptr;
    ASS_Track* m_embeddedTrack = nullptr;
    ASS_Track* m_externalTrack = nullptr;
    RenderCache m_embeddedCache;
    RenderCache m_externalCache;
};
