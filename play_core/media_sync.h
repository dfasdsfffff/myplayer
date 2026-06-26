#pragma once

#include <cstdint>

struct Frame;
struct VideoState;

namespace MediaSync {

int get_master_sync_type(VideoState* is);
double get_master_clock(VideoState* is);
void check_external_clock_speed(VideoState* is);
double compute_target_delay(double delay, VideoState* is);
double vp_duration(VideoState* is, Frame* vp, Frame* nextvp);
void update_video_pts(VideoState* is, double pts, int64_t pos, int serial);

} // namespace MediaSync
