/*
 * @file 	videoctl.cpp
 * @date 	2018/01/21 12:15
 *
 * @author 	itisyang
 * @Contact	itisyang@gmail.com
 *
 * @brief 	视频控制类
 * @note
 */


#include <thread>
#include <mutex>
#include <new>
#include "videoctl.h"

#include "decoder_workers.h"
#include "filter_configurator.h"
#include "media_sync.h"
#include "network_input.h"
#include "playback_settings.h"
#include "runtime_manager.h"
#include "soundtouch_wrap.h"

#pragma execution_character_set("utf-8")

extern std::mutex g_show_rect_mutex;

static int NormalizedToSdlVolume(double volume)
{
	return PlaybackSettings::ToSdlVolume(volume, SDL_MIX_MAXVOLUME);
}

// 是否允许丢帧（如果视频太慢了，跟不上音频或者外部时钟）
// -1为自动丢帧，0为不丢帧，1为强制丢帧
static int framedrop = 1;

#define FF_QUIT_EVENT    (SDL_USEREVENT + 2)

static void print_error(const char* s, int err) {
	char buf[256];
	av_strerror(err, buf, sizeof(buf));
	av_log(NULL, AV_LOG_ERROR, "%s: %s\n", s, buf);
}

void VideoCtl::stream_component_close(VideoState* is, int stream_index)
{
	if (!is || !is->session.ic)
		return;

	AVFormatContext* ic = is->session.ic;
	AVCodecParameters* codecpar;

	if (!ic)
		return;
	if (stream_index < 0 || stream_index >= ic->nb_streams)
		return;
	codecpar = ic->streams[stream_index]->codecpar;

	switch (codecpar->codec_type) {
	case AVMEDIA_TYPE_AUDIO:
		is->audio.aud_decoder.abort(&is->audio.sampq);
		m_audioOutput.Close();
		is->audio.aud_decoder.destroy();
		swr_free(&is->audio.swr_ctx);
		av_freep(&is->audio.audio_buf1);
		is->audio.audio_buf1_size = 0;
		is->audio.audio_buf = NULL;

		if (is->audio.soundTouchHandle)
		{
			soundtouch_destroy(is->audio.soundTouchHandle);
			is->audio.soundTouchHandle = nullptr;
		}
		if (is->audio.audio_new_buf)
		{
			av_freep(&is->audio.audio_new_buf);
			is->audio.audio_new_buf = NULL;
		}
		break;
	case AVMEDIA_TYPE_VIDEO:
		is->video.vid_decoder.abort(&is->video.pictq);
		is->video.vid_decoder.destroy();
		break;
	case AVMEDIA_TYPE_SUBTITLE:
		is->subtitle.sub_decoder.abort(&is->subtitle.subpq);
		is->subtitle.sub_decoder.destroy();
		break;
	default:
		break;
	}

	ic->streams[stream_index]->discard = AVDISCARD_ALL;
	switch (codecpar->codec_type) {
	case AVMEDIA_TYPE_AUDIO:
		is->audio.audio_st = NULL;
		is->audio.audio_stream = -1;
		break;
	case AVMEDIA_TYPE_VIDEO:
		is->video.video_st = NULL;
		is->video.video_stream = -1;
		break;
	case AVMEDIA_TYPE_SUBTITLE:
		is->subtitle.subtitle_st = NULL;
		is->subtitle.subtitle_stream = -1;
		break;
	default:
		break;
	}
}
//关闭流
void VideoCtl::stream_close(VideoState* is)
{
	if (!is) return;
	/* XXX: 使用特殊的url_shutdown调用来彻底中止解析 */
	is->session.abort_request = 1;
	is->session.io.cancelled.store(true, std::memory_order_release);
	if (is->session.continue_read_thread)
		SDL_CondSignal(is->session.continue_read_thread);
	if (is->session.read_tid.joinable())
		is->session.read_tid.join();

	std::unique_lock<std::shared_mutex> trackLock(is->session.trackMutex);
	/* close each stream */
	if (is->session.ic) {
		if (is->audio.audio_stream >= 0)
			stream_component_close(is, is->audio.audio_stream);
		if (is->video.video_stream >= 0)
			stream_component_close(is, is->video.video_stream);
		if (is->subtitle.subtitle_stream >= 0)
			stream_component_close(is, is->subtitle.subtitle_stream);

	}

	// 关闭音频（尽管在stream_component_close已经调用了）
	m_audioOutput.Close();
	delete is;
}

void VideoCtl::set_play_speed(double dSpeed)
{
	constexpr double MIN_PLAYBACK_SPEED = 0.1;
	constexpr double MAX_PLAYBACK_SPEED = 2.0;

	if (dSpeed <= MIN_PLAYBACK_SPEED || dSpeed > MAX_PLAYBACK_SPEED)
		return;

	std::unique_lock<std::shared_mutex> speedLock(m_speedMutex);
	if (dSpeed == m_fPlaybackSpeed)
		return;
	m_fPlaybackSpeed = dSpeed;
	speedLock.unlock();

	// 保护 m_CurStream 访问
	std::unique_lock<std::shared_mutex> streamLock(m_streamMutex);
	if (m_CurStream)
		m_CurStream->audio.play_rate.store(m_fPlaybackSpeed, std::memory_order_release);
}

void VideoCtl::set_play_loop_policy(VideoLoopPolicy loopPolicy)
{
	m_loopPolicy.store(loopPolicy, std::memory_order_release);
}

/* seek in the stream */
void VideoCtl::stream_seek(int64_t pos, int64_t rel)
{
	if (!m_CurStream) return;
	if (!m_CurStream->session.ic || !CanSeek(m_CurStream->session.mediaInfo)) return;

	std::lock_guard<std::mutex> seekLock(m_CurStream->session.seek_mutex);
	if (!m_CurStream->session.seek_req) {
		m_CurStream->session.seek_pos = pos;
		m_CurStream->session.seek_rel = rel;
		m_CurStream->session.seek_flags &= ~AVSEEK_FLAG_BYTE;
		m_CurStream->session.seek_req = true;
		SDL_CondSignal(m_CurStream->session.continue_read_thread);
	}
}

/* pause or resume the video */
void VideoCtl::stream_toggle_pause()
{
	if (!m_CurStream) return;

	if (m_CurStream->session.paused) {
		m_CurStream->video.frame_timer += av_gettime_relative() / 1000000.0 - m_CurStream->clocks.vidclk.lastUpdated();
		if (m_CurStream->session.read_pause_return != AVERROR(ENOSYS)) {
			m_CurStream->clocks.vidclk.setPaused(false);
		}
		m_CurStream->clocks.vidclk.set(m_CurStream->clocks.vidclk.get(),
			m_CurStream->clocks.vidclk.serial());
	}
	m_CurStream->clocks.extclk.set(m_CurStream->clocks.extclk.get(),
		m_CurStream->clocks.extclk.serial());
	const int paused = !m_CurStream->session.paused.load(std::memory_order_acquire);
	m_CurStream->clocks.audclk.setPaused(paused != 0);
	m_CurStream->clocks.vidclk.setPaused(paused != 0);
	m_CurStream->clocks.extclk.setPaused(paused != 0);
	m_CurStream->session.paused.store(paused, std::memory_order_release);
}

void VideoCtl::toggle_pause()
{
	std::unique_lock<std::shared_mutex> lock(m_streamMutex);
	if (!m_CurStream) return;
	VideoState* is = m_CurStream;

	stream_toggle_pause();
	is->video.step = 0;
}

void VideoCtl::step_to_next_frame()
{
	std::unique_lock<std::shared_mutex> lock(m_streamMutex);
	if (!m_CurStream) return;
	/* if the stream is paused unpause it, then step */
	if (m_CurStream->session.paused) {
		stream_toggle_pause();
	}
	m_CurStream->video.step = 1;
}

/* called to display each frame */
void VideoCtl::video_refresh(void* opaque, double* remaining_time)
{
	VideoState* is = (VideoState*)opaque;
	double time;

	Frame* sp, * sp2;

	double rdftspeed = 0.02;

	if (!is->session.paused && MediaSync::get_master_sync_type(is) == AV_SYNC_EXTERNAL_CLOCK && is->session.realtime)
		MediaSync::check_external_clock_speed(is);
	if (is->audio.audio_st)
	{
		time = av_gettime_relative() / 1000000.0;
		if (is->session.force_refresh || is->audio.last_vis_time + rdftspeed < time)
		{
			// 显示当前图片（如果有）
			video_display();
			is->audio.last_vis_time = time;
		}
		*remaining_time = FFMIN(*remaining_time, is->audio.last_vis_time + rdftspeed - time);
	}
	if (is->video.video_st) {
	retry:
		if (is->video.pictq.nb_remaining() == 0) {
			// nothing to do, no picture to display in the queue
		}
		else {
			double last_duration, duration, delay;
			Frame* vp, * lastvp;

			/* dequeue the picture */
			lastvp = is->video.pictq.peek_last();
			vp = is->video.pictq.peek();

			if (vp->serial != is->video.videoq.serial.load(std::memory_order_acquire)) {
				is->video.pictq.next();
				goto retry;
			}

			if (lastvp->serial != vp->serial)
				is->video.frame_timer = av_gettime_relative() / 1000000.0;

			if (is->session.paused)
				goto display;

			/* compute nominal last_duration */
			last_duration = MediaSync::vp_duration(is, lastvp, vp);
			delay = MediaSync::compute_target_delay(last_duration, is);
			time = av_gettime_relative() / 1000000.0;
			if (time < is->video.frame_timer + delay) {
				*remaining_time = FFMIN(is->video.frame_timer + delay - time, *remaining_time);
				goto display;
			}

			is->video.frame_timer += delay;
			if (delay > 0 && time - is->video.frame_timer > AV_SYNC_THRESHOLD_MAX)
				is->video.frame_timer = time;

			{
				SDL_LockMutex(is->video.pictq.mutex);
				if (!std::isnan(vp->pts))
					MediaSync::update_video_pts(is, vp->pts, vp->pos, vp->serial);
				SDL_UnlockMutex(is->video.pictq.mutex);
			}

			if (is->video.pictq.nb_remaining() > 1) {
				Frame* nextvp = is->video.pictq.peek_next();
				duration = MediaSync::vp_duration(is, vp, nextvp);
				if (!is->video.step && (framedrop > 0 || (framedrop && MediaSync::get_master_sync_type(is) != AV_SYNC_VIDEO_MASTER)) && time > is->video.frame_timer + duration) {
					is->video.frame_drops_late++;
					is->video.pictq.next();
					goto retry;
				}
			}

			if (is->subtitle.subtitle_st) {
				while (is->subtitle.subpq.nb_remaining() > 0) {
					sp = is->subtitle.subpq.peek();

					if (is->subtitle.subpq.nb_remaining() > 1)
						sp2 = is->subtitle.subpq.peek_next();
					else
						sp2 = NULL;

					if (sp->serial != is->subtitle.subtitleq.serial.load(std::memory_order_acquire)
						|| (is->clocks.vidclk.get() > (sp->pts + ((float)sp->sub.end_display_time / 1000)))
						|| (sp2 && is->clocks.vidclk.get() > (sp2->pts + ((float)sp2->sub.start_display_time / 1000))))
					{
						is->subtitle.subpq.next();
					}
					else {
						break;
					}
				}
			}

			is->video.pictq.next();
			is->session.force_refresh = 1;

			if (is->video.step && !is->session.paused)
				stream_toggle_pause();
		}
	display:
		/* display picture */
		if (is->session.force_refresh && is->video.pictq.rindex_shown)
			video_display();
	}
	is->session.force_refresh = 0;

	SigVideoPlaySeconds(static_cast<int>(MediaSync::get_master_clock(is)));
}

/* open a given stream. Return 0 if OK */
//打开流
int VideoCtl::stream_component_open(VideoState* is, int stream_index)
{
	AVFormatContext* ic = is->session.ic;
	AVCodecContext* avctx;
	const AVCodec* codec;
	const char* forced_codec_name = NULL;
	AVDictionary* opts = NULL;
	const AVDictionaryEntry* t = NULL;
	int sample_rate;
	AVChannelLayout ch_layout;
	memset(&ch_layout, 0, sizeof(AVChannelLayout));
	int ret = 0;
	int stream_lowres = 0;

	if (stream_index < 0 || stream_index >= ic->nb_streams)
		return -1;

	avctx = avcodec_alloc_context3(NULL);
	if (!avctx)
		return AVERROR(ENOMEM);

	ret = avcodec_parameters_to_context(avctx, ic->streams[stream_index]->codecpar);
	if (ret < 0)
		goto fail;
	avctx->pkt_timebase = ic->streams[stream_index]->time_base;

	codec = avcodec_find_decoder(avctx->codec_id);

	switch (avctx->codec_type) {
	case AVMEDIA_TYPE_AUDIO: is->session.last_audio_stream = stream_index; break;
	case AVMEDIA_TYPE_SUBTITLE: is->session.last_subtitle_stream = stream_index; break;
	case AVMEDIA_TYPE_VIDEO: is->session.last_video_stream = stream_index; break;
	}
	if (forced_codec_name)
		codec = avcodec_find_decoder_by_name(forced_codec_name);
	if (!codec) {
		if (forced_codec_name) av_log(NULL, AV_LOG_WARNING,
			"No codec could be found with name '%s'\n", forced_codec_name);
		else                   av_log(NULL, AV_LOG_WARNING,
			"No decoder could be found for codec %s\n", avcodec_get_name(avctx->codec_id));
		ret = AVERROR(EINVAL);
		goto fail;
	}

	avctx->codec_id = codec->id;
	if (stream_lowres > codec->max_lowres) {
		av_log(avctx, AV_LOG_WARNING, "The maximum value for lowres supported by the decoder is %d\n",
			codec->max_lowres);
		stream_lowres = codec->max_lowres;
	}
	avctx->lowres = stream_lowres;

	//if (fast)
	//    avctx->flags2 |= AV_CODEC_FLAG2_FAST;

	opts = nullptr /*filter_codec_opts(codec_opts, avctx->codec_id, ic, ic->streams[stream_index], codec)*/;
	if (!av_dict_get(opts, "threads", NULL, 0))
		av_dict_set(&opts, "threads", "auto", 0);
	if (stream_lowres)
		av_dict_set_int(&opts, "lowres", stream_lowres, 0);
    av_dict_set(&opts, "flags", "+copy_opaque", AV_DICT_MULTIKEY);
	if ((ret = avcodec_open2(avctx, codec, &opts)) < 0) {
		goto fail;
	}
	if ((t = av_dict_get(opts, "", NULL, AV_DICT_IGNORE_SUFFIX))) {
		av_log(NULL, AV_LOG_ERROR, "Option %s not found.\n", t->key);
		ret = AVERROR_OPTION_NOT_FOUND;
		goto fail;
	}

	is->session.eof = 0;
	ic->streams[stream_index]->discard = AVDISCARD_DEFAULT;
	switch (avctx->codec_type) {
	case AVMEDIA_TYPE_AUDIO:
#if CONFIG_AVFILTER
	{
		AVFilterContext* sink;

		is->audio.audio_filter_src.freq = avctx->sample_rate;
		ret = av_channel_layout_copy(&is->audio.audio_filter_src.ch_layout, &avctx->ch_layout);
		if (ret < 0)
			goto fail;
		is->audio.audio_filter_src.fmt = avctx->sample_fmt;
		double currentSpeed;
		{
			std::shared_lock<std::shared_mutex> lock(m_speedMutex);
			currentSpeed = m_fPlaybackSpeed;
		}
		auto afilters = std::format("atempo={:.2f}", currentSpeed);
		if ((ret = ConfigureAudioFilters(is, afilters.c_str(), false)) < 0)
		{
			print_error("configure_audio_filters", ret);
			goto fail;
		}
		sink = is->filters.out_audio_filter;
		sample_rate = av_buffersink_get_sample_rate(sink);
		ret = av_buffersink_get_ch_layout(sink, &ch_layout);
		if (ret < 0)
			goto fail;
	}
#else
		sample_rate = avctx->sample_rate;
		ret = av_channel_layout_copy(&ch_layout, &avctx->ch_layout);
		if (ret < 0)
			goto fail;
#endif
		/* prepare audio output */
		if ((ret = m_audioOutput.Open(is, &ch_layout, sample_rate, &is->audio.audio_tgt)) < 0)
			goto fail;
		is->audio.audio_hw_buf_size = ret;
		is->audio.audio_src = is->audio.audio_tgt;
		is->audio.audio_buf_size = 0;
		is->audio.audio_buf_index = 0;

		/* init averaging filter */
		is->audio.audio_diff_avg_coef = exp(log(0.01) / AUDIO_DIFF_AVG_NB);
		is->audio.audio_diff_avg_count = 0;
		/* since we do not have a precise anough audio FIFO fullness,
		   we correct audio sync only if larger than this threshold */
		is->audio.audio_diff_threshold = (double)(is->audio.audio_hw_buf_size) / is->audio.audio_tgt.bytes_per_sec;

		is->audio.audio_stream = stream_index;
		is->audio.audio_st = ic->streams[stream_index];

		if ((ret = is->audio.aud_decoder.init(avctx, &is->audio.audioq, is->session.continue_read_thread)) < 0)
			goto fail;
		if (is->session.ic->iformat->flags & AVFMT_NOTIMESTAMPS) {
			is->audio.aud_decoder.start_pts = is->audio.audio_st->start_time;
			is->audio.aud_decoder.start_pts_tb = is->audio.audio_st->time_base;
		}

		packet_queue_start(is->audio.aud_decoder.queue);
		is->audio.aud_decoder.decode_thread = std::thread(&DecoderWorkers::Audio, is);

		m_audioOutput.Pause(false);
		break;
	case AVMEDIA_TYPE_VIDEO:
		is->video.video_stream = stream_index;
		is->video.video_st = ic->streams[stream_index];

		if ((ret = is->video.vid_decoder.init(avctx, &is->video.videoq, is->session.continue_read_thread)) < 0)
			goto fail;
		packet_queue_start(is->video.vid_decoder.queue);
		is->video.vid_decoder.decode_thread = std::thread(&DecoderWorkers::Video, is);
		is->session.queue_attachments_req = 1;
		break;
	case AVMEDIA_TYPE_SUBTITLE:
		is->subtitle.subtitle_stream = stream_index;
		is->subtitle.subtitle_st = ic->streams[stream_index];

		if ((ret = is->subtitle.sub_decoder.init(avctx, &is->subtitle.subtitleq, is->session.continue_read_thread)) < 0)
			goto fail;
		packet_queue_start(is->subtitle.sub_decoder.queue);
		is->subtitle.sub_decoder.decode_thread = std::thread(&DecoderWorkers::Subtitle, is);
		break;
	default:
		break;
	}
	goto out;

fail:
	avcodec_free_context(&avctx);
out:
	av_channel_layout_uninit(&ch_layout);
	av_dict_free(&opts);

	return ret;
}

VideoState* VideoCtl::stream_open(const char* filename)
{
	return stream_open(MediaSource{filename ? filename : ""});
}

VideoState* VideoCtl::stream_open(const MediaSource& source)
{
	const double volume = m_volume.load(std::memory_order_acquire);
	const int sdlVolume = NormalizedToSdlVolume(volume);
	VideoState* is;
	//构造视频状态类
	is = new (std::nothrow) VideoState{};
	if (!is)
		return NULL;
	is->session.last_video_stream = is->video.video_stream = -1;
	is->session.last_audio_stream = is->audio.audio_stream = -1;
	is->session.last_subtitle_stream = is->subtitle.subtitle_stream = -1;
	is->session.source = source;

	is->audio.soundTouchHandle = soundtouch_create();
	is->audio.audio_new_buf = NULL;
	is->audio.audio_new_buf_size = 0;
	{
		std::shared_lock<std::shared_mutex> lock(m_speedMutex);
		is->audio.play_rate.store(m_fPlaybackSpeed, std::memory_order_release);
	}
	//视频文件名
	is->session.filename = av_strdup(source.location.c_str());
	if (!is->session.filename)
		goto fail;
	//指定输入格式
	is->video.ytop = 0;
	is->video.xleft = 0;

	/* start video display */
	//初始化视频帧队列
	if (is->video.pictq.init( &is->video.videoq, VIDEO_PICTURE_QUEUE_SIZE, 1) < 0)
		goto fail;
	//初始化字幕帧队列
	if (is->subtitle.subpq.init( &is->subtitle.subtitleq, SUBPICTURE_QUEUE_SIZE, 0) < 0)
		goto fail;
	//初始化音频帧队列
	if (is->audio.sampq.init( &is->audio.audioq, SAMPLE_QUEUE_SIZE, 1) < 0)
		goto fail;
	//初始化队列中的数据包
	if (is->video.videoq.init() < 0 ||
		is->audio.audioq.init() < 0 ||
		is->subtitle.subtitleq.init() < 0)
		goto fail;
	//构建 继续读取线程 信号量
	if (!(is->session.continue_read_thread = SDL_CreateCond())) {
		av_log(NULL, AV_LOG_FATAL, "SDL_CreateCond(): %s\n", SDL_GetError());
		goto fail;
	}
	//视频、音频 时钟
	is->clocks.vidclk.init(&is->video.videoq.serial);
	is->clocks.audclk.init(&is->audio.audioq.serial);
	is->clocks.extclk.init(is->clocks.extclk.serialStorage());
	is->audio.audio_clock_serial = -1;
	is->audio.audio_volume.store(sdlVolume, std::memory_order_release);

	SigVideoVolume(volume);
	SigPauseStat(is->session.paused != 0);

	is->clocks.av_sync_type = AV_SYNC_AUDIO_MASTER;
	//构建读取线程
	is->session.read_tid = std::thread([this, is] {
		m_streamReader.Run(is);
	});

	return is;

fail:
	stream_close(is);
	return NULL;
}

void VideoCtl::stream_cycle_channel(VideoState* is, int codec_type)
{
	AVFormatContext* ic = is->session.ic;
	int start_index, stream_index;
	int old_index;
	AVStream* st;
	AVProgram* p = NULL;
	int nb_streams = is->session.ic->nb_streams;

	if (codec_type == AVMEDIA_TYPE_VIDEO) {
		start_index = is->session.last_video_stream;
		old_index = is->video.video_stream;
	}
	else if (codec_type == AVMEDIA_TYPE_AUDIO) {
		start_index = is->session.last_audio_stream;
		old_index = is->audio.audio_stream;
	}
	else {
		start_index = is->session.last_subtitle_stream;
		old_index = is->subtitle.subtitle_stream;
	}
	stream_index = start_index;

	if (codec_type != AVMEDIA_TYPE_VIDEO && is->video.video_stream != -1) {
		p = av_find_program_from_stream(ic, NULL, is->video.video_stream);
		if (p) {
			nb_streams = p->nb_stream_indexes;
			for (start_index = 0; start_index < nb_streams; start_index++)
				if (p->stream_index[start_index] == stream_index)
					break;
			if (start_index == nb_streams)
				start_index = -1;
			stream_index = start_index;
		}
	}

	for (;;) {
		if (++stream_index >= nb_streams)
		{
			if (codec_type == AVMEDIA_TYPE_SUBTITLE)
			{
				stream_index = -1;
				is->session.last_subtitle_stream = -1;
				goto the_end;
			}
			if (start_index == -1)
				return;
			stream_index = 0;
		}
		if (stream_index == start_index)
			return;
		st = is->session.ic->streams[p ? p->stream_index[stream_index] : stream_index];
		if (st->codecpar->codec_type == codec_type) {
			/* check that parameters are OK */
			switch (codec_type) {
			case AVMEDIA_TYPE_AUDIO:
				if (st->codecpar->sample_rate != 0 &&
					st->codecpar->ch_layout.nb_channels != 0)
					goto the_end;
				break;
			case AVMEDIA_TYPE_VIDEO:
			case AVMEDIA_TYPE_SUBTITLE:
				goto the_end;
			default:
				break;
			}
		}
	}
the_end:
	if (p && stream_index != -1)
		stream_index = p->stream_index[stream_index];
	av_log(NULL, AV_LOG_INFO, "Switch %s stream from #%d to #%d\n",
		av_get_media_type_string((AVMediaType)codec_type),
		old_index,
		stream_index);

	stream_component_close(is, old_index);
	stream_component_open(is, stream_index);
}


void VideoCtl::refresh_loop_wait_event(VideoState* is) {
	double remaining_time = REFRESH_RATE;
	applyPlaybackCommands();
	if (is && is->session.stop_refresh_loop.exchange(false, std::memory_order_acq_rel)) {
		m_bPlayLoop.store(false, std::memory_order_release);
		return;
	}
	if (is && (!is->session.paused || is->session.force_refresh))
		video_refresh(is, &remaining_time);
	if (remaining_time > 0.0)
		av_usleep(static_cast<int64_t>(remaining_time * 1000000.0));
}

void VideoCtl::notifyPlaybackCommand()
{
	std::shared_lock<std::shared_mutex> lock(m_streamMutex);
	if (m_CurStream && m_CurStream->session.continue_read_thread)
		SDL_CondSignal(m_CurStream->session.continue_read_thread);
}

void VideoCtl::applyTrackCommand(VideoState* state, const TrackCommand& command)
{
	if (!state)
		return;
	std::unique_lock<std::shared_mutex> trackLock(state->session.trackMutex);
	if (!state->session.ic)
		return;

	const int mediaType = command.kind == TrackKind::Audio ? AVMEDIA_TYPE_AUDIO : AVMEDIA_TYPE_SUBTITLE;
	if (command.cycle) {
		stream_cycle_channel(state, mediaType);
		return;
	}
	if (!command.streamIndex || *command.streamIndex < 0 ||
		*command.streamIndex >= static_cast<int>(state->session.ic->nb_streams) ||
		state->session.ic->streams[*command.streamIndex]->codecpar->codec_type != mediaType)
		return;

	const int current = mediaType == AVMEDIA_TYPE_AUDIO ? state->audio.audio_stream : state->subtitle.subtitle_stream;
	if (current == *command.streamIndex)
		return;
	if (current >= 0)
		stream_component_close(state, current);
	stream_component_open(state, *command.streamIndex);
}

void VideoCtl::applyPlaybackCommands()
{
	const PlaybackCommands commands = m_commandMailbox.take();
	if (commands.pauseToggleCount % 2 != 0)
		toggle_pause();

	if (commands.seek) {
		std::shared_lock<std::shared_mutex> lock(m_streamMutex);
		if (m_CurStream)
			stream_seek(commands.seek->position, commands.seek->relative);
	}

	if (!commands.tracks.empty()) {
		std::unique_lock<std::shared_mutex> lock(m_streamMutex);
		for (const auto& command : commands.tracks)
			applyTrackCommand(m_CurStream, command);
	}

	if (commands.pauseToggleCount % 2 != 0) {
		std::shared_lock<std::shared_mutex> lock(m_streamMutex);
		if (m_CurStream)
			SigPauseStat(m_CurStream->session.paused != 0);
	}
}

void VideoCtl::seek_chapter(VideoState* is, int incr)
{
	if (!is) return;

	int64_t pos = MediaSync::get_master_clock(is) * AV_TIME_BASE;
	int i;

	if (!is->session.ic->nb_chapters)
		return;

	/* find the current chapter */
	for (i = 0; i < is->session.ic->nb_chapters; i++) {
		AVChapter* ch = is->session.ic->chapters[i];
		if (av_compare_ts(pos, /*AV_TIME_BASE_Q*/{ 1, AV_TIME_BASE }, ch->start, ch->time_base) < 0) {
			i--;
			break;
		}
	}

	i += incr;
	i = FFMAX(i, 0);
	if (i >= is->session.ic->nb_chapters)
		return;

	av_log(NULL, AV_LOG_VERBOSE, "Seeking to chapter %d.\n", i);
	stream_seek(av_rescale_q(is->session.ic->chapters[i]->start, is->session.ic->chapters[i]->time_base,
		/*AV_TIME_BASE_Q*/{ 1, AV_TIME_BASE }), 0);
}

//播放控制循环
void VideoCtl::LoopThread()
{
	int reconnectAttempt = 0;
	VideoState* exitStream = nullptr;

	for (;;) {
		while (m_bPlayLoop.load(std::memory_order_acquire))
		{
			VideoState* is;
			{
				std::shared_lock<std::shared_mutex> lock(m_streamMutex);
				is = m_CurStream;
			}
			refresh_loop_wait_event(is);
		}

		exitStream = nullptr;
		{
			std::shared_lock<std::shared_mutex> lock(m_streamMutex);
			exitStream = m_CurStream;
		}
		if (!exitStream)
			return;

		const auto source = exitStream->session.source;
		const auto error = exitStream->session.readError.load(std::memory_order_acquire);
		const bool canReconnect = m_reconnectController.canRetry(
			source,
			error,
			reconnectAttempt,
			exitStream->session.abort_request.load(std::memory_order_acquire));
		if (!canReconnect)
			break;

		++reconnectAttempt;
		const auto delay = ReconnectDelay(source.network, reconnectAttempt);
		SigPlaybackStatus(PlaybackStatus{PlaybackState::Reconnecting, error, reconnectAttempt, source.network.maxReconnectAttempts, delay, RedactMediaLocation(source.location)});

		{
			m_commandMailbox.clear();
			std::unique_lock<std::shared_mutex> lock(m_streamMutex);
			exitStream = m_mediaSession.release();
			m_CurStream = nullptr;
		}
		stream_close(exitStream);

		if (!m_reconnectController.wait(delay))
			return;

		VideoState* reopened = stream_open(source);
		if (!reopened) {
			SigPlaybackStatus(PlaybackStatus{PlaybackState::Failed, PlaybackError::Unknown, reconnectAttempt, source.network.maxReconnectAttempts, {}, RedactMediaLocation(source.location)});
			return;
		}
		const bool installed = m_reconnectController.runIfNotCancelled([&] {
			std::unique_lock<std::shared_mutex> streamLock(m_streamMutex);
			m_mediaSession.reset(reopened);
			m_CurStream = m_mediaSession.get();
			m_bPlayLoop.store(true, std::memory_order_release);
		});
		if (!installed) {
			stream_close(reopened);
			return;
		}
	}

	if (exitStream)
		do_exit();
}


void VideoCtl::OnPlaySeek(double dPercent)
{
	std::shared_lock<std::shared_mutex> lock(m_streamMutex);
	if (!m_CurStream || !m_CurStream->session.ic || !CanSeek(m_CurStream->session.mediaInfo))
		return;
	int64_t ts = dPercent * m_CurStream->session.ic->duration;
	if (m_CurStream->session.ic->start_time != AV_NOPTS_VALUE)
		ts += m_CurStream->session.ic->start_time;
	lock.unlock();
	m_commandMailbox.postSeek({ts, 0, 0});
	notifyPlaybackCommand();
}

void VideoCtl::OnPlaySeekSeconds(int seconds)
{
	std::shared_lock<std::shared_mutex> lock(m_streamMutex);
	if (!m_CurStream || !m_CurStream->session.ic || !CanSeek(m_CurStream->session.mediaInfo))
		return;
	int64_t ts = static_cast<int64_t>(seconds) * AV_TIME_BASE;
	if (m_CurStream->session.ic->start_time != AV_NOPTS_VALUE)
		ts += m_CurStream->session.ic->start_time;
	lock.unlock();
	m_commandMailbox.postSeek({ts, 0, 0});
	notifyPlaybackCommand();
}

void VideoCtl::OnPlayVolume(double dPercent)
{
	const double volume = PlaybackSettings::NormalizeVolume(dPercent);
	m_volume.store(volume, std::memory_order_release);

	std::shared_lock<std::shared_mutex> lock(m_streamMutex);
	if (m_CurStream)
		m_CurStream->audio.audio_volume.store(NormalizedToSdlVolume(volume), std::memory_order_release);
}

void VideoCtl::OnSeekForward()
{
	std::shared_lock<std::shared_mutex> lock(m_streamMutex);
	if (!m_CurStream || !m_CurStream->session.ic)
		return;
	double incr = 5.0;
	double pos = MediaSync::get_master_clock(m_CurStream);
	if (std::isnan(pos))
		pos = (double)m_CurStream->session.seek_pos / AV_TIME_BASE;
	pos += incr;
	if (m_CurStream->session.ic->start_time != AV_NOPTS_VALUE && pos < m_CurStream->session.ic->start_time / (double)AV_TIME_BASE)
		pos = m_CurStream->session.ic->start_time / (double)AV_TIME_BASE;
	lock.unlock();
	m_commandMailbox.postSeek({static_cast<int64_t>(pos * AV_TIME_BASE), static_cast<int64_t>(incr * AV_TIME_BASE), 0});
	notifyPlaybackCommand();
}

void VideoCtl::OnSeekBack()
{
	std::shared_lock<std::shared_mutex> lock(m_streamMutex);
	if (!m_CurStream || !m_CurStream->session.ic)
		return;
	double incr = -5.0;
	double pos = MediaSync::get_master_clock(m_CurStream);
	if (std::isnan(pos))
		pos = (double)m_CurStream->session.seek_pos / AV_TIME_BASE;
	pos += incr;
	if (m_CurStream->session.ic->start_time != AV_NOPTS_VALUE && pos < m_CurStream->session.ic->start_time / (double)AV_TIME_BASE)
		pos = m_CurStream->session.ic->start_time / (double)AV_TIME_BASE;
	lock.unlock();
	m_commandMailbox.postSeek({static_cast<int64_t>(pos * AV_TIME_BASE), static_cast<int64_t>(incr * AV_TIME_BASE), 0});
	notifyPlaybackCommand();
}

void VideoCtl::UpdateVolume(int sign, double step)
{
	double normalizedVolume = 0.0;
	{
		std::shared_lock<std::shared_mutex> lock(m_streamMutex);
		if (!m_CurStream)
			return;

		const int currentVolume = m_CurStream->audio.audio_volume.load(std::memory_order_acquire);
		const double volumeLevel = currentVolume
			? (20 * log(currentVolume / static_cast<double>(SDL_MIX_MAXVOLUME)) / log(10))
			: -1000.0;
		const int requestedVolume = lrint(SDL_MIX_MAXVOLUME * pow(10.0, (volumeLevel + sign * step) / 20.0));
		const int updatedVolume = av_clip(currentVolume == requestedVolume ? currentVolume + sign : requestedVolume, 0, SDL_MIX_MAXVOLUME);
		m_CurStream->audio.audio_volume.store(updatedVolume, std::memory_order_release);
		normalizedVolume = updatedVolume / static_cast<double>(SDL_MIX_MAXVOLUME);
		m_volume.store(normalizedVolume, std::memory_order_release);
	}

	SigVideoVolume(normalizedVolume);
}

/* display the current picture, if any */
void VideoCtl::video_display()
{
	std::shared_lock<std::shared_mutex> lock(m_streamMutex);
	if (!m_CurStream) return;
	emit_video_frame(m_CurStream);
}

void VideoCtl::emit_video_frame(VideoState* is)
{
	if (!is)
		return;

	Frame* vp = is->video.pictq.peek_last();
	if (!vp || !vp->frame || vp->frame->width <= 0 || vp->frame->height <= 0)
		return;

	auto frame = m_frameConverter.convert(vp->frame);
	if (!frame)
	{
		av_log(NULL, AV_LOG_ERROR, "Cannot convert video frame\n");
		return;
	}

	m_rendererDispatcher.dispatchFrame(frame);
}

void VideoCtl::do_exit()
{
	VideoState* is = nullptr;
	{
		std::unique_lock<std::shared_mutex> lock(m_streamMutex);
		is = m_mediaSession.release();
		m_CurStream = nullptr;
	}

	if (!is) return;

	stream_close(is);
	SigStopFinished();
}

void VideoCtl::OnAddVolume()
{
	UpdateVolume(1, SDL_VOLUME_STEP);
}

void VideoCtl::OnSubVolume()
{
	UpdateVolume(-1, SDL_VOLUME_STEP);
}

void VideoCtl::OnPause()
{
	m_commandMailbox.postPauseToggle();
	notifyPlaybackCommand();
}

void VideoCtl::requestStop()
{
	m_commandMailbox.clear();
	m_reconnectController.cancel();
	m_bPlayLoop.store(false, std::memory_order_release);

	std::shared_lock<std::shared_mutex> lock(m_streamMutex);
	if (!m_CurStream)
		return;

	m_CurStream->session.abort_request.store(1, std::memory_order_release);
	m_CurStream->session.io.cancelled.store(true, std::memory_order_release);
	if (m_CurStream->session.continue_read_thread)
		SDL_CondSignal(m_CurStream->session.continue_read_thread);
}

void VideoCtl::OnStop()
{
	PlaybackStatus status;
	bool hasStatus = false;
	{
		std::shared_lock<std::shared_mutex> lock(m_streamMutex);
		if (m_CurStream) {
			status = PlaybackStatus{PlaybackState::Stopped, PlaybackError::Cancelled, 0,
				m_CurStream->session.source.network.maxReconnectAttempts, {},
				RedactMediaLocation(m_CurStream->session.source.location)};
			hasStatus = true;
		}
	}

	requestStop();
	if (hasStatus)
		SigPlaybackStatus(status);
}

void VideoCtl::OnStopAndWait()
{
	std::lock_guard<std::mutex> lock(m_playbackMutex);
	requestStop();
	if (m_tPlayLoopThread.joinable())
		m_tPlayLoopThread.join();
}

void VideoCtl::OnCycleAudioTrack()
{
	m_commandMailbox.postTrack({TrackKind::Audio, std::nullopt, true});
	notifyPlaybackCommand();
}

void VideoCtl::OnCycleSubtitleTrack()
{
	m_commandMailbox.postTrack({TrackKind::Subtitle, std::nullopt, true});
	notifyPlaybackCommand();
}

VideoCtl::VideoCtl() :
	m_CurStream(nullptr),
	m_bPlayLoop(false),
	m_streamReader(StreamReaderCallbacks{
		[this](VideoState* state, int streamIndex) {
			std::unique_lock<std::shared_mutex> trackLock(state->session.trackMutex);
			return stream_component_open(state, streamIndex);
		},
		[this](const PlaybackStatus& status) {
			SigPlaybackStatus(status);
		},
		[this](int seconds) {
			SigVideoTotalSeconds(seconds);
		},
		[this](const MediaInfo& info) {
			SigMediaInfo(info);
		},
		[this] {
			m_bPlayLoop.store(false, std::memory_order_release);
		},
		[this] {
			const auto loopPolicy = m_loopPolicy.load(std::memory_order_acquire);
			if (loopPolicy == VideoLoopPolicy::LOOP_ALL) {
				m_bPlayLoop.store(false, std::memory_order_release);
				std::this_thread::sleep_for(std::chrono::milliseconds(10));
				SigPlayNextOne();
			}
			else if (loopPolicy == VideoLoopPolicy::LOOP_SINGLE) {
				stream_seek(0, 0);
			}
			else if (loopPolicy == VideoLoopPolicy::LOOP_RANDOM) {
				m_bPlayLoop.store(false, std::memory_order_release);
				std::this_thread::sleep_for(std::chrono::milliseconds(10));
				SigRandomPlayOne();
			}
			else {
				SigStop();
			}
		},
	})
{
	m_mediaSession.setCloseCallback([this](VideoState* state) {
		stream_close(state);
	});
	m_rendererDispatcher.setFrameDimensionsChangedCallback([this](int width, int height) {
		SigFrameDimensionsChanged(width, height);
	});
	m_rendererDispatcher.setVideoFrameCallback([this](std::shared_ptr<VideoFrame> frame) {
		SigVideoFrame(std::move(frame));
	});
	avdevice_register_all();
}

bool VideoCtl::Init()
{
	if (ConnectSignalSlots() == false)
		return false;

	auto& runtimeManager = RuntimeManager::instance();
	if (!runtimeManager.acquireNetwork())
		return false;
	m_hasNetworkInitRef = true;

	if (!runtimeManager.acquireSdl()) {
		runtimeManager.releaseNetwork();
		m_hasNetworkInitRef = false;
		return false;
	}
	m_hasSdlInitRef = true;
	return true;
}

bool VideoCtl::ConnectSignalSlots()
{
	SigStop.connect([this]() { OnStop(); });

	return true;
}


std::shared_ptr<VideoCtl> VideoCtl::MakeInstance() {
	auto p = std::shared_ptr<VideoCtl>(new VideoCtl());
	if (p->Init()) {
		return p;
	}
	return nullptr;
}

VideoCtl::~VideoCtl()
{
	std::lock_guard<std::mutex> lock(m_playbackMutex);
	requestStop();
	if (m_tPlayLoopThread.joinable())
		m_tPlayLoopThread.join();

	VideoState* state = nullptr;
	{
		std::unique_lock<std::shared_mutex> streamLock(m_streamMutex);
		state = m_mediaSession.release();
		m_CurStream = nullptr;
	}
	if (state)
		stream_close(state);

	auto& runtimeManager = RuntimeManager::instance();
	if (m_hasSdlInitRef)
		runtimeManager.releaseSdl();
	if (m_hasNetworkInitRef)
		runtimeManager.releaseNetwork();
}

bool VideoCtl::StartPlay(const std::string& strFileName)
{
    return StartPlay(MediaSource{strFileName});
}

bool VideoCtl::StartPlay(const MediaSource& source)
{
	std::lock_guard<std::mutex> lock(m_playbackMutex);

    // 检查输入参数
    const auto validation = ValidateMediaSource(source);
    if (!validation.ok) {
        av_log(NULL, AV_LOG_ERROR, "File name is empty, cannot start playback!\n");
		SigPlaybackStatus(PlaybackStatus{PlaybackState::Failed, PlaybackError::InvalidMedia, 0, source.network.maxReconnectAttempts, {}, validation.message});
        return false;
    }

    requestStop();
        if (m_tPlayLoopThread.joinable())
    {
        m_tPlayLoopThread.join();
    }
	{
		m_reconnectController.reset();
		m_bPlayLoop.store(true, std::memory_order_release);
	}
	SigPlaybackStatus(PlaybackStatus{PlaybackState::Opening, PlaybackError::None, 0, source.network.maxReconnectAttempts, {}, RedactMediaLocation(source.location)});
    SigStartPlay(source.location);//正式播放，发送给标题栏

    VideoState* is;

    //打开流
    is = stream_open(source);
    if (!is) {
        av_log(NULL, AV_LOG_FATAL, "Failed to initialize VideoState!\n");
		SigPlaybackStatus(PlaybackStatus{PlaybackState::Failed, PlaybackError::Unknown, 0, source.network.maxReconnectAttempts, {}, RedactMediaLocation(source.location)});
        return false;
    }

    {
        std::unique_lock<std::shared_mutex> lock(m_streamMutex);
        m_mediaSession.reset(is);
        m_CurStream = m_mediaSession.get();
    }

    //事件循环
    m_tPlayLoopThread = std::thread(&VideoCtl::LoopThread, this);

    return true;
}
