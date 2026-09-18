#include "subtitle_dispatcher.h"

extern "C" {
#include <libavcodec/avcodec.h>
}

#include <algorithm>

std::shared_ptr<const SubtitleFrame> SubtitleDispatcher::Copy(const AVSubtitle& subtitle, double ptsSeconds)
{
    auto frame = std::make_shared<SubtitleFrame>();
    frame->startSeconds = ptsSeconds + static_cast<double>(subtitle.start_display_time) / 1000.0;
    frame->endSeconds = ptsSeconds + static_cast<double>(subtitle.end_display_time) / 1000.0;
    frame->endSeconds = std::max(frame->endSeconds, frame->startSeconds);

    for (unsigned int index = 0; index < subtitle.num_rects; ++index) {
        const AVSubtitleRect* rect = subtitle.rects[index];
        if (!rect)
            continue;
        if (rect->type == SUBTITLE_BITMAP && rect->data[0] && rect->data[1] && rect->w > 0 && rect->h > 0 &&
            rect->linesize[0] >= rect->w && rect->nb_colors > 0) {
            SubtitleBitmap bitmap;
            bitmap.x = rect->x;
            bitmap.y = rect->y;
            bitmap.width = rect->w;
            bitmap.height = rect->h;
            bitmap.stride = rect->w * 4;
            bitmap.bgra.resize(static_cast<std::size_t>(bitmap.stride) * bitmap.height);

            const auto* palette = reinterpret_cast<const std::uint32_t*>(rect->data[1]);
            for (int y = 0; y < rect->h; ++y) {
                const std::uint8_t* source = rect->data[0] + static_cast<std::size_t>(y) * rect->linesize[0];
                std::uint8_t* destination = bitmap.bgra.data() + static_cast<std::size_t>(y) * bitmap.stride;
                for (int x = 0; x < rect->w; ++x) {
                    const unsigned int paletteIndex = source[x];
                    const std::uint32_t color = paletteIndex < static_cast<unsigned int>(rect->nb_colors)
                        ? palette[paletteIndex]
                        : 0;
                    destination[x * 4] = static_cast<std::uint8_t>(color);
                    destination[x * 4 + 1] = static_cast<std::uint8_t>(color >> 8);
                    destination[x * 4 + 2] = static_cast<std::uint8_t>(color >> 16);
                    destination[x * 4 + 3] = static_cast<std::uint8_t>(color >> 24);
                }
            }
            frame->bitmaps.push_back(std::move(bitmap));
        }
        const char* text = rect->ass && *rect->ass ? rect->ass : rect->text;
        if (text && *text) {
            if (!frame->assOrText.empty())
                frame->assOrText.push_back('\n');
            frame->assOrText.append(text);
        }
    }

    return frame;
}
