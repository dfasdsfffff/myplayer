#pragma once

#include <functional>
#include <string>

class PlaybackController {
public:
    struct Actions {
        std::function<bool(const std::string&)> play;
        std::function<void()> pause;
        std::function<void(double)> seek;
        std::function<void(int)> seekSeconds;
        std::function<void()> seekForward;
        std::function<void()> seekBack;
        std::function<void()> stop;
        std::function<void()> stopAndWait;
    };

    explicit PlaybackController(Actions actions);

    static PlaybackController* GetInstance();

    bool play(const std::string& fileName);
    void pause();
    void seek(double percent);
    void seekSeconds(int seconds);
    void seekForward();
    void seekBack();
    void stop();
    void stopAndWait();

private:
    Actions m_actions;
};
