#include "subtitle_dispatcher.h"

#include <iostream>

extern "C" {
#include <libavcodec/avcodec.h>
}

namespace {

bool Expect(bool condition, const char* message)
{
    if (!condition)
        std::cerr << "FAILED: " << message << '\n';
    return condition;
}

} // namespace

int main()
{
    AVSubtitle subtitle{};
    subtitle.start_display_time = 100;
    subtitle.end_display_time = 1600;
    subtitle.num_rects = 1;
    subtitle.rects = static_cast<AVSubtitleRect**>(av_calloc(1, sizeof(*subtitle.rects)));
    subtitle.rects[0] = static_cast<AVSubtitleRect*>(av_mallocz(sizeof(*subtitle.rects[0])));
    subtitle.rects[0]->ass = av_strdup("Dialogue: 0,0:00:00.00,0:00:01.50,Default,,0,0,0,,Hello");

    const auto frame = SubtitleDispatcher::Copy(subtitle, 4.0);
    avsubtitle_free(&subtitle);

    if (!Expect(frame && frame->startSeconds == 4.1 && frame->endSeconds == 5.6,
                "display offsets should be normalized against the subtitle PTS") ||
        !Expect(frame->assOrText == "Dialogue: 0,0:00:00.00,0:00:01.50,Default,,0,0,0,,Hello",
                "ASS text should be copied before the FFmpeg subtitle is released") ||
        !Expect(frame->bitmaps.empty(), "text subtitle should not expose bitmap data"))
        return 1;

    AVSubtitle multiAssSubtitle{};
    multiAssSubtitle.num_rects = 2;
    multiAssSubtitle.rects = static_cast<AVSubtitleRect**>(av_calloc(2, sizeof(*multiAssSubtitle.rects)));
    multiAssSubtitle.rects[0] = static_cast<AVSubtitleRect*>(av_mallocz(sizeof(*multiAssSubtitle.rects[0])));
    multiAssSubtitle.rects[1] = static_cast<AVSubtitleRect*>(av_mallocz(sizeof(*multiAssSubtitle.rects[1])));
    multiAssSubtitle.rects[0]->ass = av_strdup("Dialogue: 0,0:00:00.00,0:00:01.00,Default,,0,0,0,,First");
    multiAssSubtitle.rects[1]->ass = av_strdup("Dialogue: 0,0:00:00.00,0:00:01.00,Default,,0,0,0,,Second");

    const auto multiAssFrame = SubtitleDispatcher::Copy(multiAssSubtitle, 0.0);
    avsubtitle_free(&multiAssSubtitle);

    if (!Expect(multiAssFrame &&
                    multiAssFrame->assOrText ==
                        "Dialogue: 0,0:00:00.00,0:00:01.00,Default,,0,0,0,,First\n"
                        "Dialogue: 0,0:00:00.00,0:00:01.00,Default,,0,0,0,,Second",
                "all ASS rectangles should be copied before the FFmpeg subtitle is released"))
        return 1;

    AVSubtitle bitmapSubtitle{};
    bitmapSubtitle.num_rects = 1;
    bitmapSubtitle.rects = static_cast<AVSubtitleRect**>(av_calloc(1, sizeof(*bitmapSubtitle.rects)));
    bitmapSubtitle.rects[0] = static_cast<AVSubtitleRect*>(av_mallocz(sizeof(*bitmapSubtitle.rects[0])));
    AVSubtitleRect* bitmapRect = bitmapSubtitle.rects[0];
    bitmapRect->type = SUBTITLE_BITMAP;
    bitmapRect->x = 3;
    bitmapRect->y = 4;
    bitmapRect->w = 2;
    bitmapRect->h = 1;
    bitmapRect->nb_colors = 2;
    bitmapRect->linesize[0] = 2;
    bitmapRect->data[0] = static_cast<std::uint8_t*>(av_malloc(2));
    bitmapRect->data[0][0] = 0;
    bitmapRect->data[0][1] = 1;
    bitmapRect->data[1] = static_cast<std::uint8_t*>(av_calloc(2, sizeof(std::uint32_t)));
    reinterpret_cast<std::uint32_t*>(bitmapRect->data[1])[0] = 0xaa112233;
    reinterpret_cast<std::uint32_t*>(bitmapRect->data[1])[1] = 0xbb445566;

    const auto bitmapFrame = SubtitleDispatcher::Copy(bitmapSubtitle, 0.0);
    avsubtitle_free(&bitmapSubtitle);

    const std::vector<std::uint8_t> expectedBgra{0x33, 0x22, 0x11, 0xaa, 0x66, 0x55, 0x44, 0xbb};
    if (!Expect(bitmapFrame && bitmapFrame->bitmaps.size() == 1,
                "bitmap subtitles should produce one immutable bitmap") ||
        !Expect(bitmapFrame->bitmaps.front().x == 3 && bitmapFrame->bitmaps.front().y == 4 &&
                    bitmapFrame->bitmaps.front().width == 2 && bitmapFrame->bitmaps.front().height == 1 &&
                    bitmapFrame->bitmaps.front().stride == 8 && bitmapFrame->bitmaps.front().bgra == expectedBgra,
                "palette-indexed bitmap data should become tightly packed BGRA pixels"))
        return 1;

    return 0;
}
