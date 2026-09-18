/*
 * @file 	videoctl.h
 * @date 	2018/01/07 10:48
 *
 * @author 	itisyang
 * @Contact	itisyang@gmail.com
 *
 * @brief 	视频控制类
 * @note 	纯 C++ 实现，无 Qt 依赖
 */
#pragma once

#include <string>
#include <shared_mutex>
#include <atomic>
#include <memory>
#include <mutex>

#include "audio_output.h"
#include "datactl.h"
#include "enums.h"
#include "media_source.h"
#include "media_session.h"
#include "playback_command_mailbox.h"
#include "reconnect_controller.h"
#include "renderer_dispatcher.h"
#include "signal.h"
#include "stream_reader.h"
#include "subtitle_frame.h"
#include "video_frame_converter.h"

#ifndef CONFIG_AVFILTER
#define CONFIG_AVFILTER 0
#endif

class VideoCtl
{
private:
	/// @brief 构造函数
	VideoCtl();
public:
	/// @brief 获取一个d静态实例的指针
	/**
	* @brief 创建一个视频播放实例，不是单例
	*/
	static std::shared_ptr<VideoCtl> MakeInstance();

	/// @brief 析构函数
	~VideoCtl();
	/**
	* @brief	开始播放
	*
	* @param	strFileName 文件完整路径（UTF-8）
	* @param	widPlayWid 播放窗口原生句柄
	* @return	true 成功 false 失败
	* @note
	*/
	bool StartPlay(const std::string& strFileName);
	bool StartPlay(const MediaSource& source);


	void set_play_speed(double dSpeed);
	void set_play_loop_policy(VideoLoopPolicy loopPolicy);

	// Signal 成员，替代 Qt signals
	Signal<const std::string&>  SigPlayMsg;
	Signal<int, int>     SigFrameDimensionsChanged;
	Signal<std::shared_ptr<VideoFrame>> SigVideoFrame;
	Signal<std::shared_ptr<const SubtitleFrame>> SigSubtitleFrame;
	Signal<int>          SigVideoTotalSeconds;
	Signal<int>          SigVideoPlaySeconds;
	Signal<double>       SigVideoVolume;
	Signal<bool>         SigPauseStat;
	Signal<>             SigStop;
	Signal<>             SigStopFinished;
	Signal<const std::string&>  SigStartPlay;
	Signal<const PlaybackStatus&> SigPlaybackStatus;
	Signal<const MediaInfo&> SigMediaInfo;
	Signal<>             SigPlayNextOne;
	Signal<>             SigRandomPlayOne;

public:
	void OnPlaySeek(double dPercent);
	void OnPlaySeekSeconds(int seconds);
	void OnPlayVolume(double dPercent);
	void OnSeekForward();
	void OnSeekBack();
	void OnAddVolume();
	void OnSubVolume();
	void OnPause();
	void OnStop();
	void OnStopAndWait();
	void OnCycleAudioTrack();
	void OnCycleSubtitleTrack();
	void OnSelectAudioTrack(int streamIndex);
	void OnSelectSubtitleTrack(std::optional<int> streamIndex);

private:
	/**
	 * @brief	初始化
	 *
	 * @return	true 成功 false 失败
	 * @note
	 */
	bool Init();

	/**
	 * @brief	连接内部信号
	 *
	 * @return	true 成功 false 失败
	 * @note
	 */
	bool ConnectSignalSlots();
	int stream_component_open(VideoState* is, int stream_index);
	void LoopThread();
	VideoState* stream_open(const MediaSource& source);
	VideoState* stream_open(const char* filename);

	void stream_cycle_channel(VideoState* is, int codec_type);
	void refresh_loop_wait_event(VideoState* is);
	void seek_chapter(VideoState* is, int incr);
	void video_refresh(void* opaque, double* remaining_time);
	//更新音量
	void UpdateVolume(int sign, double step);

	void video_display();  // 移除参数，使用m_CurStream
	void emit_video_frame(VideoState* is);
	void emit_subtitle_frame(VideoState* is);
	void clear_active_subtitle();
	void do_exit();  // 移除参数，使用m_CurStream
	void stream_component_close(VideoState* is, int stream_index);  // 保留参数，内部使用
	void stream_close(VideoState* is);  // 保留参数，清理函数

	void requestStop();
	void stream_seek(int64_t pos, int64_t rel);  // 移除参数，使用m_CurStream
	void stream_toggle_pause();  // 移除参数，使用m_CurStream
	void toggle_pause();  // 移除参数，使用m_CurStream
	void step_to_next_frame();  // 移除参数，使用m_CurStream
	void applyPlaybackCommands();
	void notifyPlaybackCommand();
	void applyTrackCommand(VideoState* state, const TrackCommand& command);
private:

	std::atomic_bool m_bPlayLoop{ false }; //刷新循环标志
	std::mutex m_playbackMutex;
	ReconnectController m_reconnectController;
	MediaSession m_mediaSession;
	StreamReader m_streamReader;

	bool m_bAutorotate = true;

	VideoState* m_CurStream;
	std::shared_mutex m_streamMutex;  // 保护 m_CurStream 的读写
	AudioOutput m_audioOutput;
	//
	std::atomic<VideoLoopPolicy> m_loopPolicy{LOOP_ALL}; //循环策略

	/* options specified by the user */
	// 归一化音量 [0.0, 1.0]，避免与 SDL 音量标量混淆导致切换媒体后音量漂移
	std::atomic<double> m_volume{0.3};

	//播放刷新循环线程
	std::thread m_tPlayLoopThread;
	PlaybackCommandMailbox m_commandMailbox;

	RendererDispatcher m_rendererDispatcher;

	// 倍速播放相关
	std::shared_mutex m_speedMutex;   // 保护mPlaybackSpeed的读写
	float m_fPlaybackSpeed = 1;       // 当前的播放速度，默认为1倍速

	VideoFrameConverter m_frameConverter;
	std::shared_ptr<const SubtitleFrame> m_activeSubtitleFrame;

	// 由 RuntimeManager 记录的全局初始化引用，避免失败回滚破坏计数
	bool m_hasSdlInitRef = false;
	bool m_hasNetworkInitRef = false;
};


