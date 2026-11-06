#include <player_sdk_c/player.h>
#include <stdio.h>

static void PS_CALL on_event(void* user, const ps_event* event)
{
    ps_event_info_v1 info;
    (void)user;
    if (ps_event_get_info_v1(event, &info) != PS_OK) return;
    if (info.type == PS_EVENT_OPERATION) {
        fprintf(stderr, "operation=%llu status=%d error=%s\n",
            (unsigned long long)info.operation,
            (int)info.operation_status, info.operation_error.message);
    }
}

int main(int argc, char** argv)
{
    ps_player_handle player = NULL;
    ps_error_v1 error;
    ps_source_v1 source;
    ps_request_v1 request;
    int result = 0;
    /* Windows argv depends on the console code page. Use an ASCII URL/path here,
       or pass a UTF-8 path from your application's Unicode argument conversion. */
    if (argc != 2) { fprintf(stderr, "Usage: player_sdk_c_console <UTF-8 path or URL>\n"); return 1; }
    if (ps_player_create_v1(NULL, on_event, NULL, &player, &error) != PS_OK) {
        fprintf(stderr, "%s\n", error.message); return 1;
    }
    ps_source_init_v1(&source);
    source.location = argv[1];
    if (ps_player_open_v1(player, &source, &request) != PS_OK ||
        ps_player_play_v1(player, &request) != PS_OK) {
        fprintf(stderr, "%s\n", request.error.message); result = 1;
    } else {
        puts("Playing audio and reporting operations (no video renderer). Press Enter to exit.");
        (void)getchar();
    }
    if (ps_player_destroy(player) != PS_OK) result = 1;
    return result;
}
