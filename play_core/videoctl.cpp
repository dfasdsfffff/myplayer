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

/* prepare a new audio buffer */
static void sdl_audio_callback(void* opaque, Uint8* stream, int len)
{
	VideoState* is = (VideoState*)opaque;
	int audio_size, len1;
	const auto callbackTime = av_gettime_relative();

	while (len > 0) {
		if (is->audio.audio_buf_index >= is->audio.audio_buf_size) {
			audio_size = VideoCtl::audio_decode_frame(is);  // 直接调用static函数
			if (audio_size < 0) {
				/* if error, just output silence */
				is->audio.audio_buf = NULL;
				is->audio.audio_buf_size = SDL_AUDIO_MIN_BUFFER_SIZE / is->audio.audio_tgt.frame_size * is->audio.audio_tgt.frame_size;
			}
			else {
				is->audio.audio_buf_size = audio_size;
			}
			is->audio.audio_buf_index = 0;
		}
		len1 = is->audio.audio_buf_size - is->audio.audio_buf_index;
		if (len1 > len)
			len1 = len;
		const auto audioVolume = is->audio.audio_volume.load(std::memory_order_relaxed);
		if (is->audio.audio_buf && audioVolume == SDL_MIX_MAXVOLUME)
			memcpy(stream, (uint8_t*)is->audio.audio_buf + is->audio.audio_buf_index, len1);
		else {
			memset(stream, 0, len1);
			if (is->audio.audio_buf)
				SDL_MixAudio(stream, (uint8_t*)is->audio.audio_buf + is->audio.audio_buf_index, len1, audioVolume);
		}
		len -= len1;
		stream += len1;
		is->audio.audio_buf_index += len1;
	}
	is->audio.audio_write_buf_size = is->audio.audio_buf_size - is->audio.audio_buf_index;
	/* Let's assume the audio driver that is used by SDL has two periods. */
	if (!std::isnan(is->audio.audio_clock)) {
		is->clocks.audclk.set_at(
			is->audio.audio_clock - (double)(2 * is->audio.audio_hw_buf_size + is->audio.audio_write_buf_size) / is->audio.audio_tgt.bytes_per_sec,
			is->audio.audio_clock_serial,
			callbackTime / 1000000.0);
		is->clocks.extclk.sync_to_slave(is->clocks.audclk);
	}
}

static int decode_interrupt_cb(void* ctx)
{
	VideoState* is = (VideoState*)ctx;
	return is->session.abort_request || InterruptNetworkIo(&is->session.io);
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
		if (m_sdlAudio_dev) {
			SDL_CloseAudioDevice(m_sdlAudio_dev);
			m_sdlAudio_dev = 0;
		}
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
	if (m_sdlAudio_dev) {
		SDL_CloseAudioDevice(m_sdlAudio_dev);
		m_sdlAudio_dev = 0;
	}
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
		m_CurStream->video.frame_timer += av_gettime_relative() / 1000000.0 - m_CurStream->clocks.vidclk.last_updated;
		if (m_CurStream->session.read_pause_return != AVERROR(ENOSYS)) {
			m_CurStream->clocks.vidclk.paused = 0;
		}
		m_CurStream->clocks.vidclk.set(m_CurStream->clocks.vidclk.get(),
			m_CurStream->clocks.vidclk.serial.load(std::memory_order_acquire));
	}
	m_CurStream->clocks.extclk.set(m_CurStream->clocks.extclk.get(),
		m_CurStream->clocks.extclk.serial.load(std::memory_order_acquire));
	const int paused = !m_CurStream->session.paused.load(std::memory_order_acquire);
	m_CurStream->clocks.audclk.paused = paused;
	m_CurStream->clocks.vidclk.paused = paused;
	m_CurStream->clocks.extclk.paused = paused;
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
						|| (is->clocks.vidclk.pts > (sp->pts + ((float)sp->sub.end_display_time / 1000)))
						|| (sp2 && is->clocks.vidclk.pts > (sp2->pts + ((float)sp2->sub.start_display_time / 1000))))
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

/* copy samples for viewing in editor window */
void VideoCtl::update_sample_display(VideoState* is, short* samples, int samples_size)
{
	int size, len;

	size = samples_size / sizeof(short);
	while (size > 0) {
		len = SAMPLE_ARRAY_SIZE - is->audio.sample_array_index;
		if (len > size)
			len = size;
		memcpy(is->audio.sample_array + is->audio.sample_array_index, samples, len * sizeof(short));
		samples += len;
		is->audio.sample_array_index += len;
		if (is->audio.sample_array_index >= SAMPLE_ARRAY_SIZE)
			is->audio.sample_array_index = 0;
		size -= len;
	}
}

/* return the wanted number of samples to get better sync if sync_type is video or external master clock */
int VideoCtl::synchronize_audio(VideoState* is, int nb_samples)
{
	int wanted_nb_samples = nb_samples;

	/* 如果不是master，那么我们会尝试删除或添加样本来纠正时钟 */
	if (MediaSync::get_master_sync_type(is) != AV_SYNC_AUDIO_MASTER) {
		double diff, avg_diff;
		int min_nb_samples, max_nb_samples;

		diff = is->clocks.audclk.get() - MediaSync::get_master_clock(is);

		if (!std::isnan(diff) && fabs(diff) < AV_NOSYNC_THRESHOLD) {
			is->audio.audio_diff_cum = diff + is->audio.audio_diff_avg_coef * is->audio.audio_diff_cum;
			if (is->audio.audio_diff_avg_count < AUDIO_DIFF_AVG_NB) {
				/* 没有足够的措施来做出正确的估计 */
				is->audio.audio_diff_avg_count++;
			}
			else {
				/* 估算A-V差异 */
				avg_diff = is->audio.audio_diff_cum * (1.0 - is->audio.audio_diff_avg_coef);

				if (fabs(avg_diff) >= is->audio.audio_diff_threshold) {
					wanted_nb_samples = nb_samples + (int)(diff * is->audio.audio_src.freq);
					min_nb_samples = ((nb_samples * (100 - SAMPLE_CORRECTION_PERCENT_MAX) / 100));
					max_nb_samples = ((nb_samples * (100 + SAMPLE_CORRECTION_PERCENT_MAX) / 100));
					wanted_nb_samples = av_clip(wanted_nb_samples, min_nb_samples, max_nb_samples);
				}
				av_log(NULL, AV_LOG_TRACE, "diff=%f adiff=%f sample_diff=%d apts=%0.3f %f\n",
					diff, avg_diff, wanted_nb_samples - nb_samples,
					is->audio.audio_clock, is->audio.audio_diff_threshold);
			}
		}
		else {
			/* 差异太大：可能是初始的 PTS 错误，因此需要重置音频-视频滤波器 */
			is->audio.audio_diff_avg_count = 0;
			is->audio.audio_diff_cum = 0;
		}
	}

	return  wanted_nb_samples;
}

/**
* Decode one audio frame and return its uncompressed size.
*
* The processed audio frame is decoded, converted if required, and
* stored in is->audio.audio_buf, with size in bytes given by the return
* value.
*/
int VideoCtl::audio_decode_frame(VideoState* is)
{
	int data_size, resampled_data_size;
	av_unused double audio_clock0;
	int wanted_nb_samples;
	Frame* af;
	int translate_time = 1;
	if (is->session.paused.load(std::memory_order_acquire))
		return -1;
reload:
	do {
#if defined(_WIN32)
		// 使用条件变量等待，替代忙等待
		while (is->audio.sampq.nb_remaining() == 0 && !is->audio.audioq.abort_request.load(std::memory_order_acquire)) {
			const auto waitStarted = av_gettime_relative();
			is->audio.sampq.wait_readable_for(100); // 100ms超时
			if ((av_gettime_relative() - waitStarted) > 1000000LL * is->audio.audio_hw_buf_size / is->audio.audio_tgt.bytes_per_sec / 2)
				return -1;
		}

		if (is->audio.audioq.abort_request.load(std::memory_order_acquire))
			return -1;
#endif
		if (!(af = is->audio.sampq.peek_readable()))
			return -1;
		is->audio.sampq.next();
	} while (af->serial != is->audio.audioq.serial.load(std::memory_order_acquire));
	// 根据frame中指定的音频参数获取缓冲区的大小 af->frame->channels * af->frame->nb_samples * 2
	data_size = av_samples_get_buffer_size(NULL, af->frame->ch_layout.nb_channels,
		af->frame->nb_samples,
		(AVSampleFormat)af->frame->format, 1);

	wanted_nb_samples = VideoCtl::synchronize_audio(is, af->frame->nb_samples);

	if (af->frame->format != is->audio.audio_src.fmt ||
		av_channel_layout_compare(&af->frame->ch_layout, &is->audio.audio_src.ch_layout) ||
		af->frame->sample_rate != is->audio.audio_src.freq ||
		(wanted_nb_samples != af->frame->nb_samples && !is->audio.swr_ctx)) {
		swr_free(&is->audio.swr_ctx);
		swr_alloc_set_opts2(&is->audio.swr_ctx,
			&is->audio.audio_tgt.ch_layout, is->audio.audio_tgt.fmt, is->audio.audio_tgt.freq,
			&af->frame->ch_layout, (AVSampleFormat)af->frame->format, af->frame->sample_rate,
			0, NULL);
		if (!is->audio.swr_ctx || swr_init(is->audio.swr_ctx) < 0) {
			av_log(NULL, AV_LOG_ERROR,
				"Cannot create sample rate converter for conversion of %d Hz %s %d channels to %d Hz %s %d channels!\n",
				af->frame->sample_rate, av_get_sample_fmt_name((AVSampleFormat)af->frame->format), af->frame->ch_layout.nb_channels,
				is->audio.audio_tgt.freq, av_get_sample_fmt_name(is->audio.audio_tgt.fmt), is->audio.audio_tgt.ch_layout.nb_channels);
			swr_free(&is->audio.swr_ctx);
			return -1;
		}
		if (av_channel_layout_copy(&is->audio.audio_src.ch_layout, &af->frame->ch_layout) < 0)
			return -1;
		is->audio.audio_src.freq = af->frame->sample_rate;
		is->audio.audio_src.fmt = (AVSampleFormat)af->frame->format;
	}

	if (is->audio.swr_ctx) {
		const uint8_t** in = (const uint8_t**)af->frame->extended_data;
		uint8_t** out = &is->audio.audio_buf1;
		int out_count = (int64_t)wanted_nb_samples * is->audio.audio_tgt.freq / af->frame->sample_rate + 256;
		int out_size = av_samples_get_buffer_size(NULL, is->audio.audio_tgt.ch_layout.nb_channels, out_count, is->audio.audio_tgt.fmt, 0);
		int len2;
		if (out_size < 0) {
			av_log(NULL, AV_LOG_ERROR, "av_samples_get_buffer_size() failed\n");
			return -1;
		}
		if (wanted_nb_samples != af->frame->nb_samples) {
			if (swr_set_compensation(is->audio.swr_ctx, (wanted_nb_samples - af->frame->nb_samples) * is->audio.audio_tgt.freq / af->frame->sample_rate,
				wanted_nb_samples * is->audio.audio_tgt.freq / af->frame->sample_rate) < 0) {
				av_log(NULL, AV_LOG_ERROR, "swr_set_compensation() failed\n");
				return -1;
			}
		}		av_fast_malloc(&is->audio.audio_buf1, &is->audio.audio_buf1_size, out_size);
		if (!is->audio.audio_buf1)
			return AVERROR(ENOMEM);
		len2 = swr_convert(is->audio.swr_ctx, out, out_count, in, af->frame->nb_samples);
		if (len2 < 0) {
			av_log(NULL, AV_LOG_ERROR, "swr_convert() failed\n");
			return -1;
		}
		if (len2 == out_count) {
			av_log(NULL, AV_LOG_WARNING, "audio buffer is probably too small\n");
			if (swr_init(is->audio.swr_ctx) < 0)
				swr_free(&is->audio.swr_ctx);
		}
		is->audio.audio_buf = is->audio.audio_buf1;
		resampled_data_size = len2 * is->audio.audio_tgt.ch_layout.nb_channels * av_get_bytes_per_sample(is->audio.audio_tgt.fmt);
		//=====================倍速处理 begin==========================
		int bytes_per_sample = av_get_bytes_per_sample(is->audio.audio_tgt.fmt);
		const auto playbackRate = is->audio.play_rate.load(std::memory_order_acquire);
		if (is->audio.soundTouchHandle && playbackRate != 1.0 && !is->session.abort_request.load(std::memory_order_acquire))
		{
			av_fast_malloc(&is->audio.audio_new_buf, &is->audio.audio_new_buf_size, out_size * translate_time);
			if (!is->audio.audio_new_buf) {
				// Allocation failed; buf already freed
				return AVERROR(ENOMEM);
			}
			// 将音频数据转换为SoundTouch库需要的格式（把每两个uint8_t转为short）
			for (int i = 0; i < (resampled_data_size / 2); i++)
			{
				is->audio.audio_new_buf[i] = (is->audio.audio_buf1[i * 2] | (is->audio.audio_buf1[i * 2 + 1] << 8));
			}
			int ret_len = soundtouch_translate(is->audio.soundTouchHandle,
				is->audio.audio_new_buf, // input
				static_cast<float>(playbackRate), // speed
				static_cast<float>(1.0 / playbackRate),// pitch
				resampled_data_size / 2,
				bytes_per_sample, 
				is->audio.audio_tgt.ch_layout.nb_channels,
				af->frame->sample_rate);
			if (ret_len > 0) {
				is->audio.audio_buf = (uint8_t*)is->audio.audio_new_buf;
				resampled_data_size = ret_len;
			}
			else {
				translate_time++;
				goto reload;
			}
		}
	}
	else {
		is->audio.audio_buf = af->frame->data[0];
		resampled_data_size = data_size;
	}

	audio_clock0 = is->audio.audio_clock;
	/* update the audio clock with the pts */
	if (!isnan(af->pts))
		is->audio.audio_clock = af->pts + (double)af->frame->nb_samples / af->frame->sample_rate;
	else
		is->audio.audio_clock = NAN;
	is->audio.audio_clock_serial = af->serial;
	return resampled_data_size;
}

int VideoCtl::audio_open(void* opaque, AVChannelLayout* wanted_channel_layout, int wanted_sample_rate, struct AudioParams* audio_hw_params)
{
	SDL_AudioSpec wanted_spec, spec;
	const char* env;
	static const int next_nb_channels[] = { 0, 0, 1, 6, 2, 6, 4, 6 };
	static const int next_sample_rates[] = { 0, 44100, 48000, 96000, 192000 };
	int next_sample_rate_idx = FF_ARRAY_ELEMS(next_sample_rates) - 1;
	int wanted_nb_channels = wanted_channel_layout->nb_channels;

	env = SDL_getenv("SDL_AUDIO_CHANNELS");
	if (env) {
		wanted_nb_channels = atoi(env);
		av_channel_layout_uninit(wanted_channel_layout);
		av_channel_layout_default(wanted_channel_layout, wanted_nb_channels);
	}
	if (wanted_channel_layout->order != AV_CHANNEL_ORDER_NATIVE) {
		av_channel_layout_uninit(wanted_channel_layout);
		av_channel_layout_default(wanted_channel_layout, wanted_nb_channels);
	}
	wanted_nb_channels = wanted_channel_layout->nb_channels;
	wanted_spec.channels = wanted_nb_channels;
	wanted_spec.freq = wanted_sample_rate;
	if (wanted_spec.freq <= 0 || wanted_spec.channels <= 0) {
		av_log(NULL, AV_LOG_ERROR, "Invalid sample rate or channel count!\n");
		return -1;
	}
	while (next_sample_rate_idx && next_sample_rates[next_sample_rate_idx] >= wanted_spec.freq)
		next_sample_rate_idx--;
	wanted_spec.format = AUDIO_S16SYS;
	wanted_spec.silence = 0;
	wanted_spec.samples = FFMAX(SDL_AUDIO_MIN_BUFFER_SIZE, 2 << av_log2(wanted_spec.freq / SDL_AUDIO_MAX_CALLBACKS_PER_SEC));
	wanted_spec.callback = sdl_audio_callback;
	wanted_spec.userdata = opaque;
	while (!(m_sdlAudio_dev = SDL_OpenAudioDevice(NULL, 0, &wanted_spec, &spec, SDL_AUDIO_ALLOW_FREQUENCY_CHANGE | SDL_AUDIO_ALLOW_CHANNELS_CHANGE))) {
		av_log(NULL, AV_LOG_WARNING, "SDL_OpenAudio (%d channels, %d Hz): %s\n",
			wanted_spec.channels, wanted_spec.freq, SDL_GetError());
		wanted_spec.channels = next_nb_channels[FFMIN(7, wanted_spec.channels)];
		if (!wanted_spec.channels) {
			wanted_spec.freq = next_sample_rates[next_sample_rate_idx--];
			wanted_spec.channels = wanted_nb_channels;
			if (!wanted_spec.freq) {
				av_log(NULL, AV_LOG_ERROR,
					"No more combinations to try, audio open failed\n");
				return -1;
			}
		}
		av_channel_layout_default(wanted_channel_layout, wanted_spec.channels);
	}
	if (spec.format != AUDIO_S16SYS) {
		av_log(NULL, AV_LOG_ERROR,
			"SDL advised audio format %d is not supported!\n", spec.format);
		SDL_CloseAudioDevice(m_sdlAudio_dev);
		m_sdlAudio_dev = 0;
		return -1;
	}
	if (spec.channels != wanted_spec.channels) {
		av_channel_layout_uninit(wanted_channel_layout);
		av_channel_layout_default(wanted_channel_layout, spec.channels);
		if (wanted_channel_layout->order != AV_CHANNEL_ORDER_NATIVE) {
			av_log(NULL, AV_LOG_ERROR,
				"SDL advised channel count %d is not supported!\n", spec.channels);
			SDL_CloseAudioDevice(m_sdlAudio_dev);
			m_sdlAudio_dev = 0;
			return -1;
		}
	}

	audio_hw_params->fmt = AV_SAMPLE_FMT_S16;
	audio_hw_params->freq = spec.freq;
	if (av_channel_layout_copy(&audio_hw_params->ch_layout, wanted_channel_layout) < 0) {
		SDL_CloseAudioDevice(m_sdlAudio_dev);
		m_sdlAudio_dev = 0;
		return -1;
	}
	audio_hw_params->frame_size = av_samples_get_buffer_size(NULL, audio_hw_params->ch_layout.nb_channels, 1, audio_hw_params->fmt, 1);
	audio_hw_params->bytes_per_sec = av_samples_get_buffer_size(NULL, audio_hw_params->ch_layout.nb_channels, audio_hw_params->freq, audio_hw_params->fmt, 1);
	if (audio_hw_params->bytes_per_sec <= 0 || audio_hw_params->frame_size <= 0) {
		av_log(NULL, AV_LOG_ERROR, "av_samples_get_buffer_size failed\n");
		SDL_CloseAudioDevice(m_sdlAudio_dev);
		m_sdlAudio_dev = 0;
		return -1;
	}
	return spec.size;
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
		if ((ret = audio_open(is, &ch_layout, sample_rate, &is->audio.audio_tgt)) < 0)
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

		SDL_PauseAudioDevice(m_sdlAudio_dev, 0);
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

int VideoCtl::stream_has_enough_packets(AVStream* st, int stream_id, PacketQueue* queue) {
	return stream_id < 0 ||
		queue->abort_request ||
		(st->disposition & AV_DISPOSITION_ATTACHED_PIC) ||
		queue->nb_packets > MIN_FRAMES && (!queue->duration || av_q2d(st->time_base) * queue->duration > 1.0);
}

int VideoCtl::is_realtime(AVFormatContext* s)
{
	if (!strcmp(s->iformat->name, "rtp")
		|| !strcmp(s->iformat->name, "rtsp")
		|| !strcmp(s->iformat->name, "sdp")
		)
		return 1;

	if (s->pb && (!strncmp(s->url, "rtp:", 4)
		|| !strncmp(s->url, "udp:", 4)
		)
		)
		return 1;
	return 0;
}

/* this thread gets the stream from the disk or the network */
//读取线程
void VideoCtl::ReadThread(VideoState* is)
{
	//VideoState *is = (VideoState *)arg;
	AVFormatContext* ic = NULL;
	int err, i, ret;
	int st_index[AVMEDIA_TYPE_NB];
	AVPacket* pkt = NULL;
	int64_t stream_start_time;
	int pkt_in_play_range = 0;
	const AVDictionaryEntry* t;
	AVDictionary** opts = nullptr;
	AvDictionary inputOptions;
	int orig_nb_streams = 0;
	SDL_mutex* wait_mutex = SDL_CreateMutex();
	int scan_all_pmts_set = 0;
	int64_t pkt_ts;

	const char* wanted_stream_spec[AVMEDIA_TYPE_NB] = { 0 };

	if (!wait_mutex) {
		av_log(NULL, AV_LOG_FATAL, "SDL_CreateMutex(): %s\n", SDL_GetError());
		ret = AVERROR(ENOMEM);
		goto fail;
	}
	is->session.read_wait_mutex = wait_mutex;
	memset(st_index, -1, sizeof(st_index));
	is->session.eof = 0;


	pkt = av_packet_alloc();
	if (!pkt) {
		av_log(NULL, AV_LOG_FATAL, "Could not allocate packet.\n");
		ret = AVERROR(ENOMEM);
		goto fail;
	}

	//构建 处理封装格式 结构体
	ic = avformat_alloc_context();
	if (!ic) {
		av_log(NULL, AV_LOG_FATAL, "Could not allocate context.\n");
		ret = AVERROR(ENOMEM);
		goto fail;
	}
	ic->interrupt_callback.callback = decode_interrupt_cb;
	ic->interrupt_callback.opaque = is;

	//打开文件，获得封装等信息

	inputOptions = BuildInputOptions(is->session.source);
	is->session.io.begin(IoOperation::Opening, is->session.source.network.connectTimeout);
	err = avformat_open_input(&ic, is->session.filename, nullptr, inputOptions.put());
	is->session.io.end();
	if (err < 0) {
		print_error(is->session.filename, err);
		is->session.readResult.store(err, std::memory_order_release);
		is->session.readError.store(MapAvError(err, IsRealtimeSource(ClassifyMediaSource(is->session.source.location))), std::memory_order_release);
		ret = err;
		goto fail;
	}

	is->session.ic = ic;




	orig_nb_streams = ic->nb_streams;
	//读取一部分视音频数据并且获得一些相关的信息
	is->session.io.begin(IoOperation::Probing,
		std::chrono::duration_cast<std::chrono::milliseconds>(is->session.source.network.analyzeDuration));
	err = avformat_find_stream_info(ic, opts);
	is->session.io.end();

	//     for (i = 0; i < orig_nb_streams; i++)
	//         av_dict_free(&opts[i]);
	//     av_freep(&opts);

	if (err < 0) {
		av_log(NULL, AV_LOG_WARNING,
			"%s: could not find codec parameters\n", is->session.filename);
		is->session.readResult.store(err, std::memory_order_release);
		is->session.readError.store(MapAvError(err, IsRealtimeSource(ClassifyMediaSource(is->session.source.location))), std::memory_order_release);
		ret = err;
		goto fail;
	}

	if (ic->pb)
		ic->pb->eof_reached = 0;

	is->video.max_frame_duration = (ic->iformat->flags & AVFMT_TS_DISCONT) ? 10.0 : 3600.0;

	is->session.realtime = is_realtime(ic);
	is->session.mediaInfo = BuildMediaInfo(is->session.source, ic);
	is->session.unlimitedBuffer = UseUnlimitedBuffer(is->session.source, is->session.realtime != 0);
	SigMediaInfo(is->session.mediaInfo);

	// 发送视频总时长信号，单位为秒
	SigVideoTotalSeconds(is->session.mediaInfo.duration ? static_cast<int>(is->session.mediaInfo.duration->count() / 1000) : 0);

	// 根据用户指定的流 specifier 来设置每种媒体类型的流索引。 
	// specifier 是一个流选择表达式（stream specifier），
	// 比如 "a:0" 表示第一个音频流，"v" 表示所有视频流，"s" 表示所有字幕流等。
	for (i = 0; i < ic->nb_streams; i++) {
		AVStream* st = ic->streams[i];
		enum AVMediaType type = st->codecpar->codec_type;
		st->discard = AVDISCARD_ALL;
		if (type >= 0 && wanted_stream_spec[type] && st_index[type] == -1)
			if (avformat_match_stream_specifier(ic, st, wanted_stream_spec[type]) > 0)
				st_index[type] = i;
	}
	// 如果用户指定了流 specifier，但没有找到匹配的流，就会输出错误日志并将对应的流索引设置为 INT_MAX。
	for (i = 0; i < AVMEDIA_TYPE_NB; i++) {
		if (wanted_stream_spec[(AVMediaType)i] && st_index[(AVMediaType)i] == -1) {
			av_log(NULL, AV_LOG_ERROR, "Stream specifier %s does not match any %s stream\n", wanted_stream_spec[(AVMediaType)i], av_get_media_type_string((AVMediaType)i));
			st_index[(AVMediaType)i] = INT_MAX;
		}
	}

	//获得视频、音频、字幕的流索引
	// 根据前面得到的流索引，使用 av_find_best_stream 函数来找到每种媒体类型的最佳流索引。
	// 如果前面找到了，就会使用前面的流，否则就会根据媒体类型来寻找最佳流索引。
	st_index[AVMEDIA_TYPE_VIDEO] =
		av_find_best_stream(ic, AVMEDIA_TYPE_VIDEO,
			st_index[AVMEDIA_TYPE_VIDEO], -1, NULL, 0);

	st_index[AVMEDIA_TYPE_AUDIO] =
		av_find_best_stream(ic, AVMEDIA_TYPE_AUDIO,
			st_index[AVMEDIA_TYPE_AUDIO],
			st_index[AVMEDIA_TYPE_VIDEO],
			NULL, 0);

	st_index[AVMEDIA_TYPE_SUBTITLE] =
		av_find_best_stream(ic, AVMEDIA_TYPE_SUBTITLE,
			st_index[AVMEDIA_TYPE_SUBTITLE],
			(st_index[AVMEDIA_TYPE_AUDIO] >= 0 ?
				st_index[AVMEDIA_TYPE_AUDIO] :
				st_index[AVMEDIA_TYPE_VIDEO]),
			NULL, 0);

	//if (st_index[AVMEDIA_TYPE_VIDEO] >= 0) {
	//	AVStream* st = ic->streams[st_index[AVMEDIA_TYPE_VIDEO]];
	//	AVCodecParameters* codecpar = st->codecpar;
	//	AVRational sar = av_guess_sample_aspect_ratio(ic, st, NULL);
	//}

	/* open the streams */
	//打开音频流
	if (st_index[AVMEDIA_TYPE_AUDIO] >= 0) {
		stream_component_open(is, st_index[AVMEDIA_TYPE_AUDIO]);
	}

	//打开视频流
	ret = -1;
	if (st_index[AVMEDIA_TYPE_VIDEO] >= 0) {
		ret = stream_component_open(is, st_index[AVMEDIA_TYPE_VIDEO]);
	}

	//打开字幕流
	if (st_index[AVMEDIA_TYPE_SUBTITLE] >= 0) {
		stream_component_open(is, st_index[AVMEDIA_TYPE_SUBTITLE]);
	}

	if (is->video.video_stream < 0 && is->audio.audio_stream < 0) {
		av_log(NULL, AV_LOG_FATAL, "Failed to open file '%s' or configure filtergraph\n",
			is->session.filename);
		ret = -1;
		goto fail;
	}
	SigPlaybackStatus(PlaybackStatus{PlaybackState::Playing, PlaybackError::None, 0, is->session.source.network.maxReconnectAttempts, {}, RedactMediaLocation(is->session.source.location)});

	//读取视频数据
	for (;;) {
		if (is->session.abort_request.load(std::memory_order_acquire))
			break;
		const auto paused = is->session.paused.load(std::memory_order_acquire);
		if (paused != is->session.last_paused) {
			is->session.last_paused = paused;
			if (paused)
				is->session.read_pause_return = av_read_pause(ic);
			else
				av_read_play(ic);
		}
		//if (m_bSpeedChanged) {
		//	std::shared_lock<std::shared_mutex> lock(m_speedMutex);
		//	is->sound_touch.setSampleRate(ic->sample_rate);
		//	is->sound_touch.setChannels(2); // 立体声
		//	is->sound_touch.setTempoChange(0.0f); // 保持原始速度
		//	is->sound_touch.setPitchSemiTones(0.0f); // 保持原始音调
		//	m_bSpeedChanged = false;
		//}
		int64_t seekTarget = 0;
		int64_t seekRelative = 0;
		int seekFlags = 0;
		bool hasSeekRequest = false;
		{
			std::lock_guard<std::mutex> seekLock(is->session.seek_mutex);
			if (is->session.seek_req) {
				seekTarget = is->session.seek_pos;
				seekRelative = is->session.seek_rel;
				seekFlags = is->session.seek_flags;
				is->session.seek_req = false;
				hasSeekRequest = true;
			}
		}
		if (hasSeekRequest) {
			const int64_t seekMin = seekRelative > 0 ? seekTarget - seekRelative + 2 : INT64_MIN;
			const int64_t seekMax = seekRelative < 0 ? seekTarget - seekRelative - 2 : INT64_MAX;
			// FIXME the +-2 is due to rounding being not done in the correct direction in generation
			//      of the seek_pos/seek_rel variables

			ret = avformat_seek_file(is->session.ic, -1, seekMin, seekTarget, seekMax, seekFlags);
			if (ret < 0) {
				av_log(NULL, AV_LOG_ERROR,
					"%s: error while seeking\n", is->session.ic->url);
			}
			else {
				if (is->audio.audio_stream >= 0)
					packet_queue_flush(&is->audio.audioq);
				if (is->subtitle.subtitle_stream >= 0)
					packet_queue_flush(&is->subtitle.subtitleq);
				if (is->video.video_stream >= 0)
					packet_queue_flush(&is->video.videoq);
				if (seekFlags & AVSEEK_FLAG_BYTE)
					is->clocks.extclk.set(NAN, 0);
				else
					is->clocks.extclk.set(seekTarget / static_cast<double>(AV_TIME_BASE), 0);
			}
			is->session.queue_attachments_req = 1;
			is->session.eof = 0;
			if (paused)
				step_to_next_frame();
		}
		if (is->session.queue_attachments_req) {
			if (is->video.video_st && is->video.video_st->disposition & AV_DISPOSITION_ATTACHED_PIC) {
				if ((ret = av_packet_ref(pkt, &is->video.video_st->attached_pic)) < 0)
					goto fail;
				packet_queue_put(&is->video.videoq, pkt);
				packet_queue_put_nullpacket(&is->video.videoq, pkt, is->video.video_stream);
			}
			is->session.queue_attachments_req = 0;
		}

		/* if the queue are full, no need to read more */
		if (!is->session.unlimitedBuffer &&
			(is->audio.audioq.size.load(std::memory_order_relaxed)
				+ is->video.videoq.size.load(std::memory_order_relaxed)
				+ is->subtitle.subtitleq.size.load(std::memory_order_relaxed) > MAX_QUEUE_SIZE
				|| (stream_has_enough_packets(is->audio.audio_st, is->audio.audio_stream, &is->audio.audioq) &&
					stream_has_enough_packets(is->video.video_st, is->video.video_stream, &is->video.videoq) &&
					stream_has_enough_packets(is->subtitle.subtitle_st, is->subtitle.subtitle_stream, &is->subtitle.subtitleq)))) {
			/* wait 10 ms */
			SDL_LockMutex(is->session.read_wait_mutex);
			SDL_CondWaitTimeout(is->session.continue_read_thread, is->session.read_wait_mutex, 10);
			SDL_UnlockMutex(is->session.read_wait_mutex);
			continue;
		}
		if (!paused &&
			(!is->audio.audio_st || (is->audio.aud_decoder.finished == is->audio.audioq.serial.load(std::memory_order_acquire) && is->audio.sampq.nb_remaining() == 0)) &&
			(!is->video.video_st || (is->video.vid_decoder.finished == is->video.videoq.serial.load(std::memory_order_acquire) && is->video.pictq.nb_remaining() == 0))) {
			const auto loopPolicy = m_loopPolicy.load(std::memory_order_acquire);
			if (loopPolicy == VideoLoopPolicy::LOOP_ALL) {
				//播放结束
				m_bPlayLoop.store(false, std::memory_order_release);
				std::this_thread::sleep_for(std::chrono::milliseconds(10));
				SigPlayNextOne();
				continue;
			}
			else if (loopPolicy == VideoLoopPolicy::LOOP_SINGLE) {
				// 重新播放
				stream_seek(0, 0);
			}
			else if (loopPolicy == VideoLoopPolicy::LOOP_RANDOM) {
				m_bPlayLoop.store(false, std::memory_order_release);
				std::this_thread::sleep_for(std::chrono::milliseconds(10));
				SigRandomPlayOne();
				continue;
			}
			else {
				// 先暂停播放循环，再退出
				SigStop();
				continue;
			}
		}
		//按帧读取
		is->session.io.begin(IoOperation::Reading, is->session.source.network.readTimeout);
		ret = av_read_frame(ic, pkt);
		is->session.io.end();
		if (ret < 0) {
			is->session.readResult.store(ret, std::memory_order_release);
			is->session.readError.store(MapAvError(ret, is->session.realtime != 0), std::memory_order_release);
			if ((ret == AVERROR_EOF || avio_feof(ic->pb)) && !is->session.eof) {
				if (is->video.video_stream >= 0)
					packet_queue_put_nullpacket(&is->video.videoq, pkt, is->video.video_stream);
				if (is->audio.audio_stream >= 0)
					packet_queue_put_nullpacket(&is->audio.audioq, pkt, is->audio.audio_stream);
				if (is->subtitle.subtitle_stream >= 0)
					packet_queue_put_nullpacket(&is->subtitle.subtitleq, pkt, is->subtitle.subtitle_stream);
				is->session.eof = 1;
			}
			if (ic->pb && ic->pb->error)
				break;
			SDL_LockMutex(is->session.read_wait_mutex);
			SDL_CondWaitTimeout(is->session.continue_read_thread, is->session.read_wait_mutex, 10);
			SDL_UnlockMutex(is->session.read_wait_mutex);
			continue;
		}
		else {
			is->session.eof = 0;
		}
		/* check if packet is in play range specified by user, then queue, otherwise discard */
		stream_start_time = ic->streams[pkt->stream_index]->start_time;
		pkt_ts = pkt->pts == AV_NOPTS_VALUE ? pkt->dts : pkt->pts;
		pkt_in_play_range = AV_NOPTS_VALUE == AV_NOPTS_VALUE ||
			(pkt_ts - (stream_start_time != AV_NOPTS_VALUE ? stream_start_time : 0)) *
			av_q2d(ic->streams[pkt->stream_index]->time_base) -
			(double)(0 != AV_NOPTS_VALUE ? 0 : 0) / 1000000
			<= ((double)AV_NOPTS_VALUE / 1000000);
		//按数据帧的类型存放至对应队列
		if (pkt->stream_index == is->audio.audio_stream && pkt_in_play_range) {
			packet_queue_put(&is->audio.audioq, pkt);
		}
		else if (pkt->stream_index == is->video.video_stream && pkt_in_play_range
			&& !(is->video.video_st->disposition & AV_DISPOSITION_ATTACHED_PIC)) {
			packet_queue_put(&is->video.videoq, pkt);
		}
		else if (pkt->stream_index == is->subtitle.subtitle_stream && pkt_in_play_range) {
			packet_queue_put(&is->subtitle.subtitleq, pkt);
		}
		else {
			av_packet_unref(pkt);
		}
	}

	ret = 0;
fail:
	if (ic && !is->session.ic)
		avformat_close_input(&ic);
	// 通知 LoopThread 线程读取结束
	if (ret != 0)
	{
		const auto error = is->session.readError.load(std::memory_order_acquire);
		SigPlaybackStatus(PlaybackStatus{PlaybackState::Failed, error, 0, is->session.source.network.maxReconnectAttempts, {}, RedactMediaLocation(is->session.source.location)});
		m_bPlayLoop.store(false, std::memory_order_release);
	}
	if (is->session.read_wait_mutex) {
		SDL_DestroyMutex(is->session.read_wait_mutex);
		is->session.read_wait_mutex = nullptr;
	}
	return;
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
	is->clocks.extclk.init(&is->clocks.extclk.serial);
	is->audio.audio_clock_serial = -1;
	is->audio.audio_volume.store(sdlVolume, std::memory_order_release);

	SigVideoVolume(volume);
	SigPauseStat(is->session.paused != 0);

	is->clocks.av_sync_type = AV_SYNC_AUDIO_MASTER;
	//构建读取线程
	is->session.read_tid = std::thread(&VideoCtl::ReadThread, this, is);

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
	if (is && is->session.stop_refresh_loop.exchange(false, std::memory_order_acq_rel)) {
		m_bPlayLoop.store(false, std::memory_order_release);
		return;
	}
	if (is && (!is->session.paused || is->session.force_refresh))
		video_refresh(is, &remaining_time);
	if (remaining_time > 0.0)
		av_usleep(static_cast<int64_t>(remaining_time * 1000000.0));
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
	std::unique_lock<std::shared_mutex> lock(m_streamMutex);
	if (m_CurStream == nullptr)
	{
		return;
	}
	if (!m_CurStream->session.ic || !CanSeek(m_CurStream->session.mediaInfo))
		return;
	int64_t ts = dPercent * m_CurStream->session.ic->duration;
	if (m_CurStream->session.ic->start_time != AV_NOPTS_VALUE)
		ts += m_CurStream->session.ic->start_time;
	stream_seek(ts, 0);
}

void VideoCtl::OnPlaySeekSeconds(int seconds)
{
	std::unique_lock<std::shared_mutex> lock(m_streamMutex);
	if (m_CurStream == nullptr)
	{
		return;
	}
	if (!m_CurStream->session.ic || !CanSeek(m_CurStream->session.mediaInfo))
		return;
	int64_t ts = static_cast<int64_t>(seconds) * AV_TIME_BASE;
	if (m_CurStream->session.ic->start_time != AV_NOPTS_VALUE)
		ts += m_CurStream->session.ic->start_time;
	stream_seek(ts, 0);
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
	std::unique_lock<std::shared_mutex> lock(m_streamMutex);
	if (m_CurStream == nullptr)
	{
		return;
	}
	double incr = 5.0;
	double pos = MediaSync::get_master_clock(m_CurStream);
	if (std::isnan(pos))
		pos = (double)m_CurStream->session.seek_pos / AV_TIME_BASE;
	pos += incr;
	if (m_CurStream->session.ic->start_time != AV_NOPTS_VALUE && pos < m_CurStream->session.ic->start_time / (double)AV_TIME_BASE)
		pos = m_CurStream->session.ic->start_time / (double)AV_TIME_BASE;
	stream_seek((int64_t)(pos * AV_TIME_BASE), (int64_t)(incr * AV_TIME_BASE));
}

void VideoCtl::OnSeekBack()
{
	std::unique_lock<std::shared_mutex> lock(m_streamMutex);
	if (m_CurStream == nullptr)
	{
		return;
	}
	double incr = -5.0;
	double pos = MediaSync::get_master_clock(m_CurStream);
	if (std::isnan(pos))
		pos = (double)m_CurStream->session.seek_pos / AV_TIME_BASE;
	pos += incr;
	if (m_CurStream->session.ic->start_time != AV_NOPTS_VALUE && pos < m_CurStream->session.ic->start_time / (double)AV_TIME_BASE)
		pos = m_CurStream->session.ic->start_time / (double)AV_TIME_BASE;
	stream_seek((int64_t)(pos * AV_TIME_BASE), (int64_t)(incr * AV_TIME_BASE));
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
	toggle_pause();
	std::shared_lock<std::shared_mutex> lock(m_streamMutex);
	if (m_CurStream == nullptr)
	{

		return;
	}
	SigPauseStat(m_CurStream->session.paused != 0);
}

void VideoCtl::requestStop()
{
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
	std::unique_lock<std::shared_mutex> lock(m_streamMutex);
	if (!m_CurStream)
		return;
	stream_cycle_channel(m_CurStream, AVMEDIA_TYPE_AUDIO);
}

void VideoCtl::OnCycleSubtitleTrack()
{
	std::unique_lock<std::shared_mutex> lock(m_streamMutex);
	if (!m_CurStream)
		return;
	stream_cycle_channel(m_CurStream, AVMEDIA_TYPE_SUBTITLE);
}

VideoCtl::VideoCtl() :
	m_CurStream(nullptr),
	m_bPlayLoop(false),
	m_sdlAudio_dev(0)
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
