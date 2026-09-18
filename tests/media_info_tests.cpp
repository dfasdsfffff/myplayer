#include "network_input.h"

#include <iostream>

extern "C" {
#include <libavformat/avformat.h>
}

namespace {
bool Expect(bool value, const char* message)
{
    if (!value)
        std::cerr << "FAILED: " << message << '\n';
    return value;
}
}

int main()
{
    AVFormatContext* format = avformat_alloc_context();
    if (!Expect(format != nullptr, "format context should allocate"))
        return 1;
    format->duration = 42000000;

    AVStream* video = avformat_new_stream(format, nullptr);
    AVStream* audio = avformat_new_stream(format, nullptr);
    AVStream* subtitle = avformat_new_stream(format, nullptr);
    video->codecpar->codec_type = AVMEDIA_TYPE_VIDEO;
    video->codecpar->width = 1920;
    video->codecpar->height = 1080;
    video->sample_aspect_ratio = {4, 3};
    audio->codecpar->codec_type = AVMEDIA_TYPE_AUDIO;
    audio->codecpar->codec_id = AV_CODEC_ID_AAC;
    audio->disposition = AV_DISPOSITION_DEFAULT;
    av_dict_set(&audio->metadata, "language", "eng", 0);
    av_dict_set(&audio->metadata, "title", "English", 0);
    subtitle->codecpar->codec_type = AVMEDIA_TYPE_SUBTITLE;
    subtitle->codecpar->codec_id = AV_CODEC_ID_ASS;
    subtitle->disposition = AV_DISPOSITION_FORCED;
    av_dict_set(&subtitle->metadata, "language", "zho", 0);

    const int audioIndex = audio->index;
    const int subtitleIndex = subtitle->index;

    const MediaInfo info = BuildMediaInfo(MediaSource{"movie.mkv"}, format);
    avformat_free_context(format);

    if (!Expect(info.width == 1920 && info.height == 1080, "video dimensions should be copied") ||
        !Expect(info.sampleAspectRatio.num == 4 && info.sampleAspectRatio.den == 3, "SAR should be copied") ||
        !Expect(info.tracks.size() == 2, "audio and subtitle tracks should be listed") ||
        !Expect(info.tracks[0].streamIndex == audioIndex && info.tracks[0].language == "eng" &&
                    info.tracks[0].title == "English" && info.tracks[0].isDefault, "audio metadata should normalize") ||
        !Expect(info.tracks[1].streamIndex == subtitleIndex && info.tracks[1].language == "zho" &&
                    info.tracks[1].isForced, "subtitle metadata should normalize"))
        return 1;
    return 0;
}
