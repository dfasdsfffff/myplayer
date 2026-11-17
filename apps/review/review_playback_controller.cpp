#include "review_playback_controller.h"
#include <algorithm>
namespace review {
void ReviewPlaybackController::cancel() {
    pending_={}; stage_=Stage::Idle; target_.reset(); resume_=false;
    scrubbing_=false; scrubResume_=false;
}
void ReviewPlaybackController::reset(bool preserveRange) {
    cancel(); boundaryLatched_=false;
    if (!preserveRange) { duration_=0; in_=0; out_=0; }
}
void ReviewPlaybackController::setDuration(qint64 duration) {
    const bool whole=out_==duration_;
    duration_=std::max<qint64>(0,duration);
    if (whole) out_=duration_;
    else out_=std::min(out_,duration_);
    if (in_>=out_) { in_=0; out_=duration_; }
}
bool ReviewPlaybackController::setRange(qint64 in, qint64 out) {
    if (in<0 || in>=out || out>duration_) return false;
    const bool wasBusy=busy(); cancel(); in_=in; out_=out; boundaryLatched_=false;
    // Replacing an in-flight resume intent must leave playback paused.
    if (wasBusy) pause();
    return true;
}
void ReviewPlaybackController::issue(Command command, qint64 value, Stage stage) {
    stage_=stage; pending_=submit_(command,value);
    if (!pending_) cancel();
}
void ReviewPlaybackController::pause() {
    cancel(); boundaryLatched_=true; issue(Command::Pause,0,Stage::Pausing);
}
void ReviewPlaybackController::play(qint64 position) {
    cancel(); boundaryLatched_=false;
    if (hasRange() && (position<in_ || position>=out_)) seek(in_,true);
    else issue(Command::Play,0,Stage::Playing);
}
void ReviewPlaybackController::seek(qint64 position, bool resume) {
    cancel(); target_=duration_>0 ? std::clamp(position,qint64{0},duration_-1) : std::max<qint64>(0,position);
    // Manual navigation may inspect outside I/O, but must not silently play outside it.
    resume_=resume && (!hasRange() || (*target_>=in_ && *target_<out_));
    boundaryLatched_=false;
    issue(Command::Pause,0,Stage::Pausing);
}
void ReviewPlaybackController::beginScrub(bool wasPlaying) {
    cancel(); scrubbing_=true; scrubResume_=wasPlaying;
    issue(Command::Pause,0,Stage::Pausing);
}
void ReviewPlaybackController::endScrub(qint64 position, bool stopOnScrub) {
    if (!scrubbing_) return;
    const bool resume=scrubResume_ && !stopOnScrub;
    seek(position,resume);
}
void ReviewPlaybackController::positionChanged(qint64 position, bool playing) {
    if (!playing || busy() || scrubbing_ || boundaryLatched_ || !hasRange()) return;
    if (position>=out_) {
        if (loop_) seek(in_,true); else pause();
        boundaryLatched_=true;
    }
}
void ReviewPlaybackController::afterPause() {
    if (target_) issue(Command::Seek,*target_,Stage::Seeking);
    else stage_=Stage::Idle;
}
void ReviewPlaybackController::finished(const play_core::OperationResult& result) {
    if (!pending_ || result.session!=pending_.session || result.operation!=pending_.operation) return;
    pending_={};
    if (result.status!=play_core::OperationStatus::Succeeded) { cancel(); boundaryLatched_=true; return; }
    switch (stage_) {
    case Stage::Pausing: afterPause(); break;
    case Stage::Seeking:
        target_.reset();
        if (resume_) { resume_=false; issue(Command::Play,0,Stage::Playing); }
        else stage_=Stage::Idle;
        break;
    case Stage::Playing: stage_=Stage::Idle; boundaryLatched_=false; break;
    case Stage::Idle: break;
    }
}
}
