#ifndef PLAYER_C_LAYOUT_TEST_H
#define PLAYER_C_LAYOUT_TEST_H
#include <stddef.h>
_Static_assert(PS_ABI_VERSION == 1u, "PS_ABI_VERSION changed");
_Static_assert(PS_OK == 0, "PS_OK changed");
_Static_assert(PS_ERROR_INVALID_ARGUMENT == 1, "PS_ERROR_INVALID_ARGUMENT changed");
_Static_assert(PS_ERROR_INVALID_STATE == 2, "PS_ERROR_INVALID_STATE changed");
_Static_assert(PS_ERROR_CAPACITY == 3, "PS_ERROR_CAPACITY changed");
_Static_assert(PS_ERROR_CANCELLED == 4, "PS_ERROR_CANCELLED changed");
_Static_assert(PS_ERROR_MEDIA == 5, "PS_ERROR_MEDIA changed");
_Static_assert(PS_ERROR_INTERNAL == 6, "PS_ERROR_INTERNAL changed");
_Static_assert(PS_ERROR_TIMEOUT == 7, "PS_ERROR_TIMEOUT changed");
_Static_assert(PS_ERROR_AUTHENTICATION == 8, "PS_ERROR_AUTHENTICATION changed");
_Static_assert(PS_ERROR_NOT_FOUND == 9, "PS_ERROR_NOT_FOUND changed");
_Static_assert(PS_ERROR_UNSUPPORTED_PROTOCOL == 10, "PS_ERROR_UNSUPPORTED_PROTOCOL changed");
_Static_assert(PS_ERROR_NETWORK_UNAVAILABLE == 11, "PS_ERROR_NETWORK_UNAVAILABLE changed");
_Static_assert(PS_ERROR_CONNECTION_LOST == 12, "PS_ERROR_CONNECTION_LOST changed");
_Static_assert(PS_ERROR_INVALID_MEDIA == 13, "PS_ERROR_INVALID_MEDIA changed");
_Static_assert(PS_ERROR_DECODER_FAILURE == 14, "PS_ERROR_DECODER_FAILURE changed");
_Static_assert(PS_HARDWARE_AUTO == 0, "PS_HARDWARE_AUTO changed");
_Static_assert(PS_HARDWARE_DISABLED == 1, "PS_HARDWARE_DISABLED changed");
_Static_assert(PS_HARDWARE_D3D11VA == 2, "PS_HARDWARE_D3D11VA changed");
_Static_assert(PS_RTSP_TCP == 0, "PS_RTSP_TCP changed");
_Static_assert(PS_RTSP_UDP == 1, "PS_RTSP_UDP changed");
_Static_assert(PS_STATE_CLOSED == 0, "PS_STATE_CLOSED changed");
_Static_assert(PS_STATE_OPENING == 1, "PS_STATE_OPENING changed");
_Static_assert(PS_STATE_READY == 2, "PS_STATE_READY changed");
_Static_assert(PS_STATE_PLAYING == 3, "PS_STATE_PLAYING changed");
_Static_assert(PS_STATE_PAUSED == 4, "PS_STATE_PAUSED changed");
_Static_assert(PS_STATE_RECONNECTING == 5, "PS_STATE_RECONNECTING changed");
_Static_assert(PS_STATE_STOPPED == 6, "PS_STATE_STOPPED changed");
_Static_assert(PS_STATE_CLOSING == 7, "PS_STATE_CLOSING changed");
_Static_assert(PS_STATE_ENDED == 8, "PS_STATE_ENDED changed");
_Static_assert(PS_STATE_FAILED == 9, "PS_STATE_FAILED changed");
_Static_assert(PS_EVENT_STATE == 0, "PS_EVENT_STATE changed");
_Static_assert(PS_EVENT_MEDIA_INFO == 1, "PS_EVENT_MEDIA_INFO changed");
_Static_assert(PS_EVENT_POSITION == 2, "PS_EVENT_POSITION changed");
_Static_assert(PS_EVENT_OPERATION == 3, "PS_EVENT_OPERATION changed");
_Static_assert(PS_EVENT_VIDEO == 4, "PS_EVENT_VIDEO changed");
_Static_assert(PS_EVENT_SUBTITLE == 5, "PS_EVENT_SUBTITLE changed");
_Static_assert(PS_EVENT_ENDED == 6, "PS_EVENT_ENDED changed");
_Static_assert(PS_EVENT_ERROR == 7, "PS_EVENT_ERROR changed");
_Static_assert(PS_EVENT_CLOSED == 8, "PS_EVENT_CLOSED changed");
_Static_assert(PS_OPERATION_SUCCEEDED == 0, "PS_OPERATION_SUCCEEDED changed");
_Static_assert(PS_OPERATION_FAILED == 1, "PS_OPERATION_FAILED changed");
_Static_assert(PS_OPERATION_CANCELLED == 2, "PS_OPERATION_CANCELLED changed");
_Static_assert(PS_OPERATION_SUPERSEDED == 3, "PS_OPERATION_SUPERSEDED changed");
_Static_assert(PS_MEDIA_UNKNOWN == 0, "PS_MEDIA_UNKNOWN changed");
_Static_assert(PS_MEDIA_VIDEO == 1, "PS_MEDIA_VIDEO changed");
_Static_assert(PS_MEDIA_AUDIO == 2, "PS_MEDIA_AUDIO changed");
_Static_assert(PS_MEDIA_SUBTITLE == 3, "PS_MEDIA_SUBTITLE changed");
_Static_assert(PS_VIDEO_BGRA32 == 0, "PS_VIDEO_BGRA32 changed");
_Static_assert(PS_VIDEO_YUV420P == 1, "PS_VIDEO_YUV420P changed");
_Static_assert(PS_COLOR_UNKNOWN == 0, "PS_COLOR_UNKNOWN changed");
_Static_assert(PS_COLOR_BT601 == 1, "PS_COLOR_BT601 changed");
_Static_assert(PS_COLOR_BT709 == 2, "PS_COLOR_BT709 changed");
_Static_assert(PS_COLOR_BT2020 == 3, "PS_COLOR_BT2020 changed");
_Static_assert(PS_RANGE_UNKNOWN == 0, "PS_RANGE_UNKNOWN changed");
_Static_assert(PS_RANGE_LIMITED == 1, "PS_RANGE_LIMITED changed");
_Static_assert(PS_RANGE_FULL == 2, "PS_RANGE_FULL changed");
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
