#define SDL_MAIN_HANDLED
#include <SDL.h>
#include <play_core/player.h>
#include <iostream>
int main() {
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) return 1;
    for (int i=0;i<10;++i) {
        play_core::Player first;
        { play_core::Player second; second.close(); }
        if (!(SDL_WasInit(SDL_INIT_AUDIO) & SDL_INIT_AUDIO)) return 2;
        first.close();
    }
    const bool alive = (SDL_WasInit(SDL_INIT_AUDIO) & SDL_INIT_AUDIO) != 0;
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
    if (!alive) return 3;
    std::cout << "Host SDL initialization survives SDK instance teardown\n"; return 0;
}
