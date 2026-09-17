#pragma once

#include "video_state.h"

class DecoderWorkers final {
public:
    DecoderWorkers() = delete;

    static int Audio(void* opaque);
    static int Video(void* opaque);
    static int Subtitle(void* opaque);

    static int GetVideoFrame(VideoState* state, AVFrame* frame);
    static int QueuePicture(VideoState* state,
                            AVFrame* sourceFrame,
                            double pts,
                            double duration,
                            int64_t pos,
                            int serial);
};
