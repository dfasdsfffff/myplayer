#include "media_sync.h"

#include "av_constants.h"
#include "av_types.h"
#include "video_state.h"

#include <cmath>

namespace MediaSync {

int get_master_sync_type(VideoState* is)
{
    if (is->clocks.av_sync_type == AV_SYNC_VIDEO_MASTER) {
        if (is->video.video_st)
            return AV_SYNC_VIDEO_MASTER;
        return AV_SYNC_AUDIO_MASTER;
    }
    if (is->clocks.av_sync_type == AV_SYNC_AUDIO_MASTER) {
        if (is->audio.audio_st)
            return AV_SYNC_AUDIO_MASTER;
        return AV_SYNC_EXTERNAL_CLOCK;
    }
    return AV_SYNC_EXTERNAL_CLOCK;
}

double get_master_clock(VideoState* is)
{
    switch (get_master_sync_type(is)) {
    case AV_SYNC_VIDEO_MASTER:
        return is->clocks.vidclk.get();
    case AV_SYNC_AUDIO_MASTER:
        return is->clocks.audclk.get();
    default:
        return is->clocks.extclk.get();
    }
}

void check_external_clock_speed(VideoState* is)
{
    if ((is->video.video_stream >= 0 && is->video.videoq.nb_packets <= EXTERNAL_CLOCK_MIN_FRAMES) ||
        (is->audio.audio_stream >= 0 && is->audio.audioq.nb_packets <= EXTERNAL_CLOCK_MIN_FRAMES)) {
        is->clocks.extclk.set_speed(FFMAX(EXTERNAL_CLOCK_SPEED_MIN, is->clocks.extclk.speed - EXTERNAL_CLOCK_SPEED_STEP));
    } else if ((is->video.video_stream < 0 || is->video.videoq.nb_packets > EXTERNAL_CLOCK_MAX_FRAMES) &&
        (is->audio.audio_stream < 0 || is->audio.audioq.nb_packets > EXTERNAL_CLOCK_MAX_FRAMES)) {
        is->clocks.extclk.set_speed(FFMIN(EXTERNAL_CLOCK_SPEED_MAX, is->clocks.extclk.speed + EXTERNAL_CLOCK_SPEED_STEP));
    } else {
        double speed = is->clocks.extclk.speed;
        if (speed != 1.0)
            is->clocks.extclk.set_speed(speed + EXTERNAL_CLOCK_SPEED_STEP * (1.0 - speed) / fabs(1.0 - speed));
    }
}

double compute_target_delay(double delay, VideoState* is)
{
    double sync_threshold;
    double diff = 0;

    if (get_master_sync_type(is) != AV_SYNC_VIDEO_MASTER) {
        diff = is->clocks.vidclk.get() - get_master_clock(is);

        sync_threshold = FFMAX(AV_SYNC_THRESHOLD_MIN, FFMIN(AV_SYNC_THRESHOLD_MAX, delay));
        if (!std::isnan(diff) && fabs(diff) < is->video.max_frame_duration) {
            if (diff <= -sync_threshold)
                delay = FFMAX(0, delay + diff);
            else if (diff >= sync_threshold && delay > AV_SYNC_FRAMEDUP_THRESHOLD)
                delay = delay + diff;
            else if (diff >= sync_threshold)
                delay = 2 * delay;
        }
    }

    av_log(nullptr, AV_LOG_TRACE, "video: delay=%0.3f A-V=%f\n", delay, -diff);
    return delay;
}

double vp_duration(VideoState* is, Frame* vp, Frame* nextvp)
{
    if (vp->serial != nextvp->serial)
        return 0.0;

    double duration = nextvp->pts - vp->pts;
    if (std::isnan(duration) || duration <= 0 || duration > is->video.max_frame_duration)
        return vp->duration;
    return duration;
}

void update_video_pts(VideoState* is, double pts, int64_t pos, int serial)
{
    is->clocks.vidclk.set(pts, serial);
    is->clocks.extclk.sync_to_slave(is->clocks.vidclk);
}

} // namespace MediaSync
