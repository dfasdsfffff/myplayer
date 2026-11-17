#pragma once
#include <play_core/player.h>
#include <QtGlobal>
#include <functional>
#include <optional>
namespace review {
// GUI-thread orchestration only. Core remains authoritative for state and seek precision.
class ReviewPlaybackController {
public:
    enum class Command { Pause, Seek, Play };
    using Submit = std::function<play_core::Request(Command, qint64)>;
    explicit ReviewPlaybackController(Submit submit) : submit_(std::move(submit)) {}
    void reset(bool preserveRange = false);
    void setDuration(qint64 duration);
    bool setRange(qint64 in, qint64 out);
    qint64 inPoint() const { return in_; }
    qint64 outPoint() const { return out_; }
    bool hasRange() const { return duration_ > 0 && in_ < out_; }
    void setLoop(bool loop) { loop_ = loop; }
    bool looping() const { return loop_; }
    bool busy() const { return pending_.operation != 0; }
    void cancel();
    void pause();
    void play(qint64 position);
    void seek(qint64 position, bool resume);
    void beginScrub(bool wasPlaying);
    void endScrub(qint64 position, bool stopOnScrub);
    void positionChanged(qint64 position, bool playing);
    void finished(const play_core::OperationResult& result);
private:
    enum class Stage { Idle, Pausing, Seeking, Playing };
    void issue(Command command, qint64 value, Stage stage);
    void afterPause();
    Submit submit_;
    play_core::Request pending_;
    Stage stage_{Stage::Idle};
    qint64 duration_{0}, in_{0}, out_{0};
    std::optional<qint64> target_;
    bool resume_{false}, loop_{false}, scrubResume_{false}, scrubbing_{false};
    bool boundaryLatched_{false};
};
}
