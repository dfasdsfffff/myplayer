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
#ifndef VIDEOCTL_H
#define VIDEOCTL_H

#include <string>
#include <shared_mutex>
#include <atomic>
#include <memory>
#include <mutex>
#include <vector>

#include "datactl.h"
#include "enums.h"
#include "media_session.h"
#include "renderer_dispatcher.h"
#include "signal.h"
#include "video_frame.h"

#ifndef CONFIG_AVFILTER
#define CONFIG_AVFILTER 0
#endif

//单例模式
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


	static int audio_decode_frame(VideoState* is);  // 设为static，可从静态回调调用
	void update_sample_display(VideoState* is, short* samples, int samples_size);
	void set_play_speed(double dSpeed);
	void set_play_loop_policy(VideoLoopPolicy loopPolicy);

	// Signal 成员，替代 Qt signals
	Signal<const std::string&>  SigPlayMsg;
	Signal<int, int>     SigFrameDimensionsChanged;
	Signal<std::shared_ptr<VideoFrame>> SigVideoFrame;
	Signal<int>          SigVideoTotalSeconds;
	Signal<int>          SigVideoPlaySeconds;
	Signal<double>       SigVideoVolume;
	Signal<bool>         SigPauseStat;
	Signal<>             SigStop;
	Signal<>             SigStopFinished;
	Signal<const std::string&>  SigStartPlay;
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
	/**
	 * @brief	从视频队列中获取数据，并解码数据，得到可显示的视频帧
	 *
	 * @return	-1表示出错，0表示没有得到视频帧，1表示得到视频帧
	 * @note 返回值0表示，数据帧被丢弃了
	 */
	int get_video_frame(VideoState* is, AVFrame* frame);

	int audio_thread(void* arg);

	int video_thread(void* arg);

	int subtitle_thread(void* arg);
	/**
	 * @brief	同步音频
	 * @param  is 视频状态, nb_samples 音频采样数
	 * @return	-1表示出错，0表示没有得到视频帧，1表示得到视频帧
	 * @note 返回具体的音频采样数
	 */
	static int synchronize_audio(VideoState* is, int nb_samples);  // 设为static，可从static函数调用

	int audio_open(void* opaque, AVChannelLayout* wanted_channel_layout, int wanted_sample_rate, struct AudioParams* audio_hw_params);
	int stream_component_open(VideoState* is, int stream_index);
	static int stream_has_enough_packets(AVStream* st, int stream_id, PacketQueue* queue);  // 纯操作，设为static
	static int is_realtime(AVFormatContext* s);  // 纯操作，设为static
	void ReadThread(VideoState* CurStream);
	void LoopThread();
	VideoState* stream_open(const char* filename);

	void stream_cycle_channel(VideoState* is, int codec_type);
	void refresh_loop_wait_event(VideoState* is);
	void seek_chapter(VideoState* is, int incr);
	void video_refresh(void* opaque, double* remaining_time);
	int queue_picture(VideoState* is, AVFrame* src_frame, double pts, double duration, int64_t pos, int serial);
	//更新音量
	void UpdateVolume(int sign, double step);

	void video_display();  // 移除参数，使用m_CurStream
	void emit_video_frame(VideoState* is);
	void do_exit();  // 移除参数，使用m_CurStream
	void stream_component_close(VideoState* is, int stream_index);  // 保留参数，内部使用
	void stream_close(VideoState* is);  // 保留参数，清理函数

	static int get_master_sync_type(VideoState* is);  // 纯计算，设为static
	static double get_master_clock(VideoState* is);  // 纯计算，设为static
	static void check_external_clock_speed(VideoState* is);  // 纯操作，设为static
	void stream_seek(int64_t pos, int64_t rel);  // 移除参数，使用m_CurStream
	void stream_toggle_pause();  // 移除参数，使用m_CurStream
	void toggle_pause();  // 移除参数，使用m_CurStream
	void step_to_next_frame();  // 移除参数，使用m_CurStream
	static double compute_target_delay(double delay, VideoState* is);  // 纯计算，设为static
	static double vp_duration(VideoState* is, Frame* vp, Frame* nextvp);  // 纯计算，设为static
	static void update_video_pts(VideoState* is, double pts, int64_t pos, int serial);  // 纯操作，设为static
public:
	static int configure_filtergraph(AVFilterGraph* graph, const char* filtergraph,
		AVFilterContext* source_ctx, AVFilterContext* sink_ctx);  // 纯配置，设为static
	int configure_video_filters(AVFilterGraph* graph, VideoState* is, const char* vfilters, AVFrame* frame);
	int configure_audio_filters(VideoState* is, const char* mAfilters, int force_output_format);

private:

	std::atomic_bool m_bPlayLoop{ false }; //刷新循环标志
	std::mutex m_playbackMutex;
	MediaSession m_mediaSession;

	bool m_bAutorotate = true;

	VideoState* m_CurStream;
	std::shared_mutex m_streamMutex;  // 保护 m_CurStream 的读写
	SDL_AudioDeviceID m_sdlAudio_dev;
	//
	VideoLoopPolicy m_loopPolicy = LOOP_ALL; //循环策略

	/* options specified by the user */
	int startup_volume;

	//播放刷新循环线程
	std::thread m_tPlayLoopThread;

	RendererDispatcher m_rendererDispatcher;

	// 倍速播放相关
	std::shared_mutex m_speedMutex;   // 保护mPlaybackSpeed的读写
	float m_fPlaybackSpeed = 1;       // 当前的播放速度，默认为1倍速
};

#endif // VIDEOCTL_H
