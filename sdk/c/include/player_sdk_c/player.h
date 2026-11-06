#ifndef PLAYER_SDK_C_PLAYER_H
#define PLAYER_SDK_C_PLAYER_H
#include <stdint.h>
#if defined(_WIN32)
# define PS_CALL __cdecl
# if defined(PLAYER_SDK_C_BUILD)
#  define PS_API __declspec(dllexport)
# else
#  define PS_API __declspec(dllimport)
# endif
#else
# define PS_CALL
# define PS_API __attribute__((visibility("default")))
#endif
#ifdef __cplusplus
extern "C" {
#endif
#define PS_ABI_VERSION 1u
#define PS_OK 0
#define PS_ERROR_INVALID_ARGUMENT 1
#define PS_ERROR_INVALID_STATE 2
#define PS_ERROR_CAPACITY 3
#define PS_ERROR_CANCELLED 4
#define PS_ERROR_MEDIA 5
#define PS_ERROR_INTERNAL 6
#define PS_ERROR_TIMEOUT 7
#define PS_ERROR_AUTHENTICATION 8
#define PS_ERROR_NOT_FOUND 9
#define PS_ERROR_UNSUPPORTED_PROTOCOL 10
#define PS_ERROR_NETWORK_UNAVAILABLE 11
#define PS_ERROR_CONNECTION_LOST 12
#define PS_ERROR_INVALID_MEDIA 13
#define PS_ERROR_DECODER_FAILURE 14
#define PS_HARDWARE_AUTO 0
#define PS_HARDWARE_DISABLED 1
#define PS_HARDWARE_D3D11VA 2
#define PS_RTSP_TCP 0
#define PS_RTSP_UDP 1
#define PS_STATE_CLOSED 0
#define PS_STATE_OPENING 1
#define PS_STATE_READY 2
#define PS_STATE_PLAYING 3
#define PS_STATE_PAUSED 4
#define PS_STATE_RECONNECTING 5
#define PS_STATE_STOPPED 6
#define PS_STATE_CLOSING 7
#define PS_STATE_ENDED 8
#define PS_STATE_FAILED 9
#define PS_EVENT_STATE 0
#define PS_EVENT_MEDIA_INFO 1
#define PS_EVENT_POSITION 2
#define PS_EVENT_OPERATION 3
#define PS_EVENT_VIDEO 4
#define PS_EVENT_SUBTITLE 5
#define PS_EVENT_ENDED 6
#define PS_EVENT_ERROR 7
#define PS_EVENT_CLOSED 8
#define PS_OPERATION_SUCCEEDED 0
#define PS_OPERATION_FAILED 1
#define PS_OPERATION_CANCELLED 2
#define PS_OPERATION_SUPERSEDED 3
#define PS_MEDIA_UNKNOWN 0
#define PS_MEDIA_VIDEO 1
#define PS_MEDIA_AUDIO 2
#define PS_MEDIA_SUBTITLE 3
#define PS_VIDEO_BGRA32 0
#define PS_VIDEO_YUV420P 1
#define PS_COLOR_UNKNOWN 0
#define PS_COLOR_BT601 1
#define PS_COLOR_BT709 2
#define PS_COLOR_BT2020 3
#define PS_RANGE_UNKNOWN 0
#define PS_RANGE_LIMITED 1
#define PS_RANGE_FULL 2

typedef struct ps_player* ps_player_handle;
typedef struct ps_event ps_event;
/* Serial callback on the player dispatcher, not a UI thread. Event is borrowed
 * until callback returns. Commands are allowed; destruction of this player is not.
 * No exception may escape a foreign callback. */
typedef void (PS_CALL *ps_event_callback)(void* user_data, const ps_event* event);
typedef struct ps_player_options_v1 {
    uint32_t struct_size;
    int32_t hardware_decode, loop;
} ps_player_options_v1;
typedef struct ps_header_v1 { const char* name; const char* value; } ps_header_v1;
typedef struct ps_source_v1 {
    uint32_t struct_size;
    const char* location;
    int32_t enable_audio;
    int64_t connect_timeout_ms, read_timeout_ms, analyze_duration_us, probe_size;
    int32_t rtsp_transport;
    const char* user_agent;
    const ps_header_v1* headers;
    uint32_t header_count;
    int32_t max_reconnect_attempts;
    int64_t initial_reconnect_delay_ms, max_reconnect_delay_ms, stable_playback_reset_seconds;
    int32_t reconnect;
} ps_source_v1;
typedef struct ps_error_v1 {
    int32_t code, native_code;
    char message[512]; /* UTF-8, possibly truncated at a codepoint boundary. */
} ps_error_v1;
typedef struct ps_request_v1 {
    uint64_t operation, session;
    ps_error_v1 error;
} ps_request_v1;
typedef struct ps_event_info_v1 {
    int32_t type, state;
    uint64_t session;
    int64_t position_ms;
    uint64_t operation, operation_session;
    int32_t operation_status, has_requested_position, has_actual_position;
    int64_t requested_position_ms, actual_position_ms;
    ps_error_v1 error, operation_error;
} ps_event_info_v1;
typedef struct ps_media_info_v1 {
    int32_t network_source, live, seekable, has_duration;
    int64_t duration_ms;
    uint32_t track_count;
    int32_t width, height, aspect_num, aspect_den;
    double rotation_degrees;
} ps_media_info_v1;
typedef struct ps_track_info_v1 {
    int32_t stream_index, type;
    const char* language;
    const char* title;
    const char* codec;
    int32_t is_default, is_forced;
} ps_track_info_v1;
typedef struct ps_video_plane_v1 {
    const uint8_t* data;
    uint64_t size;
    int32_t stride, height;
} ps_video_plane_v1;
typedef struct ps_video_frame_v1 {
    int32_t format, width, height, bytes_per_line, aspect_num, aspect_den;
    double rotation_degrees;
    int32_t color_space, color_range, unsupported_hdr_transfer, has_timestamp;
    int64_t timestamp_ms;
    const uint8_t* bgra;
    uint64_t bgra_size;
    ps_video_plane_v1 planes[3];
} ps_video_frame_v1;
typedef struct ps_subtitle_v1 {
    int64_t start_ms, end_ms;
    const char* text;
    const char* ass_header;
    int32_t ass_packet;
    uint32_t bitmap_count;
} ps_subtitle_v1;
typedef struct ps_subtitle_bitmap_v1 {
    int32_t x, y, width, height, stride;
    const uint8_t* bgra;
    uint64_t bgra_size;
} ps_subtitle_bitmap_v1;

/* v1 layouts/functions are frozen. Future layouts get new suffixed APIs; never
 * enlarge a v1 output. Use native default alignment (no packing overrides).
 * Enum values are int32_t, booleans 0/1, strings UTF-8, times named by unit.
 * Inputs are copied before returning. Getter pointers borrow event storage.
 * NULL required arguments return INVALID_ARGUMENT. Getter outputs are cleared
 * on failure. Out parameters must not overlap input storage. */
PS_API uint32_t PS_CALL ps_abi_version(void);
PS_API const char* PS_CALL ps_version(void);
PS_API int32_t PS_CALL ps_player_options_init_v1(ps_player_options_v1* options);
PS_API int32_t PS_CALL ps_source_init_v1(ps_source_v1* source);
/* NULL options selects defaults; NULL callback discards events. error optional.
 * out_player is NULL on failure. Keep user_data alive until destroy returns. */
PS_API int32_t PS_CALL ps_player_create_v1(const ps_player_options_v1* options,
    ps_event_callback callback, void* user_data, ps_player_handle* out_player, ps_error_v1* error);
/* NULL is a no-op. Waits for workers/callbacks. Must not race calls on this
 * handle, nor run on its callback (returns INVALID_STATE there). */
PS_API int32_t PS_CALL ps_player_destroy(ps_player_handle player);
/* Returns request acceptance, not completion. out_request is required. Match
 * operation/session with PS_EVENT_OPERATION for terminal results. */
PS_API int32_t PS_CALL ps_player_open_v1(ps_player_handle player, const ps_source_v1* source, ps_request_v1* out_request);
PS_API int32_t PS_CALL ps_player_play_v1(ps_player_handle player, ps_request_v1* out_request);
PS_API int32_t PS_CALL ps_player_set_paused_v1(ps_player_handle player, int32_t paused, ps_request_v1* out_request);
PS_API int32_t PS_CALL ps_player_seek_v1(ps_player_handle player, int64_t position_ms, ps_request_v1* out_request);
PS_API int32_t PS_CALL ps_player_stop_v1(ps_player_handle player, ps_request_v1* out_request);
PS_API int32_t PS_CALL ps_player_close_v1(ps_player_handle player, ps_request_v1* out_request);
PS_API int32_t PS_CALL ps_player_set_hardware_decode_v1(ps_player_handle player, int32_t hardware, ps_request_v1* out_request);
PS_API int32_t PS_CALL ps_player_set_volume_v1(ps_player_handle player, double volume, ps_request_v1* out_request);
PS_API int32_t PS_CALL ps_player_set_playback_rate_v1(ps_player_handle player, double rate, ps_request_v1* out_request);
PS_API int32_t PS_CALL ps_player_select_audio_track_v1(ps_player_handle player, int32_t track_id, ps_request_v1* out_request);
PS_API int32_t PS_CALL ps_player_select_subtitle_track_v1(ps_player_handle player, int32_t enabled, int32_t track_id, ps_request_v1* out_request);
/* Retain creates an SDK-owned copy of the event handle, sharing immutable frame
 * storage. It outlives the player. Release each retained handle exactly once,
 * never release a borrowed callback event. NULL release is allowed. */
PS_API int32_t PS_CALL ps_event_retain(const ps_event* event, ps_event** out_event);
PS_API void PS_CALL ps_event_release(ps_event* event);
PS_API int32_t PS_CALL ps_event_get_info_v1(const ps_event* event, ps_event_info_v1* out_info);
PS_API int32_t PS_CALL ps_event_get_media_info_v1(const ps_event* event, ps_media_info_v1* out_info);
PS_API int32_t PS_CALL ps_event_get_track_v1(const ps_event* event, uint32_t index, ps_track_info_v1* out_track);
PS_API int32_t PS_CALL ps_event_get_video_v1(const ps_event* event, ps_video_frame_v1* out_frame);
PS_API int32_t PS_CALL ps_event_get_subtitle_v1(const ps_event* event, ps_subtitle_v1* out_subtitle);
PS_API int32_t PS_CALL ps_event_get_subtitle_bitmap_v1(const ps_event* event, uint32_t index, ps_subtitle_bitmap_v1* out_bitmap);
#ifdef __cplusplus
}
#endif
#endif
