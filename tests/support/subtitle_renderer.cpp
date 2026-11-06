#include "subtitle_renderer.h"

#include "subtitle_frame.h"

#include <SDL.h>
#include <ass/ass.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string_view>
#include <vector>

namespace {

std::string Trim(std::string value)
{
    const auto first = std::find_if_not(value.begin(), value.end(), [](unsigned char character) { return std::isspace(character); });
    const auto last = std::find_if_not(value.rbegin(), value.rend(), [](unsigned char character) { return std::isspace(character); }).base();
    return first < last ? std::string(first, last) : std::string();
}

bool ParseSrtTime(const std::string& value, int& hours, int& minutes, int& seconds, int& milliseconds)
{
    char firstColon = 0;
    char secondColon = 0;
    char comma = 0;
    std::istringstream input(value);
    input >> hours >> firstColon >> minutes >> secondColon >> seconds >> comma >> milliseconds;
    return input && firstColon == ':' && secondColon == ':' && comma == ',';
}

std::string AssTimestamp(const std::string& value)
{
    int hours = 0;
    int minutes = 0;
    int seconds = 0;
    int milliseconds = 0;
    if (!ParseSrtTime(Trim(value), hours, minutes, seconds, milliseconds))
        return {};

    char buffer[16]{};
    std::snprintf(buffer, sizeof(buffer), "%d:%02d:%02d.%02d", hours, minutes, seconds, milliseconds / 10);
    return buffer;
}

ASS_Track* LoadSrtTrack(ASS_Library* library, const std::string& fileName)
{
    std::ifstream input(fileName, std::ios::binary);
    if (!input)
        return nullptr;

    std::ostringstream converted;
    converted << "[Script Info]\nScriptType: v4.00+\nPlayResX: 384\nPlayResY: 288\n\n"
                 "[V4+ Styles]\n"
                 "Format: Name,Fontname,Fontsize,PrimaryColour,SecondaryColour,OutlineColour,BackColour,"
                 "Bold,Italic,Underline,StrikeOut,ScaleX,ScaleY,Spacing,Angle,BorderStyle,Outline,Shadow,"
                 "Alignment,MarginL,MarginR,MarginV,Encoding\n"
                 "Style: Default,Arial,24,&H00FFFFFF,&H000000FF,&H00000000,&H64000000,0,0,0,0,100,100,0,0,1,1,0,2,10,10,10,1\n\n"
                 "[Events]\n"
                 "Format: Layer,Start,End,Style,Name,MarginL,MarginR,MarginV,Effect,Text\n";

    std::string line;
    while (std::getline(input, line)) {
        line = Trim(line);
        if (line.empty())
            continue;

        std::string timing;
        if (!std::getline(input, timing))
            break;
        const std::size_t separator = timing.find("-->");
        if (separator == std::string::npos)
            continue;
        const std::string start = AssTimestamp(timing.substr(0, separator));
        const std::string end = AssTimestamp(timing.substr(separator + 3));
        if (start.empty() || end.empty())
            continue;

        std::string text;
        while (std::getline(input, line) && !Trim(line).empty()) {
            if (!text.empty())
                text += "\\N";
            text += line;
        }
        if (!text.empty())
            converted << "Dialogue: 0," << start << ',' << end << ",Default,,0,0,0,," << text << '\n';
    }

    const std::string data = converted.str();
    ASS_Track* track = ass_new_track(library);
    if (!track)
        return nullptr;
    ass_process_data(track, data.data(), static_cast<int>(data.size()));
    return track;
}

} // namespace

SubtitleRenderer::SubtitleRenderer()
{
    m_library = ass_library_init();
    if (!m_library)
        return;
    ass_set_fonts_dir(m_library, nullptr);
    m_renderer = ass_renderer_init(m_library);
    if (m_renderer)
        ass_set_fonts(m_renderer, nullptr, "Arial", 1, nullptr, 1);
}

SubtitleRenderer::~SubtitleRenderer()
{
    clearExternalFile();
    if (m_embeddedTrack)
        ass_free_track(m_embeddedTrack);
    if (m_renderer)
        ass_renderer_done(m_renderer);
    if (m_library)
        ass_library_done(m_library);
}

void SubtitleRenderer::setFrame(std::shared_ptr<const SubtitleFrame> frame)
{
    m_frame = std::move(frame);
    if (m_embeddedTrack) {
        ass_free_track(m_embeddedTrack);
        m_embeddedTrack = nullptr;
    }
    m_embeddedCache = {};
    if (m_frame && !m_frame->assOrText.empty() && m_library) {
        m_embeddedTrack = ass_new_track(m_library);
        ass_process_data(m_embeddedTrack, m_frame->assOrText.data(), static_cast<int>(m_frame->assOrText.size()));
    }
}

void SubtitleRenderer::clear()
{
    setFrame({});
    clearExternalFile();
}

bool SubtitleRenderer::loadExternalFile(const std::string& fileName)
{
    if (!m_library || fileName.empty())
        return false;

    std::string extension = std::filesystem::path(fileName).extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    ASS_Track* track = extension == ".srt" ? LoadSrtTrack(m_library, fileName)
                                            : ass_read_file(m_library, fileName.c_str(), nullptr);
    if (!track)
        return false;

    clearExternalFile();
    m_externalTrack = track;
    return true;
}

void SubtitleRenderer::clearExternalFile()
{
    if (m_externalTrack) {
        ass_free_track(m_externalTrack);
        m_externalTrack = nullptr;
    }
    m_externalCache = {};
}

bool SubtitleRenderer::hasExternalSubtitle() const
{
    return m_externalTrack != nullptr;
}

void SubtitleRenderer::render(SDL_Renderer* renderer, const QSize& videoSize, double clockSeconds)
{
    if (!renderer || videoSize.isEmpty())
        return;
    if (m_frame && clockSeconds >= m_frame->startSeconds && clockSeconds <= m_frame->endSeconds) {
        renderBitmaps(renderer, videoSize);
        renderAssTrack(renderer, m_embeddedTrack, videoSize, clockSeconds, m_embeddedCache);
    }
    renderAssTrack(renderer, m_externalTrack, videoSize, clockSeconds, m_externalCache);
}

void SubtitleRenderer::renderBitmaps(SDL_Renderer* renderer, const QSize& videoSize)
{
    for (const SubtitleBitmap& bitmap : m_frame->bitmaps) {
        if (bitmap.width <= 0 || bitmap.height <= 0 || bitmap.stride < bitmap.width * 4 ||
            bitmap.bgra.size() < static_cast<std::size_t>(bitmap.stride) * bitmap.height)
            continue;

        SDL_Texture* texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_BGRA32, SDL_TEXTUREACCESS_STREAMING,
            bitmap.width, bitmap.height);
        if (!texture)
            continue;
        SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
        SDL_UpdateTexture(texture, nullptr, bitmap.bgra.data(), bitmap.stride);
        const SDL_Rect destination{std::clamp(bitmap.x, 0, videoSize.width()),
            std::clamp(bitmap.y, 0, videoSize.height()),
            std::min(bitmap.width, std::max(0, videoSize.width() - bitmap.x)),
            std::min(bitmap.height, std::max(0, videoSize.height() - bitmap.y))};
        if (destination.w > 0 && destination.h > 0)
            SDL_RenderCopy(renderer, texture, nullptr, &destination);
        SDL_DestroyTexture(texture);
    }
}

void SubtitleRenderer::renderAssTrack(SDL_Renderer* renderer, ASS_Track* track, const QSize& videoSize,
    double clockSeconds, RenderCache& cache)
{
    if (!m_renderer || !track)
        return;
    constexpr long long kTimeBucketMs = 33;
    const long long timeBucket = static_cast<long long>(clockSeconds * 1000.0) / kTimeBucketMs;
    if (cache.track != track || cache.viewportWidth != videoSize.width() || cache.viewportHeight != videoSize.height() ||
        cache.timeBucket != timeBucket) {
        cache.track = track;
        cache.viewportWidth = videoSize.width();
        cache.viewportHeight = videoSize.height();
        cache.timeBucket = timeBucket;
        cache.images.clear();

        ass_set_frame_size(m_renderer, videoSize.width(), videoSize.height());
        int changed = 0;
        ASS_Image* image = ass_render_frame(m_renderer, track, static_cast<long long>(clockSeconds * 1000.0), &changed);
        Q_UNUSED(changed);
        for (; image; image = image->next) {
            if (image->w <= 0 || image->h <= 0 || !image->bitmap)
                continue;
            CachedImage cached;
            cached.x = image->dst_x;
            cached.y = image->dst_y;
            cached.width = image->w;
            cached.height = image->h;
            cached.bgra.resize(static_cast<std::size_t>(image->w) * image->h * 4);
            const std::uint8_t red = static_cast<std::uint8_t>(image->color >> 24);
            const std::uint8_t green = static_cast<std::uint8_t>(image->color >> 16);
            const std::uint8_t blue = static_cast<std::uint8_t>(image->color >> 8);
            const std::uint8_t opacity = static_cast<std::uint8_t>(255 - (image->color & 0xff));
            for (int y = 0; y < image->h; ++y) {
                for (int x = 0; x < image->w; ++x) {
                    const std::uint8_t alpha = static_cast<std::uint8_t>(image->bitmap[y * image->stride + x] * opacity / 255);
                    auto* pixel = cached.bgra.data() + (static_cast<std::size_t>(y) * image->w + x) * 4;
                    pixel[0] = blue; pixel[1] = green; pixel[2] = red; pixel[3] = alpha;
                }
            }
            cache.images.push_back(std::move(cached));
        }
    }

    for (const CachedImage& image : cache.images) {
        SDL_Texture* texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_BGRA32, SDL_TEXTUREACCESS_STREAMING,
            image.width, image.height);
        if (!texture)
            continue;
        SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
        SDL_UpdateTexture(texture, nullptr, image.bgra.data(), image.width * 4);
        const SDL_Rect destination{std::clamp(image.x, 0, videoSize.width()),
            std::clamp(image.y, 0, videoSize.height()),
            std::min(image.width, std::max(0, videoSize.width() - image.x)),
            std::min(image.height, std::max(0, videoSize.height() - image.y))};
        if (destination.w > 0 && destination.h > 0)
            SDL_RenderCopy(renderer, texture, nullptr, &destination);
        SDL_DestroyTexture(texture);
    }
}
