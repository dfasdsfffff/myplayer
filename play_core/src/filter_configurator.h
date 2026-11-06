#pragma once

#include "video_state.h"

#ifndef CONFIG_AVFILTER
#define CONFIG_AVFILTER 0
#endif

int ConfigureFilterGraph(AVFilterGraph* graph,
                         const char* filtergraph,
                         AVFilterContext* sourceCtx,
                         AVFilterContext* sinkCtx);

int ConfigureVideoFilters(AVFilterGraph* graph,
                          VideoState* state,
                          const char* filters,
                          AVFrame* frame,
                          bool autorotate);

int ConfigureAudioFilters(VideoState* state,
                          const char* filters,
                          bool forceOutputFormat);
