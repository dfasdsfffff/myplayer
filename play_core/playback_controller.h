#pragma once

#include "enums.h"

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
        std::function<void(double)> setVolume;
        std::function<void(double)> setSpeed;
        std::function<void(VideoLoopPolicy)> setLoopPolicy;
        std::function<void()> cycleAudioTrack;
        std::function<void()> cycleSubtitleTrack;
        std::function<void()> addVolume;
        std::function<void()> subVolume;
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
    void setVolume(double percent);
    void setSpeed(double speed);
    void setLoopPolicy(VideoLoopPolicy policy);
    void cycleAudioTrack();
    void cycleSubtitleTrack();
    void addVolume();
    void subVolume();

private:
    Actions m_actions;
};
