#include <player_sdk_c/player.h>
#include "player_c_layout.h"
#include <stdio.h>
#include <string.h>
#include <math.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "line %d: %s\n", __LINE__, #x); return 1; } } while (0)
int main(void) {
    ps_player_options_v1 options;
    ps_source_v1 source;
    ps_player_handle player = NULL;
    ps_error_v1 err;
    ps_request_v1 request;
    ps_event_info_v1 info;
    ps_media_info_v1 media;
    ps_track_info_v1 track;
    ps_video_frame_v1 video;
    ps_subtitle_v1 subtitle;
    ps_subtitle_bitmap_v1 bitmap;
    ps_event* retained = NULL;
    CHECK(ps_abi_version() == PS_ABI_VERSION && strlen(ps_version()) > 0);
    CHECK(ps_player_options_init_v1(NULL) == PS_ERROR_INVALID_ARGUMENT);
    CHECK(ps_source_init_v1(NULL) == PS_ERROR_INVALID_ARGUMENT);
    CHECK(ps_player_options_init_v1(&options) == PS_OK);
    CHECK(ps_source_init_v1(&source) == PS_OK);
    CHECK(options.struct_size == sizeof(options) && source.connect_timeout_ms == 10000);
    options.struct_size = 0;
    CHECK(ps_player_create_v1(&options, NULL, NULL, &player, &err) == PS_ERROR_INVALID_ARGUMENT);
    CHECK(player == NULL && err.code == PS_ERROR_INVALID_ARGUMENT);
    CHECK(ps_player_create_v1(NULL, NULL, NULL, NULL, &err) == PS_ERROR_INVALID_ARGUMENT);
    CHECK(ps_player_play_v1(NULL, &request) == PS_ERROR_INVALID_ARGUMENT && request.operation == 0);
    CHECK(ps_event_retain(NULL, &retained) == PS_ERROR_INVALID_ARGUMENT && retained == NULL);
    CHECK(ps_event_get_info_v1(NULL, &info) == PS_ERROR_INVALID_ARGUMENT);
    CHECK(ps_event_get_media_info_v1(NULL, &media) == PS_ERROR_INVALID_ARGUMENT);
    CHECK(ps_event_get_track_v1(NULL, 0, &track) == PS_ERROR_INVALID_ARGUMENT);
    CHECK(ps_event_get_video_v1(NULL, &video) == PS_ERROR_INVALID_ARGUMENT);
    CHECK(ps_event_get_subtitle_v1(NULL, &subtitle) == PS_ERROR_INVALID_ARGUMENT);
    CHECK(ps_event_get_subtitle_bitmap_v1(NULL, 0, &bitmap) == PS_ERROR_INVALID_ARGUMENT);
    ps_event_release(NULL);
    CHECK(ps_player_destroy(NULL) == PS_OK);
    CHECK(ps_player_options_init_v1(&options) == PS_OK);
    options.hardware_decode = PS_HARDWARE_DISABLED;
    CHECK(ps_player_create_v1(&options, NULL, NULL, &player, &err) == PS_OK);
    CHECK(ps_player_play_v1(player, NULL) == PS_ERROR_INVALID_ARGUMENT);
    CHECK(ps_player_open_v1(player, NULL, &request) == PS_ERROR_INVALID_ARGUMENT);
    source.location = "";
    CHECK(ps_player_open_v1(player, &source, &request) == PS_ERROR_INVALID_ARGUMENT);
    CHECK(request.operation == 0 && request.error.message[0]);
    CHECK(ps_player_set_paused_v1(player, 2, &request) == PS_ERROR_INVALID_ARGUMENT);
    CHECK(ps_player_set_hardware_decode_v1(player, 99, &request) == PS_ERROR_INVALID_ARGUMENT);
    CHECK(ps_player_set_volume_v1(player, NAN, &request) == PS_ERROR_INVALID_ARGUMENT);
    CHECK(ps_player_set_playback_rate_v1(player, 3, &request) == PS_ERROR_INVALID_ARGUMENT);
    CHECK(ps_player_seek_v1(player, -1, &request) == PS_ERROR_INVALID_ARGUMENT);
    CHECK(ps_player_select_subtitle_track_v1(player, 3, 0, &request) == PS_ERROR_INVALID_ARGUMENT);
    CHECK(ps_player_set_volume_v1(player, 0.5, &request) == PS_OK && request.operation);
    CHECK(ps_player_set_playback_rate_v1(player, 1, &request) == PS_OK);
    CHECK(ps_player_set_hardware_decode_v1(player, PS_HARDWARE_DISABLED, &request) == PS_OK);
    CHECK(ps_player_set_paused_v1(player, 1, &request) == PS_OK);
    CHECK(ps_player_select_audio_track_v1(player, 0, &request) == PS_OK);
    CHECK(ps_player_select_subtitle_track_v1(player, 0, 0, &request) == PS_OK);
    CHECK(ps_player_stop_v1(player, &request) == PS_OK);
    CHECK(ps_player_close_v1(player, &request) == PS_OK);
    CHECK(ps_player_destroy(player) == PS_OK);
    return 0;
}
