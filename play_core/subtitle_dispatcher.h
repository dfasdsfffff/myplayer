#pragma once

#include "subtitle_frame.h"

#include <memory>

struct AVSubtitle;

class SubtitleDispatcher final {
public:
    static std::shared_ptr<const SubtitleFrame> Copy(const AVSubtitle& subtitle, double ptsSeconds);
};
