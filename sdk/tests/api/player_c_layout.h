#ifndef PLAYER_C_LAYOUT_TEST_H
#define PLAYER_C_LAYOUT_TEST_H
#include <stddef.h>
/* Frozen v1 x64 baseline. These fail if a future edit changes the binary layout. */
#if UINTPTR_MAX == UINT64_MAX
#define SIZE(type, size) _Static_assert(sizeof(type) == size, #type " size changed")
#define OFFSET(type, field, offset) _Static_assert(offsetof(type, field) == offset, #type "." #field " moved")
SIZE(ps_player_options_v1, 12);
SIZE(ps_header_v1, 16);
SIZE(ps_source_v1, 120);
OFFSET(ps_source_v1, location, 8);
OFFSET(ps_source_v1, connect_timeout_ms, 24);
OFFSET(ps_source_v1, user_agent, 64);
OFFSET(ps_source_v1, headers, 72);
OFFSET(ps_source_v1, reconnect, 112);
SIZE(ps_error_v1, 520);
SIZE(ps_request_v1, 536);
OFFSET(ps_request_v1, error, 16);
SIZE(ps_event_info_v1, 1112);
OFFSET(ps_event_info_v1, requested_position_ms, 56);
OFFSET(ps_event_info_v1, error, 72);
OFFSET(ps_event_info_v1, operation_error, 592);
SIZE(ps_media_info_v1, 56);
OFFSET(ps_media_info_v1, rotation_degrees, 48);
SIZE(ps_track_info_v1, 40);
SIZE(ps_video_plane_v1, 24);
SIZE(ps_video_frame_v1, 144);
OFFSET(ps_video_frame_v1, rotation_degrees, 24);
OFFSET(ps_video_frame_v1, bgra, 56);
OFFSET(ps_video_frame_v1, planes, 72);
SIZE(ps_subtitle_v1, 40);
SIZE(ps_subtitle_bitmap_v1, 40);
#undef SIZE
#undef OFFSET
#endif
#endif
