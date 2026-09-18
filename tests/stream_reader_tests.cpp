#define SDL_MAIN_HANDLED

#include "stream_reader.h"

#include "av_constants.h"
#include "packet_queue.h"
#include "video_state.h"

#include <cerrno>
#include <iostream>
#include <memory>
#include <stdexcept>

namespace {

bool Expect(bool condition, const char* message)
{
    if (!condition)
        std::cerr << "FAILED: " << message << '\n';
    return condition;
}

struct AvFormatContextDeleter {
    void operator()(AVFormatContext* context) const
    {
        if (!context)
            return;
        auto* inputFormat = const_cast<AVInputFormat*>(context->iformat);
        if (inputFormat)
            av_freep(&inputFormat);
        if (context->pb)
            avio_context_free(&context->pb);
        av_freep(&context->url);
        avformat_free_context(context);
    }
};

struct AvStreamDeleter {
    void operator()(AVStream* stream) const
    {
        if (!stream)
            return;
        avcodec_parameters_free(&stream->codecpar);
        av_freep(&stream);
    }
};

StreamReaderCallbacks RequiredCallbacks()
{
    return {
        [](VideoState*, int) { return 0; },
        [](const PlaybackStatus&) {},
        [](int) {},
        [](const MediaInfo&) {},
        []() {},
        []() {},
    };
}

std::unique_ptr<AVStream, AvStreamDeleter> MakeStream()
{
    std::unique_ptr<AVStream, AvStreamDeleter> stream(
        static_cast<AVStream*>(av_mallocz(sizeof(AVStream))));
    if (!stream)
        return nullptr;
    stream->codecpar = avcodec_parameters_alloc();
    if (!stream->codecpar)
        return nullptr;
    stream->time_base = {1, 1000};
    return stream;
}

std::unique_ptr<AVFormatContext, AvFormatContextDeleter> MakeFormatContext(
    const char* formatName,
    const char* url,
    bool withIoContext = false)
{
    std::unique_ptr<AVFormatContext, AvFormatContextDeleter> context(avformat_alloc_context());
    if (!context)
        return nullptr;
    context->iformat = static_cast<AVInputFormat*>(av_mallocz(sizeof(AVInputFormat)));
    if (!context->iformat)
        return nullptr;
    const_cast<AVInputFormat*>(context->iformat)->name = formatName;
    context->url = av_strdup(url);
    if (!context->url)
        return nullptr;
    if (withIoContext) {
        constexpr int bufferSize = 4096;
        auto* buffer = static_cast<unsigned char*>(av_malloc(bufferSize));
        if (!buffer)
            return nullptr;
        context->pb = avio_alloc_context(buffer, bufferSize, 0, nullptr, nullptr, nullptr, nullptr);
        if (!context->pb) {
            av_freep(&buffer);
            return nullptr;
        }
    }
    return context;
}

} // namespace

int main()
{
    {
        PacketQueue queue;
        if (!Expect(queue.init() == 0, "packet queue should initialize"))
            return 1;
        if (!Expect(StreamReader::HasEnoughPackets(nullptr, -1, &queue),
                "absent stream has enough packets"))
            return 1;
        queue.abort();
        auto stream = MakeStream();
        if (!Expect(stream != nullptr, "stream should allocate"))
            return 1;
        if (!Expect(StreamReader::HasEnoughPackets(stream.get(), 0, &queue),
                "aborted queue has enough packets"))
            return 1;
    }

    {
        PacketQueue queue;
        if (!Expect(queue.init() == 0, "packet queue should initialize"))
            return 1;
        queue.start();
        auto stream = MakeStream();
        if (!Expect(stream != nullptr, "stream should allocate"))
            return 1;
        for (int index = 0; index <= MIN_FRAMES; ++index) {
            AVPacket packet{};
            packet.duration = 2000;
            if (!Expect(queue.put(&packet) == 0, "queue should accept test packets"))
                return 1;
        }
        if (!Expect(StreamReader::HasEnoughPackets(stream.get(), 0, &queue),
                "queue above packet and duration thresholds has enough packets"))
            return 1;
    }

    {
        auto rtspContext = MakeFormatContext("rtsp", "file:test.mp4");
        if (!Expect(rtspContext != nullptr, "rtsp format context should allocate"))
            return 1;
        if (!Expect(StreamReader::IsRealtime(rtspContext.get()), "rtsp demuxer is realtime"))
            return 1;

        auto rtpUrlContext = MakeFormatContext("mov,mp4,m4a,3gp,3g2,mj2", "rtp://example.test/live", true);
        if (!Expect(rtpUrlContext != nullptr, "rtp url context should allocate"))
            return 1;
        if (!Expect(StreamReader::IsRealtime(rtpUrlContext.get()), "rtp url is realtime"))
            return 1;

        auto fileContext = MakeFormatContext("mov,mp4,m4a,3gp,3g2,mj2", "file:test.mp4");
        if (!Expect(fileContext != nullptr, "file context should allocate"))
            return 1;
        if (!Expect(!StreamReader::IsRealtime(fileContext.get()), "local file is not realtime"))
            return 1;
    }

    {
        bool sawStatus = false;
        PlaybackStatus status;
        auto callbacks = RequiredCallbacks();
        callbacks.publishStatus = [&](const PlaybackStatus& published) {
            sawStatus = true;
            status = published;
        };
        bool stoppedRefreshLoop = false;
        callbacks.stopRefreshLoop = [&] {
            stoppedRefreshLoop = true;
        };
        StreamReader reader(std::move(callbacks));

        auto state = std::make_unique<VideoState>();
        state->session.filename = av_strdup("missing-stream-reader-test-input.mp4");
        if (!Expect(state->session.filename != nullptr, "test filename should allocate"))
            return 1;
        state->session.source.location = state->session.filename;
        state->session.source.network.maxReconnectAttempts = 7;
        reader.Run(state.get());
        av_freep(&state->session.filename);

        if (!Expect(sawStatus, "read failure publishes playback status"))
            return 1;
        if (!Expect(status.state == PlaybackState::Failed, "read failure publishes failed state"))
            return 1;
        if (!Expect(status.error == PlaybackError::Unknown || status.error == PlaybackError::NotFound,
                "read failure maps through playback error policy"))
            return 1;
        if (!Expect(status.maxReconnectAttempts == 7, "read failure preserves reconnect attempt limit"))
            return 1;
        if (!Expect(stoppedRefreshLoop, "read failure stops refresh loop"))
            return 1;
    }

    {
        StreamReaderCallbacks callbacks = RequiredCallbacks();
        callbacks.openComponent = nullptr;
        bool threw = false;
        try {
            StreamReader reader(std::move(callbacks));
        } catch (const std::invalid_argument&) {
            threw = true;
        }
        if (!Expect(threw, "missing required callback throws invalid_argument"))
            return 1;
    }

    return 0;
}
