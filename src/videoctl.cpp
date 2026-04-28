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
#include "videoctl.h"

#include "soundtouch_wrap.h"

#pragma execution_character_set("utf-8")

extern std::mutex g_show_rect_mutex;
// 是否允许丢帧（如果视频太慢了，跟不上音频或者外部时钟）
// -1为自动丢帧，0为不丢帧，1为强制丢帧
static int framedrop = 1;
static int infinite_buffer = -1;
static int64_t audio_callback_time;

#define FF_QUIT_EVENT    (SDL_USEREVENT + 2)

static void print_error(const char* s, int err) {
	char buf[256];
	av_strerror(err, buf, sizeof(buf));
	av_log(NULL, AV_LOG_ERROR, "%s: %s\n", s, buf);
}

static const struct TextureFormatEntry
{
	enum AVPixelFormat format;
	int texture_fmt;
} sdl_texture_format_map[] = {
	{AV_PIX_FMT_RGB8, SDL_PIXELFORMAT_RGB332},
	{AV_PIX_FMT_RGB444, SDL_PIXELFORMAT_RGB444},
	{AV_PIX_FMT_RGB555, SDL_PIXELFORMAT_RGB555},
	{AV_PIX_FMT_BGR555, SDL_PIXELFORMAT_BGR555},
	{AV_PIX_FMT_RGB565, SDL_PIXELFORMAT_RGB565},
	{AV_PIX_FMT_BGR565, SDL_PIXELFORMAT_BGR565},
	{AV_PIX_FMT_RGB24, SDL_PIXELFORMAT_RGB24},
	{AV_PIX_FMT_BGR24, SDL_PIXELFORMAT_BGR24},
	{AV_PIX_FMT_0RGB32, SDL_PIXELFORMAT_RGB888},
	{AV_PIX_FMT_0BGR32, SDL_PIXELFORMAT_BGR888},
	{AV_PIX_FMT_NE(RGB0, 0BGR), SDL_PIXELFORMAT_RGBX8888},
	{AV_PIX_FMT_NE(BGR0, 0RGB), SDL_PIXELFORMAT_BGRX8888},
	{AV_PIX_FMT_RGB32, SDL_PIXELFORMAT_ARGB8888},
	{AV_PIX_FMT_RGB32_1, SDL_PIXELFORMAT_RGBA8888},
	{AV_PIX_FMT_BGR32, SDL_PIXELFORMAT_ABGR8888},
	{AV_PIX_FMT_BGR32_1, SDL_PIXELFORMAT_BGRA8888},
	{AV_PIX_FMT_YUV420P, SDL_PIXELFORMAT_IYUV},
	{AV_PIX_FMT_YUYV422, SDL_PIXELFORMAT_YUY2},
	{AV_PIX_FMT_UYVY422, SDL_PIXELFORMAT_UYVY},
	{AV_PIX_FMT_NONE, SDL_PIXELFORMAT_UNKNOWN},
};

static enum AVColorSpace sdl_supported_color_spaces[] = {
	AVCOL_SPC_BT709,
	AVCOL_SPC_BT470BG,
	AVCOL_SPC_SMPTE170M,
	AVCOL_SPC_UNSPECIFIED,
};

/*
这段代码定义了一个名为 cmp_audio_fmts 的静态内联函数，用于比较两个音频格式（AVSampleFormat 类型）及其对应的通道数（int64_t 类型）。具体来说，该函数会根据以下规则返回一个整数值：

如果两个音频流的通道数都等于1，则忽略其样本格式是否为平面格式（planar format），直接比较它们的打包样本格式（packed sample format）。如果不相等，则返回非零值（表示不同），否则返回0（表示相同）。

如果任一音频流的通道数大于1，则首先比较两者的通道数是否相等。如果不相等，则直接返回非零值（表示不同）。如果通道数相等，则进一步比较它们的样本格式是否相同。只要有一个条件不满足，就返回非零值（表示不同），只有两者都相同时才返回0（表示相同）。
*/
static inline int cmp_audio_fmts(enum AVSampleFormat fmt1, int64_t channel_count1,
	enum AVSampleFormat fmt2, int64_t channel_count2)
{
	/* 如果通道计数== 1，平面格式和非平面格式是相同的 */
	if (channel_count1 == 1 && channel_count2 == 1)
		return av_get_packed_sample_fmt(fmt1) != av_get_packed_sample_fmt(fmt2);
	else
		return channel_count1 != channel_count2 || fmt1 != fmt2;
}

static double get_rotation(const int32_t* displaymatrix)
{
	double theta = 0;
	if (displaymatrix)
		theta = -round(av_display_rotation_get(displaymatrix));

	theta -= 360 * floor(theta / 360 + 0.9 / 360);

	if (fabs(theta - 90 * round(theta / 90)) > 2)
		av_log(NULL, AV_LOG_WARNING, "Odd rotation angle.\n"
			"If you want to help, upload a sample "
			"of this file to https://streams.videolan.org/upload/ "
			"and contact the ffmpeg-devel mailing list. (ffmpeg-devel@ffmpeg.org)");

	return theta;
}
/* prepare a new audio buffer */
static void sdl_audio_callback(void* opaque, Uint8* stream, int len)
{
	VideoState* is = (VideoState*)opaque;
	int audio_size, len1;

	VideoCtl* pVideoCtl = VideoCtl::GetInstance();

	audio_callback_time = av_gettime_relative();

	while (len > 0) {
		if (is->audio_buf_index >= is->audio_buf_size) {
			audio_size = pVideoCtl->audio_decode_frame(is);
			if (audio_size < 0) {
				/* if error, just output silence */
				is->audio_buf = NULL;
				is->audio_buf_size = SDL_AUDIO_MIN_BUFFER_SIZE / is->audio_tgt.frame_size * is->audio_tgt.frame_size;
			}
			else {
				is->audio_buf_size = audio_size;
			}
			is->audio_buf_index = 0;
		}
		len1 = is->audio_buf_size - is->audio_buf_index;
		if (len1 > len)
			len1 = len;
		if (is->audio_buf && is->audio_volume == SDL_MIX_MAXVOLUME)
			memcpy(stream, (uint8_t*)is->audio_buf + is->audio_buf_index, len1);
		else {
			memset(stream, 0, len1);
			if (is->audio_buf)
				SDL_MixAudio(stream, (uint8_t*)is->audio_buf + is->audio_buf_index, len1, is->audio_volume);
		}
		len -= len1;
		stream += len1;
		is->audio_buf_index += len1;
	}
	is->audio_write_buf_size = is->audio_buf_size - is->audio_buf_index;
	/* Let's assume the audio driver that is used by SDL has two periods. */
	if (!std::isnan(is->audio_clock)) {
		is->audclk.set_at(
			is->audio_clock - (double)(2 * is->audio_hw_buf_size + is->audio_write_buf_size) / is->audio_tgt.bytes_per_sec,
			is->audio_clock_serial,
			audio_callback_time / 1000000.0);
		is->extclk.sync_to_slave(is->audclk);
	}
}

static int decode_interrupt_cb(void* ctx)
{
	VideoState* is = (VideoState*)ctx;
	return is->abort_request;
}


int VideoCtl::realloc_texture(SDL_Texture** texture, Uint32 new_format, int new_width, int new_height, SDL_BlendMode blendmode, int init_texture)
{
	Uint32 format;
	int access, w, h;
	if (SDL_QueryTexture(*texture, &format, &access, &w, &h) < 0 || new_width != w || new_height != h || new_format != format) {
		void* pixels;
		int pitch;
		SDL_DestroyTexture(*texture);
		if (!(*texture = SDL_CreateTexture(m_sdlRenderer, new_format, SDL_TEXTUREACCESS_STREAMING, new_width, new_height)))
			return -1;
		if (SDL_SetTextureBlendMode(*texture, blendmode) < 0)
			return -1;
		if (init_texture) {
			if (SDL_LockTexture(*texture, NULL, &pixels, &pitch) < 0)
				return -1;
			memset(pixels, 0, pitch * new_height);
			SDL_UnlockTexture(*texture);
		}
	}
	return 0;
}

void VideoCtl::calculate_display_rect(SDL_Rect* rect,
	int scr_xleft, int scr_ytop, int scr_width, int scr_height,
	int pic_width, int pic_height, AVRational pic_sar)
{
	float aspect_ratio;
	int width, height, x, y;

	if (pic_sar.num == 0)
		aspect_ratio = 0;
	else
		aspect_ratio = av_q2d(pic_sar);

	if (aspect_ratio <= 0.0)
		aspect_ratio = 1.0;
	aspect_ratio *= (float)pic_width / (float)pic_height;

	/* XXX: we suppose the screen has a 1.0 pixel ratio */
	height = scr_height;
	width = lrint(height * aspect_ratio) & ~1;
	if (width > scr_width) {
		width = scr_width;
		height = lrint(width / aspect_ratio) & ~1;
	}
	x = (scr_width - width) / 2;
	y = (scr_height - height) / 2;
	rect->x = scr_xleft + x;
	rect->y = scr_ytop + y;
	rect->w = FFMAX(width, 1);
	rect->h = FFMAX(height, 1);
}

int VideoCtl::upload_texture(SDL_Texture* tex, AVFrame* frame, struct SwsContext** img_convert_ctx) {
	int ret = 0;
	switch (frame->format) {
	case AV_PIX_FMT_YUV420P:
		if (frame->linesize[0] < 0 || frame->linesize[1] < 0 || frame->linesize[2] < 0) {
			av_log(NULL, AV_LOG_ERROR, "Negative linesize is not supported for YUV.\n");
			return -1;
		}
		ret = SDL_UpdateYUVTexture(tex, NULL, frame->data[0], frame->linesize[0],
			frame->data[1], frame->linesize[1],
			frame->data[2], frame->linesize[2]);
		break;
	case AV_PIX_FMT_BGRA:
		if (frame->linesize[0] < 0) {
			ret = SDL_UpdateTexture(tex, NULL, frame->data[0] + frame->linesize[0] * (frame->height - 1), -frame->linesize[0]);
		}
		else {
			ret = SDL_UpdateTexture(tex, NULL, frame->data[0], frame->linesize[0]);
		}
		break;
	default:
		/* This should only happen if we are not using avfilter... */
		*img_convert_ctx = sws_getCachedContext(*img_convert_ctx,
			frame->width, frame->height, (AVPixelFormat)frame->format, frame->width, frame->height,
			AV_PIX_FMT_BGRA, SWS_BICUBIC, NULL, NULL, NULL);
		if (*img_convert_ctx != NULL) {
			uint8_t* pixels[4];
			int pitch[4];
			if (!SDL_LockTexture(tex, NULL, (void**)pixels, pitch)) {
				sws_scale(*img_convert_ctx, (const uint8_t* const*)frame->data, frame->linesize,
					0, frame->height, pixels, pitch);
				SDL_UnlockTexture(tex);
			}
		}
		else {
			av_log(NULL, AV_LOG_FATAL, "Cannot initialize the conversion context\n");
			ret = -1;
		}
		break;
	}
	return ret;
}

//显示视频画面
void VideoCtl::video_image_display(VideoState* is)
{
	Frame* vp;
	Frame* sp = NULL;
	SDL_Rect rect;

	vp = is->pictq.peek_last();
	if (is->subtitle_st) {
		if (is->subpq.nb_remaining() > 0) {
			sp = is->subpq.peek();

			if (vp->pts >= sp->pts + ((float)sp->sub.start_display_time / 1000)) {
				if (!sp->uploaded) {
					uint8_t* pixels[4];
					int pitch[4];
					int i;
					if (!sp->width || !sp->height) {
						sp->width = vp->width;
						sp->height = vp->height;
					}
					if (realloc_texture(&is->sub_texture, SDL_PIXELFORMAT_ARGB8888, sp->width, sp->height, SDL_BLENDMODE_BLEND, 1) < 0)
						return;

					for (i = 0; i < sp->sub.num_rects; i++) {
						AVSubtitleRect* sub_rect = sp->sub.rects[i];

						sub_rect->x = av_clip(sub_rect->x, 0, sp->width);
						sub_rect->y = av_clip(sub_rect->y, 0, sp->height);
						sub_rect->w = av_clip(sub_rect->w, 0, sp->width - sub_rect->x);
						sub_rect->h = av_clip(sub_rect->h, 0, sp->height - sub_rect->y);

						is->sub_convert_ctx = sws_getCachedContext(is->sub_convert_ctx,
							sub_rect->w, sub_rect->h, AV_PIX_FMT_PAL8,
							sub_rect->w, sub_rect->h, AV_PIX_FMT_BGRA,
							0, NULL, NULL, NULL);
						if (!is->sub_convert_ctx) {
							av_log(NULL, AV_LOG_FATAL, "Cannot initialize the conversion context\n");
							return;
						}
						if (!SDL_LockTexture(is->sub_texture, (SDL_Rect*)sub_rect, (void**)pixels, pitch)) {
							sws_scale(is->sub_convert_ctx, (const uint8_t* const*)sub_rect->data, sub_rect->linesize,
								0, sub_rect->h, pixels, pitch);
							SDL_UnlockTexture(is->sub_texture);
						}
					}
					sp->uploaded = 1;
				}
			}
			else
				sp = NULL;
		}
	}

	calculate_display_rect(&rect, is->xleft, is->ytop, is->width, is->height, vp->width, vp->height, vp->sar);

	if (!vp->uploaded) {
		int sdl_pix_fmt = vp->frame->format == AV_PIX_FMT_YUV420P ? SDL_PIXELFORMAT_YV12 : SDL_PIXELFORMAT_ARGB8888;
		if (realloc_texture(&is->vid_texture, sdl_pix_fmt, vp->frame->width, vp->frame->height, SDL_BLENDMODE_NONE, 0) < 0)
			return;
		if (upload_texture(is->vid_texture, vp->frame, &is->img_convert_ctx) < 0)
			return;
		vp->uploaded = 1;
		vp->flip_v = vp->frame->linesize[0] < 0;

		//通知宽高变化
		if (m_nFrameW != vp->frame->width || m_nFrameH != vp->frame->height)
		{
			m_nFrameW = vp->frame->width;
			m_nFrameH = vp->frame->height;
			SigFrameDimensionsChanged(m_nFrameW, m_nFrameH);
		}
	}

	SDL_RenderCopyEx(m_sdlRenderer, is->vid_texture, NULL, &rect, 0, NULL, (SDL_RendererFlip)(vp->flip_v ? SDL_FLIP_VERTICAL : 0));
	if (sp) {
		SDL_RenderCopy(m_sdlRenderer, is->sub_texture, NULL, &rect);
	}
}


//关闭流对应的解码器等
void VideoCtl::stream_component_close(VideoState* is, int stream_index)
{
	AVFormatContext* ic = is->ic;
	AVCodecParameters* codecpar;

	if (stream_index < 0 || stream_index >= ic->nb_streams)
		return;
	codecpar = ic->streams[stream_index]->codecpar;

	switch (codecpar->codec_type) {
	case AVMEDIA_TYPE_AUDIO:
		is->aud_decoder.abort(&is->sampq);
		SDL_CloseAudioDevice(m_sdlAudio_dev);
		is->aud_decoder.destroy();
		swr_free(&is->swr_ctx);
		av_freep(&is->audio_buf1);
		is->audio_buf1_size = 0;
		is->audio_buf = NULL;

		if (is->rdft) {
			av_rdft_end(is->rdft);
			av_freep(&is->rdft_data);
			is->rdft = NULL;
			is->rdft_bits = 0;
		}
		if (is->soundTouchHandle)
		{
			soundtouch_destroy(is->soundTouchHandle);
			is->soundTouchHandle = nullptr;
		}
		if (is->audio_new_buf)
		{
			av_freep(&is->audio_new_buf);
			is->audio_new_buf = NULL;
		}
		break;
	case AVMEDIA_TYPE_VIDEO:
		is->vid_decoder.abort(&is->pictq);
		is->vid_decoder.destroy();
		break;
	case AVMEDIA_TYPE_SUBTITLE:
		is->sub_decoder.abort(&is->subpq);
		is->sub_decoder.destroy();
		break;
	default:
		break;
	}

	ic->streams[stream_index]->discard = AVDISCARD_ALL;
	switch (codecpar->codec_type) {
	case AVMEDIA_TYPE_AUDIO:
		is->audio_st = NULL;
		is->audio_stream = -1;
		break;
	case AVMEDIA_TYPE_VIDEO:
		is->video_st = NULL;
		is->video_stream = -1;
		break;
	case AVMEDIA_TYPE_SUBTITLE:
		is->subtitle_st = NULL;
		is->subtitle_stream = -1;
		break;
	default:
		break;
	}
}
//关闭流
void VideoCtl::stream_close(VideoState* is)
{
	/* XXX: 使用特殊的url_shutdown调用来彻底中止解析 */
	is->abort_request = 1;
	is->read_tid.join();

	/* close each stream */
	if (is->audio_stream >= 0)
		stream_component_close(is, is->audio_stream);
	if (is->video_stream >= 0)
		stream_component_close(is, is->video_stream);
	if (is->subtitle_stream >= 0)
		stream_component_close(is, is->subtitle_stream);

	avformat_close_input(&is->ic);

	packet_queue_destroy(&is->videoq);
	packet_queue_destroy(&is->audioq);
	packet_queue_destroy(&is->subtitleq);

	/* free all pictures */
	is->pictq.destroy();
	is->sampq.destroy();
	is->subpq.destroy();
	SDL_DestroyCond(is->continue_read_thread);
	sws_freeContext(is->img_convert_ctx);
	sws_freeContext(is->sub_convert_ctx);
	av_free(is->filename);

	if (is->vid_texture)
		SDL_DestroyTexture(is->vid_texture);
	if (is->sub_texture)
		SDL_DestroyTexture(is->sub_texture);
	// 关闭音频（尽管在stream_component_close已经调用了）
	SDL_CloseAudioDevice(m_sdlAudio_dev);
	av_free(is);
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
	std::shared_lock<std::shared_mutex> streamLock(m_streamMutex);
	if (m_CurStream) {
		m_CurStream->play_rate = m_fPlaybackSpeed;
	}
}

void VideoCtl::set_play_loop_policy(VideoLoopPolicy loopPolicy)
{
	this->m_loopPolicy = loopPolicy;
}

int VideoCtl::get_master_sync_type(VideoState* is) {
	if (is->av_sync_type == AV_SYNC_VIDEO_MASTER) {
		if (is->video_st)
			return AV_SYNC_VIDEO_MASTER;
		else
			return AV_SYNC_AUDIO_MASTER;
	}
	else if (is->av_sync_type == AV_SYNC_AUDIO_MASTER) {
		if (is->audio_st)
			return AV_SYNC_AUDIO_MASTER;
		else
			return AV_SYNC_EXTERNAL_CLOCK;
	}
	else {
		return AV_SYNC_EXTERNAL_CLOCK;
	}
}

/* get the current master clock value */
double VideoCtl::get_master_clock(VideoState* is)
{
	double val;

	switch (get_master_sync_type(is)) {
	case AV_SYNC_VIDEO_MASTER:
		val = is->vidclk.get();
		break;
	case AV_SYNC_AUDIO_MASTER:
		val = is->audclk.get();
		break;
	default:
		val = is->extclk.get();
		break;
	}
	return val;
}
/*
check_external_clock_speed 函数用于动态调整外部时钟（extclk）的播放速度，以实现音视频同步，特别是在音频或视频数据包队列过多或过少时。
具体作用如下：
•	当音频或视频队列中的数据包数量过少（小于等于 EXTERNAL_CLOCK_MIN_FRAMES），说明解码速度跟不上播放速度，外部时钟会减慢（防止播放过快）。
•	当音频和视频队列中的数据包数量都很多（大于 EXTERNAL_CLOCK_MAX_FRAMES），说明解码速度远快于播放速度，外部时钟会加快（防止播放过慢）。
•	其他情况，如果外部时钟速度不是1.0，则逐步调整回1.0，保持正常速度。
这样做的目的是让外部时钟根据当前缓冲区的状态自适应调整速度，保证音视频同步和流畅播放，避免卡顿或延迟。
注意：
•	该函数只在同步类型为“外部时钟”时才有意义（如网络流或实时流场景）。
•	通过 set_clock_speed 修改 extclk 的速度，FFMAX 和 FFMIN 用于限制速度调整的上下限。
*/
void VideoCtl::check_external_clock_speed(VideoState* is) {
	if (is->video_stream >= 0 && is->videoq.nb_packets <= EXTERNAL_CLOCK_MIN_FRAMES ||
		is->audio_stream >= 0 && is->audioq.nb_packets <= EXTERNAL_CLOCK_MIN_FRAMES) {
		is->extclk.set_speed( FFMAX(EXTERNAL_CLOCK_SPEED_MIN, is->extclk.speed - EXTERNAL_CLOCK_SPEED_STEP));
	}
	else if ((is->video_stream < 0 || is->videoq.nb_packets > EXTERNAL_CLOCK_MAX_FRAMES) &&
		(is->audio_stream < 0 || is->audioq.nb_packets > EXTERNAL_CLOCK_MAX_FRAMES)) {
		is->extclk.set_speed( FFMIN(EXTERNAL_CLOCK_SPEED_MAX, is->extclk.speed + EXTERNAL_CLOCK_SPEED_STEP));
	}
	else {
		double speed = is->extclk.speed;
		if (speed != 1.0)
			is->extclk.set_speed( speed + EXTERNAL_CLOCK_SPEED_STEP * (1.0 - speed) / fabs(1.0 - speed));
	}
}

/* seek in the stream */
void VideoCtl::stream_seek(VideoState* is, int64_t pos, int64_t rel)
{
	if (!is->seek_req) {
		is->seek_pos = pos;
		is->seek_rel = rel;
		is->seek_flags &= ~AVSEEK_FLAG_BYTE;
		is->seek_req = 1;
		SDL_CondSignal(is->continue_read_thread);
	}
}

/* pause or resume the video */
void VideoCtl::stream_toggle_pause(VideoState* is)
{
	if (is->paused) {
		is->frame_timer += av_gettime_relative() / 1000000.0 - is->vidclk.last_updated;
		if (is->read_pause_return != AVERROR(ENOSYS)) {
			is->vidclk.paused = 0;
		}
		is->vidclk.set(is->vidclk.get(), is->vidclk.serial);
	}
	is->extclk.set(is->extclk.get(), is->extclk.serial);
	is->paused = is->audclk.paused = is->vidclk.paused = is->extclk.paused = !is->paused;
}

void VideoCtl::toggle_pause(VideoState* is)
{
	stream_toggle_pause(is);
	is->step = 0;
}


void VideoCtl::step_to_next_frame(VideoState* is)
{
	/* if the stream is paused unpause it, then step */
	if (is->paused)
		stream_toggle_pause(is);
	is->step = 1;
}

double VideoCtl::compute_target_delay(double delay, VideoState* is)
{
	double sync_threshold, diff = 0;

	/* 跟随主同步源的更新延迟 */
	if (get_master_sync_type(is) != AV_SYNC_VIDEO_MASTER) {
		/* if video is slave, we try to correct big delays by
		duplicating or deleting a frame */
		diff = is->vidclk.get() - get_master_clock(is);

		/* 跳过或重复帧。我们考虑延迟来计算阈值。我仍然不知道这是不是最好的猜测 */
		sync_threshold = FFMAX(AV_SYNC_THRESHOLD_MIN, FFMIN(AV_SYNC_THRESHOLD_MAX, delay));
		if (!std::isnan(diff) && fabs(diff) < is->max_frame_duration) {
			if (diff <= -sync_threshold)
				delay = FFMAX(0, delay + diff);
			else if (diff >= sync_threshold && delay > AV_SYNC_FRAMEDUP_THRESHOLD)
				delay = delay + diff;
			else if (diff >= sync_threshold)
				delay = 2 * delay;
		}
	}

	av_log(NULL, AV_LOG_TRACE, "video: delay=%0.3f A-V=%f\n",
		delay, -diff);

	return delay;
}

// vp_duration 函数在本软件中用于计算当前视频帧与下一帧之间的显示时长，即两帧的时间间隔
double VideoCtl::vp_duration(VideoState* is, Frame* vp, Frame* nextvp) {
	if (vp->serial == nextvp->serial) {
		double duration = nextvp->pts - vp->pts;
		if (std::isnan(duration) || duration <= 0 || duration > is->max_frame_duration)
			return vp->duration;
		else
			return duration;
	}
	else {
		return 0.0;
	}
}

void VideoCtl::update_video_pts(VideoState* is, double pts, int64_t pos, int serial) {
	/* 更新当前视频pts */
	is->vidclk.set(pts, serial);
	is->extclk.sync_to_slave(is->vidclk);
}

int VideoCtl::configure_filtergraph(AVFilterGraph* graph, const char* filtergraph, AVFilterContext* source_ctx, AVFilterContext* sink_ctx)
{
	int ret, i;
	int nb_filters = graph->nb_filters;
	AVFilterInOut* outputs = NULL, * inputs = NULL;

	if (filtergraph)
	{
		outputs = avfilter_inout_alloc();
		inputs = avfilter_inout_alloc();
		if (!outputs || !inputs)
		{
			ret = AVERROR(ENOMEM);
			goto fail;
		}

		outputs->name = av_strdup("in");
		outputs->filter_ctx = source_ctx;
		outputs->pad_idx = 0;
		outputs->next = NULL;

		inputs->name = av_strdup("out");
		inputs->filter_ctx = sink_ctx;
		inputs->pad_idx = 0;
		inputs->next = NULL;

		if ((ret = avfilter_graph_parse_ptr(graph, filtergraph, &inputs, &outputs, NULL)) < 0)
			goto fail;
	}
	else
	{
		if ((ret = avfilter_link(source_ctx, 0, sink_ctx, 0)) < 0)
			goto fail;
	}

	/* Reorder the filters to ensure that inputs of the custom filters are merged first */
	for (i = 0; i < graph->nb_filters - nb_filters; i++)
		FFSWAP(AVFilterContext*, graph->filters[i], graph->filters[i + nb_filters]);

	ret = avfilter_graph_config(graph, NULL);
fail:
	avfilter_inout_free(&outputs);
	avfilter_inout_free(&inputs);
	return ret;
}


int VideoCtl::configure_video_filters(AVFilterGraph* graph, VideoState* is, const char* vfilters, AVFrame* frame)
{
	enum AVPixelFormat pix_fmts[FF_ARRAY_ELEMS(sdl_texture_format_map)];
	// char sws_flags_str[512] = "";
	char buffersrc_args[256];
	int ret;
	AVFilterContext* filt_src = NULL, * filt_out = NULL, * last_filter = NULL;
	AVCodecParameters* codecpar = is->video_st->codecpar;
	AVRational fr = av_guess_frame_rate(is->ic, is->video_st, NULL);
	const AVDictionaryEntry* e = NULL;
	int nb_pix_fmts = 0;
	int i, j;
	AVBufferSrcParameters* par = av_buffersrc_parameters_alloc();

	if (!par)
		return AVERROR(ENOMEM);

	for (i = 0; i < m_sdlRendererInfo.num_texture_formats; i++)
	{
		for (j = 0; j < FF_ARRAY_ELEMS(sdl_texture_format_map) - 1; j++)
		{
			if (m_sdlRendererInfo.texture_formats[i] == sdl_texture_format_map[j].texture_fmt)
			{
				pix_fmts[nb_pix_fmts++] = sdl_texture_format_map[j].format;
				break;
			}
		}
	}
	pix_fmts[nb_pix_fmts] = AV_PIX_FMT_NONE;

	//while ((e = av_dict_iterate(sws_dict, e)))
	//{
	//	if (!strcmp(e->key, "sws_flags"))
	//	{
	//		av_strlcatf(sws_flags_str, sizeof(sws_flags_str), "%s=%s:", "flags", e->value);
	//	}
	//	else
	//		av_strlcatf(sws_flags_str, sizeof(sws_flags_str), "%s=%s:", e->key, e->value);
	//}
	//if (strlen(sws_flags_str))
	//	sws_flags_str[strlen(sws_flags_str) - 1] = '\0';

	//graph->scale_sws_opts = av_strdup(sws_flags_str);

	snprintf(buffersrc_args, sizeof(buffersrc_args),
		"video_size=%dx%d:pix_fmt=%d:time_base=%d/%d:pixel_aspect=%d/%d:"
		"colorspace=%d:range=%d",
		frame->width, frame->height, frame->format,
		is->video_st->time_base.num, is->video_st->time_base.den,
		codecpar->sample_aspect_ratio.num, FFMAX(codecpar->sample_aspect_ratio.den, 1),
		frame->colorspace, frame->color_range);
	if (fr.num && fr.den)
		av_strlcatf(buffersrc_args, sizeof(buffersrc_args), ":frame_rate=%d/%d", fr.num, fr.den);

	if ((ret = avfilter_graph_create_filter(&filt_src,
		avfilter_get_by_name("buffer"),
		"ffplay_buffer", buffersrc_args, NULL,
		graph)) < 0)
		goto fail;
	par->hw_frames_ctx = frame->hw_frames_ctx;
	ret = av_buffersrc_parameters_set(filt_src, par);
	if (ret < 0)
		goto fail;

	ret = avfilter_graph_create_filter(&filt_out,
		avfilter_get_by_name("buffersink"),
		"ffplay_buffersink", NULL, NULL, graph);
	if (ret < 0)
		goto fail;

	if ((ret = av_opt_set_int_list(filt_out, "pix_fmts", pix_fmts, AV_PIX_FMT_NONE, AV_OPT_SEARCH_CHILDREN)) < 0)
		goto fail;
	if ((ret = av_opt_set_int_list(filt_out, "color_spaces", sdl_supported_color_spaces, AVCOL_SPC_UNSPECIFIED, AV_OPT_SEARCH_CHILDREN)) < 0)
		goto fail;

	last_filter = filt_out;

	/* Note: this macro adds a filter before the lastly added filter, so the
	 * processing order of the filters is in reverse */
#define INSERT_FILT(name, arg)                                                \
    do                                                                        \
    {                                                                         \
        AVFilterContext *filt_ctx;                                            \
                                                                              \
        ret = avfilter_graph_create_filter(&filt_ctx,                         \
                                           avfilter_get_by_name(name),        \
                                           "ffplay_" name, arg, NULL, graph); \
        if (ret < 0)                                                          \
            goto fail;                                                        \
                                                                              \
        ret = avfilter_link(filt_ctx, 0, last_filter, 0);                     \
        if (ret < 0)                                                          \
            goto fail;                                                        \
                                                                              \
        last_filter = filt_ctx;                                               \
    } while (0)

	if (m_bAutorotate)
	{
		double theta = 0.0;
		int32_t* displaymatrix = NULL;
		AVFrameSideData* sd = av_frame_get_side_data(frame, AV_FRAME_DATA_DISPLAYMATRIX);
		if (sd)
			displaymatrix = (int32_t*)sd->data;
		if (!displaymatrix)
		{
			const AVPacketSideData* psd = av_packet_side_data_get(is->video_st->codecpar->coded_side_data,
				is->video_st->codecpar->nb_coded_side_data,
				AV_PKT_DATA_DISPLAYMATRIX);
			if (psd)
				displaymatrix = (int32_t*)psd->data;
		}
		theta = get_rotation(displaymatrix);

		if (fabs(theta - 90) < 1.0)
		{
			INSERT_FILT("transpose", displaymatrix[3] > 0 ? "cclock_flip" : "clock");
		}
		else if (fabs(theta - 180) < 1.0)
		{
			if (displaymatrix[0] < 0)
				INSERT_FILT("hflip", NULL);
			if (displaymatrix[4] < 0)
				INSERT_FILT("vflip", NULL);
		}
		else if (fabs(theta - 270) < 1.0)
		{
			INSERT_FILT("transpose", displaymatrix[3] < 0 ? "clock_flip" : "cclock");
		}
		else if (fabs(theta) > 1.0)
		{
			char rotate_buf[64];
			snprintf(rotate_buf, sizeof(rotate_buf), "%f*PI/180", theta);
			INSERT_FILT("rotate", rotate_buf);
		}
		else
		{
			if (displaymatrix && displaymatrix[4] < 0)
				INSERT_FILT("vflip", NULL);
		}
	}

	if ((ret = configure_filtergraph(graph, vfilters, filt_src, last_filter)) < 0)
		goto fail;

	is->in_video_filter = filt_src;
	is->out_video_filter = filt_out;

fail:
	av_freep(&par);
	return ret;
}

int VideoCtl::configure_audio_filters(VideoState* is, const char* afilters, int force_output_format)
{
	static const enum AVSampleFormat sample_fmts[] = { AV_SAMPLE_FMT_S16, AV_SAMPLE_FMT_NONE };
	int sample_rates[2] = { 0, -1 };
	AVFilterContext* filt_asrc = NULL, * filt_asink = NULL;
	char aresample_swr_opts[512] = "";
	const AVDictionaryEntry* e = NULL;
	AVBPrint bp;
	char asrc_args[256];
	int ret;

	avfilter_graph_free(&is->agraph);
	if (!(is->agraph = avfilter_graph_alloc()))
		return AVERROR(ENOMEM);
	is->agraph->nb_threads = 0;

	av_bprint_init(&bp, 0, AV_BPRINT_SIZE_AUTOMATIC);

	//while ((e = av_dict_iterate(swr_opts, e)))
	//	av_strlcatf(aresample_swr_opts, sizeof(aresample_swr_opts), "%s=%s:", e->key, e->value);
	//if (strlen(aresample_swr_opts))
	//	aresample_swr_opts[strlen(aresample_swr_opts) - 1] = '\0';
	//av_opt_set(is->agraph, "aresample_swr_opts", aresample_swr_opts, 0);

	av_channel_layout_describe_bprint(&is->audio_filter_src.ch_layout, &bp);

	ret = snprintf(asrc_args, sizeof(asrc_args),
		"sample_rate=%d:sample_fmt=%s:time_base=%d/%d:channel_layout=%s",
		is->audio_filter_src.freq, av_get_sample_fmt_name(is->audio_filter_src.fmt),
		1, is->audio_filter_src.freq, bp.str);
	// 创建音频源滤镜
	ret = avfilter_graph_create_filter(&filt_asrc,
		avfilter_get_by_name("abuffer"), "ffplay_abuffer",
		asrc_args, NULL, is->agraph);
	if (ret < 0)
		goto end;
	// 创建音频汇滤镜
	ret = avfilter_graph_create_filter(&filt_asink,
		avfilter_get_by_name("abuffersink"), "ffplay_abuffersink",
		NULL, NULL, is->agraph);
	if (ret < 0)
		goto end;

	if ((ret = av_opt_set_int_list(filt_asink, "sample_fmts", sample_fmts, AV_SAMPLE_FMT_NONE, AV_OPT_SEARCH_CHILDREN)) < 0)
		goto end;
	if ((ret = av_opt_set_int(filt_asink, "all_channel_counts", 1, AV_OPT_SEARCH_CHILDREN)) < 0)
		goto end;

	if (force_output_format)
	{
		av_bprint_clear(&bp);
		av_channel_layout_describe_bprint(&is->audio_tgt.ch_layout, &bp);
		sample_rates[0] = is->audio_tgt.freq;
		if ((ret = av_opt_set_int(filt_asink, "all_channel_counts", 0, AV_OPT_SEARCH_CHILDREN)) < 0)
			goto end;
		if ((ret = av_opt_set(filt_asink, "ch_layouts", bp.str, AV_OPT_SEARCH_CHILDREN)) < 0)
			goto end;
		if ((ret = av_opt_set_int_list(filt_asink, "sample_rates", sample_rates, -1, AV_OPT_SEARCH_CHILDREN)) < 0)
			goto end;
	}
	{

		std::string s = afilters;
		if ((ret = configure_filtergraph(is->agraph, afilters, filt_asrc, filt_asink)) < 0)
			goto end;
	}

	is->in_audio_filter = filt_asrc;
	is->out_audio_filter = filt_asink;

end:
	if (ret < 0)
		avfilter_graph_free(&is->agraph);
	av_bprint_finalize(&bp, NULL);

	return ret;
}

/* called to display each frame */
void VideoCtl::video_refresh(void* opaque, double* remaining_time)
{
	VideoState* is = (VideoState*)opaque;
	double time;

	Frame* sp, * sp2;

	double rdftspeed = 0.02;

	if (!is->paused && get_master_sync_type(is) == AV_SYNC_EXTERNAL_CLOCK && is->realtime)
		check_external_clock_speed(is);
	if (is->audio_st)
	{
		time = av_gettime_relative() / 1000000.0;
		if (is->force_refresh || is->last_vis_time + rdftspeed < time)
		{
			// 显示当前图片（如果有）
			video_display(is);
			is->last_vis_time = time;
		}
		*remaining_time = FFMIN(*remaining_time, is->last_vis_time + rdftspeed - time);
	}
	if (is->video_st) {
	retry:
		if (is->pictq.nb_remaining() == 0) {
			// nothing to do, no picture to display in the queue
		}
		else {
			double last_duration, duration, delay;
			Frame* vp, * lastvp;

			/* dequeue the picture */
			lastvp = is->pictq.peek_last();
			vp = is->pictq.peek();

			if (vp->serial != is->videoq.serial) {
				is->pictq.next();
				goto retry;
			}

			if (lastvp->serial != vp->serial)
				is->frame_timer = av_gettime_relative() / 1000000.0;

			if (is->paused)
				goto display;

			/* compute nominal last_duration */
			last_duration = vp_duration(is, lastvp, vp);
			delay = compute_target_delay(last_duration, is);
			time = av_gettime_relative() / 1000000.0;
			if (time < is->frame_timer + delay) {
				*remaining_time = FFMIN(is->frame_timer + delay - time, *remaining_time);
				goto display;
			}

			is->frame_timer += delay;
			if (delay > 0 && time - is->frame_timer > AV_SYNC_THRESHOLD_MAX)
				is->frame_timer = time;

			SDL_LockMutex(is->pictq.mutex);
			if (!std::isnan(vp->pts))
				update_video_pts(is, vp->pts, vp->pos, vp->serial);
			SDL_UnlockMutex(is->pictq.mutex);

			if (is->pictq.nb_remaining() > 1) {
				Frame* nextvp = is->pictq.peek_next();
				duration = vp_duration(is, vp, nextvp);
				if (!is->step && (framedrop > 0 || (framedrop && get_master_sync_type(is) != AV_SYNC_VIDEO_MASTER)) && time > is->frame_timer + duration) {
					is->frame_drops_late++;
					is->pictq.next();
					goto retry;
				}
			}

			if (is->subtitle_st) {
				while (is->subpq.nb_remaining() > 0) {
					sp = is->subpq.peek();

					if (is->subpq.nb_remaining() > 1)
						sp2 = is->subpq.peek_next();
					else
						sp2 = NULL;

					if (sp->serial != is->subtitleq.serial
						|| (is->vidclk.pts > (sp->pts + ((float)sp->sub.end_display_time / 1000)))
						|| (sp2 && is->vidclk.pts > (sp2->pts + ((float)sp2->sub.start_display_time / 1000))))
					{
						if (sp->uploaded) {
							int i;
							for (i = 0; i < sp->sub.num_rects; i++) {
								AVSubtitleRect* sub_rect = sp->sub.rects[i];
								uint8_t* pixels;
								int pitch, j;

								if (!SDL_LockTexture(is->sub_texture, (SDL_Rect*)sub_rect, (void**)&pixels, &pitch)) {
									for (j = 0; j < sub_rect->h; j++, pixels += pitch)
										memset(pixels, 0, sub_rect->w << 2);
									SDL_UnlockTexture(is->sub_texture);
								}
							}
						}
						is->subpq.next();
					}
					else {
						break;
					}
				}
			}

			is->pictq.next();
			is->force_refresh = 1;

			if (is->step && !is->paused)
				stream_toggle_pause(is);
		}
	display:
		/* display picture */
		if (is->force_refresh && is->pictq.rindex_shown)
			video_display(is);
	}
	is->force_refresh = 0;

	SigVideoPlaySeconds(static_cast<int>(get_master_clock(is)));
}

int VideoCtl::queue_picture(VideoState* is, AVFrame* src_frame, double pts, double duration, int64_t pos, int serial)
{
	Frame* vp;

	if (!(vp = is->pictq.peek_writable()))
		return -1;

	vp->sar = src_frame->sample_aspect_ratio;
	vp->uploaded = 0;

	vp->width = src_frame->width;
	vp->height = src_frame->height;
	vp->format = src_frame->format;
	vp->pts = pts;
	vp->duration = duration;
	vp->pos = pos;
	vp->serial = serial;

	av_frame_move_ref(vp->frame, src_frame);
	is->pictq.push();
	return 0;
}

// 从视频队列中获取数据，并解码数据，得到可显示的视频帧
// 返回值：-1表示出错，0表示没有得到视频帧，1表示得到视频帧
int VideoCtl::get_video_frame(VideoState* is, AVFrame* frame)
{
	int got_picture;

	if ((got_picture = is->vid_decoder.decode_frame(frame, NULL)) < 0)
		return -1;

	if (got_picture) {
		double dpts = NAN;

		if (frame->pts != AV_NOPTS_VALUE)
			dpts = av_q2d(is->video_st->time_base) * frame->pts;
		/*
		基于流与视频帧的宽高比，猜测视频帧的屏幕宽高比。
		流与视频帧的宽高比存在不同的情况，这个函数返回一个值，让你用于显示视频帧。
		默认原则：先用stream中的宽高比，再选择frame的。
		若没结果，则返回值为0/1
		*/
		frame->sample_aspect_ratio = av_guess_sample_aspect_ratio(is->ic, is->video_st, frame);

		if (framedrop > 0 //允许丢帧
			|| (framedrop && get_master_sync_type(is) != AV_SYNC_VIDEO_MASTER)// 自动丢帧，并且视频不是主时钟
			) {
			if (frame->pts != AV_NOPTS_VALUE) {
				// 如果视频帧的pts与主时钟的差值小于AV_NOSYNC_THRESHOLD（有同步的意义），
				// 并且比上次丢帧的时间更早，
				// 并且视频解码器的pkt_serial与视频时钟的serial相同，
				// 并且视频队列中有数据包，那么丢弃该帧
				double diff = dpts - get_master_clock(is);
				if (!isnan(diff) && fabs(diff) < AV_NOSYNC_THRESHOLD &&
					diff - is->frame_last_filter_delay < 0 &&
					is->vid_decoder.pkt_serial == is->vidclk.serial &&
					is->videoq.nb_packets) {
					is->frame_drops_early++;
					av_frame_unref(frame);
					got_picture = 0;
				}
			}
		}
	}

	return got_picture;
}

struct FrameData
{
	int64_t pkt_pos;
};

int VideoCtl::audio_thread(void* arg)
{
	VideoState* is = (VideoState*)arg;
	AVFrame* frame = av_frame_alloc();
	Frame* af;
	int reconfigure;
	int got_frame = 0;
	AVRational tb;
	int ret = 0;
	int last_serial = -1;

	if (!frame)
		return AVERROR(ENOMEM);

	do {
		if ((got_frame = is->aud_decoder.decode_frame(frame, NULL)) < 0)
			goto the_end;

		if (got_frame) {
			tb = AVRational{ 1, frame->sample_rate };
			tb = AVRational{ 1, frame->sample_rate };
#if CONFIG_AVFILTER
			// config filter
			reconfigure =
				cmp_audio_fmts(is->audio_filter_src.fmt, is->audio_filter_src.ch_layout.nb_channels,
					static_cast<AVSampleFormat>(frame->format), frame->ch_layout.nb_channels) ||
				av_channel_layout_compare(&is->audio_filter_src.ch_layout, &frame->ch_layout) ||
				is->audio_filter_src.freq != frame->sample_rate ||
				is->aud_decoder.pkt_serial != last_serial;

			if (reconfigure)
			{
				char buf1[1024], buf2[1024];
				av_channel_layout_describe(&is->audio_filter_src.ch_layout, buf1, sizeof(buf1));
				av_channel_layout_describe(&frame->ch_layout, buf2, sizeof(buf2));
				av_log(NULL, AV_LOG_DEBUG,
					"Audio frame changed from rate:%d ch:%d fmt:%s layout:%s serial:%d to rate:%d ch:%d fmt:%s layout:%s serial:%d\n",
					is->audio_filter_src.freq, is->audio_filter_src.ch_layout.nb_channels, av_get_sample_fmt_name(is->audio_filter_src.fmt), buf1, last_serial,
					frame->sample_rate, frame->ch_layout.nb_channels, av_get_sample_fmt_name(static_cast<AVSampleFormat>(frame->format)), buf2, is->aud_decoder.pkt_serial);

				is->audio_filter_src.fmt = static_cast<AVSampleFormat>(frame->format);
				ret = av_channel_layout_copy(&is->audio_filter_src.ch_layout, &frame->ch_layout);
				if (ret < 0)
				{
					goto the_end;
				}
				is->audio_filter_src.freq = frame->sample_rate;
				last_serial = is->aud_decoder.pkt_serial;
				double currentSpeed;
				{
					std::shared_lock<std::shared_mutex> lock(m_speedMutex);
					currentSpeed = m_fPlaybackSpeed;
				}
				auto afilters = std::format("atempo={:.2f}", currentSpeed);
				if ((ret = configure_audio_filters(is, afilters.c_str(), 1)) < 0)
				{
					goto the_end;
				}
			}
			// end config avfilter
			if ((ret = av_buffersrc_add_frame(is->in_audio_filter, frame)) < 0)
				goto the_end;

			while ((ret = av_buffersink_get_frame_flags(is->out_audio_filter, frame, 0)) >= 0)
			{
				FrameData* fd = frame->opaque_ref ? (FrameData*)frame->opaque_ref->data : NULL;
				tb = av_buffersink_get_time_base(is->out_audio_filter);
				if (!(af = frame_queue_peek_writable(&is->sampq)))
					goto the_end;

				af->pts = (frame->pts == AV_NOPTS_VALUE) ? NAN : frame->pts * av_q2d(tb);
				af->pos = fd ? fd->pkt_pos : -1;
				af->serial = is->aud_decoder.pkt_serial;
				af->duration = av_q2d(AVRational{ frame->nb_samples, frame->sample_rate });

				av_frame_move_ref(af->frame, frame);
				frame_queue_push(&is->sampq);
				// config avfilter
				if (is->audioq.serial != is->aud_decoder.pkt_serial)
					break;
			}
			if (ret == AVERROR_EOF)
				is->aud_decoder.finished = is->aud_decoder.pkt_serial;
			// end config avfilter
#else
			if (!(af = is->sampq.peek_writable()))
				goto the_end;

			af->pts = (frame->pts == AV_NOPTS_VALUE) ? NAN : frame->pts * av_q2d(tb);
			af->pos = frame->pkt_pos;
			af->serial = is->aud_decoder.pkt_serial;
			af->duration = av_q2d({ frame->nb_samples, frame->sample_rate });

			av_frame_move_ref(af->frame, frame);
			is->sampq.push();
#endif
		}
	} while (ret >= 0 || ret == AVERROR(EAGAIN) || ret == AVERROR_EOF);
the_end:

	av_frame_free(&frame);
	return ret;
}

//视频解码线程
int VideoCtl::video_thread(void* arg)
{
	VideoState* is = (VideoState*)arg;
	AVFrame* frame = av_frame_alloc();
	double pts;
	double duration;
	int ret;
	AVRational tb = is->video_st->time_base;
	// 帧率，单位为帧/秒，表示每秒钟显示多少帧
	AVRational frame_rate = av_guess_frame_rate(is->ic, is->video_st, NULL);
	// 滤镜相关
	AVFilterGraph* graph = NULL;
	AVFilterContext* filt_out = NULL, * filt_in = NULL;
	int last_w = 0;
	int last_h = 0;
	enum AVPixelFormat last_format = AVPixelFormat(-2);
	int last_serial = -1;
	int last_vfilter_idx = 0;
	if (!frame)
	{
		return AVERROR(ENOMEM);
	}

	//循环从队列中获取视频帧
	for (;;) {
		ret = get_video_frame(is, frame);
		if (ret < 0)
			goto the_end;
		if (!ret)
			continue;
#if CONFIG_AVFILTER
		// 新建滤镜
		if (last_w != frame->width ||
			last_h != frame->height ||
			last_format != frame->format ||
			last_serial != is->vid_decoder.pkt_serial ||
			last_vfilter_idx != is->vfilter_idx)
		{
			av_log(NULL, AV_LOG_DEBUG,
				"Video frame changed from size:%dx%d format:%s serial:%d to size:%dx%d format:%s serial:%d\n",
				last_w, last_h,
				(const char*)av_x_if_null(av_get_pix_fmt_name(last_format), "none"), last_serial,
				frame->width, frame->height,
				(const char*)av_x_if_null(av_get_pix_fmt_name(AVPixelFormat(frame->format)), "none"), is->vid_decoder.pkt_serial);
			avfilter_graph_free(&graph);
			graph = avfilter_graph_alloc();
			if (!graph)
			{
				ret = AVERROR(ENOMEM);
				goto the_end;
			}
			graph->nb_threads = 0;
			double currentSpeed;
			{
				std::shared_lock<std::shared_mutex> lock(m_speedMutex);
				currentSpeed = m_fPlaybackSpeed;
			}
			std::string mvfilters = std::format("setpts={:.2f}*PTS", 1 / currentSpeed);
			if ((ret = configure_video_filters(graph, is, mvfilters.c_str(), frame)) < 0)
			{
				SDL_Event event{};
				event.type = FF_QUIT_EVENT;
				event.user.data1 = is;
				SDL_PushEvent(&event);
				goto the_end;
			}
			filt_in = is->in_video_filter;
			filt_out = is->out_video_filter;
			last_w = frame->width;
			last_h = frame->height;
			last_format = AVPixelFormat(frame->format);
			last_serial = is->vid_decoder.pkt_serial;
			last_vfilter_idx = is->vfilter_idx;
			frame_rate = av_buffersink_get_frame_rate(filt_out);
		}
		// end 新建滤镜

		ret = av_buffersrc_add_frame(filt_in, frame);
		if (ret < 0)
			goto the_end;

		while (ret >= 0)
		{
			FrameData* fd;

			is->frame_last_returned_time = av_gettime_relative() / 1000000.0;

			ret = av_buffersink_get_frame_flags(filt_out, frame, 0);
			if (ret < 0)
			{
				if (ret == AVERROR_EOF)
					is->vid_decoder.finished = is->vid_decoder.pkt_serial;
				ret = 0;
				break;
			}

			fd = frame->opaque_ref ? (FrameData*)frame->opaque_ref->data : NULL;

			is->frame_last_filter_delay = av_gettime_relative() / 1000000.0 - is->frame_last_returned_time;
			if (fabs(is->frame_last_filter_delay) > AV_NOSYNC_THRESHOLD / 10.0)
				is->frame_last_filter_delay = 0;
			tb = av_buffersink_get_time_base(filt_out);
			duration = (frame_rate.num && frame_rate.den ? av_q2d(AVRational{ frame_rate.den, frame_rate.num }) : 0);
			pts = (frame->pts == AV_NOPTS_VALUE) ? NAN : frame->pts * av_q2d(tb);
			ret = queue_picture(is, frame, pts, duration, fd ? fd->pkt_pos : -1, is->vid_decoder.pkt_serial);
			av_frame_unref(frame);
			if (is->videoq.serial != is->vid_decoder.pkt_serial)
				break;
		}
#else
		
		// 计算每帧的显示时间，单位为秒
		duration = (frame_rate.num && frame_rate.den ? av_q2d({ frame_rate.den, frame_rate.num }) : 0);
		// 计算视频帧的显示时间戳，单位为秒
		pts = ((frame->pts == AV_NOPTS_VALUE) ? NAN : frame->pts * av_q2d(tb));// 根据播放速度调整延迟
		ret = queue_picture(is, frame, pts, duration, frame->pkt_pos, is->vid_decoder.pkt_serial);
		av_frame_unref(frame);
#endif
		if (ret < 0)
			goto the_end;
	}
the_end:

	av_frame_free(&frame);
	return 0;
}

int VideoCtl::subtitle_thread(void* arg)
{
	VideoState* is = (VideoState*)arg;
	Frame* sp;
	int got_subtitle;
	double pts;

	for (;;) {
		if (!(sp = is->subpq.peek_writable()))
			return 0;

		if ((got_subtitle = is->sub_decoder.decode_frame(NULL, &sp->sub)) < 0)
			break;

		pts = 0;

		if (got_subtitle && sp->sub.format == 0) {
			if (sp->sub.pts != AV_NOPTS_VALUE)
				pts = sp->sub.pts / (double)AV_TIME_BASE;
			sp->pts = pts;
			sp->serial = is->sub_decoder.pkt_serial;
			sp->width = is->sub_decoder.avctx->width;
			sp->height = is->sub_decoder.avctx->height;
			sp->uploaded = 0;

			/* now we can update the picture count */
			is->subpq.push();
		}
		else if (got_subtitle) {
			avsubtitle_free(&sp->sub);
		}
	}
	return 0;
}

/* copy samples for viewing in editor window */
void VideoCtl::update_sample_display(VideoState* is, short* samples, int samples_size)
{
	int size, len;

	size = samples_size / sizeof(short);
	while (size > 0) {
		len = SAMPLE_ARRAY_SIZE - is->sample_array_index;
		if (len > size)
			len = size;
		memcpy(is->sample_array + is->sample_array_index, samples, len * sizeof(short));
		samples += len;
		is->sample_array_index += len;
		if (is->sample_array_index >= SAMPLE_ARRAY_SIZE)
			is->sample_array_index = 0;
		size -= len;
	}
}

/* return the wanted number of samples to get better sync if sync_type is video or external master clock */
int VideoCtl::synchronize_audio(VideoState* is, int nb_samples)
{
	int wanted_nb_samples = nb_samples;

	/* 如果不是master，那么我们会尝试删除或添加样本来纠正时钟 */
	if (get_master_sync_type(is) != AV_SYNC_AUDIO_MASTER) {
		double diff, avg_diff;
		int min_nb_samples, max_nb_samples;

		diff = is->audclk.get() - get_master_clock(is);

		if (!std::isnan(diff) && fabs(diff) < AV_NOSYNC_THRESHOLD) {
			is->audio_diff_cum = diff + is->audio_diff_avg_coef * is->audio_diff_cum;
			if (is->audio_diff_avg_count < AUDIO_DIFF_AVG_NB) {
				/* 没有足够的措施来做出正确的估计 */
				is->audio_diff_avg_count++;
			}
			else {
				/* 估算A-V差异 */
				avg_diff = is->audio_diff_cum * (1.0 - is->audio_diff_avg_coef);

				if (fabs(avg_diff) >= is->audio_diff_threshold) {
					wanted_nb_samples = nb_samples + (int)(diff * is->audio_src.freq);
					min_nb_samples = ((nb_samples * (100 - SAMPLE_CORRECTION_PERCENT_MAX) / 100));
					max_nb_samples = ((nb_samples * (100 + SAMPLE_CORRECTION_PERCENT_MAX) / 100));
					wanted_nb_samples = av_clip(wanted_nb_samples, min_nb_samples, max_nb_samples);
				}
				av_log(NULL, AV_LOG_TRACE, "diff=%f adiff=%f sample_diff=%d apts=%0.3f %f\n",
					diff, avg_diff, wanted_nb_samples - nb_samples,
					is->audio_clock, is->audio_diff_threshold);
			}
		}
		else {
			/* 差异太大：可能是初始的 PTS 错误，因此需要重置音频-视频滤波器 */
			is->audio_diff_avg_count = 0;
			is->audio_diff_cum = 0;
		}
	}

	return  wanted_nb_samples;
}

/**
* Decode one audio frame and return its uncompressed size.
*
* The processed audio frame is decoded, converted if required, and
* stored in is->audio_buf, with size in bytes given by the return
* value.
*/
int VideoCtl::audio_decode_frame(VideoState* is)
{
	int data_size, resampled_data_size;
	av_unused double audio_clock0;
	int wanted_nb_samples;
	Frame* af;
	int translate_time = 1;
	if (is->paused)
		return -1;
reload:
	do {
#if defined(_WIN32)
		// 使用条件变量等待，替代忙等待
		SDL_LockMutex(is->sampq.mutex);
		while (is->sampq.nb_remaining() == 0 && !is->audioq.abort_request) {
			// 设置超时，避免永久阻塞
			audio_callback_time = av_gettime_relative();
			SDL_CondWaitTimeout(is->sampq.cond, is->sampq.mutex, 100); // 100ms超时
			// 检查是否超时过长
			if ((av_gettime_relative() - audio_callback_time) > 1000000LL * is->audio_hw_buf_size / is->audio_tgt.bytes_per_sec / 2) {
				SDL_UnlockMutex(is->sampq.mutex);
				return -1;
			}
		}
		SDL_UnlockMutex(is->sampq.mutex);
		
		// 检查是否因中止请求而退出
		if (is->audioq.abort_request) {
			return -1;
		}
#endif
		if (!(af = is->sampq.peek_readable()))
			return -1;
		is->sampq.next();
	} while (af->serial != is->audioq.serial);
	// 根据frame中指定的音频参数获取缓冲区的大小 af->frame->channels * af->frame->nb_samples * 2
	data_size = av_samples_get_buffer_size(NULL, af->frame->ch_layout.nb_channels,
		af->frame->nb_samples,
		(AVSampleFormat)af->frame->format, 1);

	wanted_nb_samples = synchronize_audio(is, af->frame->nb_samples);

	if (af->frame->format != is->audio_src.fmt ||
		av_channel_layout_compare(&af->frame->ch_layout, &is->audio_src.ch_layout) ||
		af->frame->sample_rate != is->audio_src.freq ||
		(wanted_nb_samples != af->frame->nb_samples && !is->swr_ctx)) {
		swr_free(&is->swr_ctx);
		swr_alloc_set_opts2(&is->swr_ctx,
			&is->audio_tgt.ch_layout, is->audio_tgt.fmt, is->audio_tgt.freq,
			&af->frame->ch_layout, (AVSampleFormat)af->frame->format, af->frame->sample_rate,
			0, NULL);
		if (!is->swr_ctx || swr_init(is->swr_ctx) < 0) {
			av_log(NULL, AV_LOG_ERROR,
				"Cannot create sample rate converter for conversion of %d Hz %s %d channels to %d Hz %s %d channels!\n",
				af->frame->sample_rate, av_get_sample_fmt_name((AVSampleFormat)af->frame->format), af->frame->ch_layout.nb_channels,
				is->audio_tgt.freq, av_get_sample_fmt_name(is->audio_tgt.fmt), is->audio_tgt.ch_layout.nb_channels);
			swr_free(&is->swr_ctx);
			return -1;
		}
		if (av_channel_layout_copy(&is->audio_src.ch_layout, &af->frame->ch_layout) < 0)
			return -1;
		is->audio_src.freq = af->frame->sample_rate;
		is->audio_src.fmt = (AVSampleFormat)af->frame->format;
	}

	if (is->swr_ctx) {
		const uint8_t** in = (const uint8_t**)af->frame->extended_data;
		uint8_t** out = &is->audio_buf1;
		int out_count = (int64_t)wanted_nb_samples * is->audio_tgt.freq / af->frame->sample_rate + 256;
		int out_size = av_samples_get_buffer_size(NULL, is->audio_tgt.ch_layout.nb_channels, out_count, is->audio_tgt.fmt, 0);
		int len2;
		if (out_size < 0) {
			av_log(NULL, AV_LOG_ERROR, "av_samples_get_buffer_size() failed\n");
			return -1;
		}
		if (wanted_nb_samples != af->frame->nb_samples) {
			if (swr_set_compensation(is->swr_ctx, (wanted_nb_samples - af->frame->nb_samples) * is->audio_tgt.freq / af->frame->sample_rate,
				wanted_nb_samples * is->audio_tgt.freq / af->frame->sample_rate) < 0) {
				av_log(NULL, AV_LOG_ERROR, "swr_set_compensation() failed\n");
				return -1;
			}
		}		av_fast_malloc(&is->audio_buf1, &is->audio_buf1_size, out_size);
		if (!is->audio_buf1)
			return AVERROR(ENOMEM);
		len2 = swr_convert(is->swr_ctx, out, out_count, in, af->frame->nb_samples);
		if (len2 < 0) {
			av_log(NULL, AV_LOG_ERROR, "swr_convert() failed\n");
			return -1;
		}
		if (len2 == out_count) {
			av_log(NULL, AV_LOG_WARNING, "audio buffer is probably too small\n");
			if (swr_init(is->swr_ctx) < 0)
				swr_free(&is->swr_ctx);
		}
		is->audio_buf = is->audio_buf1;
		resampled_data_size = len2 * is->audio_tgt.ch_layout.nb_channels * av_get_bytes_per_sample(is->audio_tgt.fmt);
		//=====================倍速处理 begin==========================
		int bytes_per_sample = av_get_bytes_per_sample(is->audio_tgt.fmt);
		if (is->soundTouchHandle && is->play_rate != 1.0f && !is->abort_request)
		{
			av_fast_malloc(&is->audio_new_buf, &is->audio_new_buf_size, out_size * translate_time);
			if (!is->audio_new_buf) {
				// Allocation failed; buf already freed
				return AVERROR(ENOMEM);
			}
			// 将音频数据转换为SoundTouch库需要的格式（把每两个uint8_t转为short）
			for (int i = 0; i < (resampled_data_size / 2); i++)
			{
				is->audio_new_buf[i] = (is->audio_buf1[i * 2] | (is->audio_buf1[i * 2 + 1] << 8));
			}
			int ret_len = soundtouch_translate(is->soundTouchHandle, 
				is->audio_new_buf, // input
				(float)(is->play_rate), // speed
				(float)(1.0f / is->play_rate),// pitch
				resampled_data_size / 2,
				bytes_per_sample, 
				is->audio_tgt.ch_layout.nb_channels,
				af->frame->sample_rate);
			if (ret_len > 0) {
				is->audio_buf = (uint8_t*)is->audio_new_buf;
				resampled_data_size = ret_len;
			}
			else {
				translate_time++;
				goto reload;
			}
		}
	}
	else {
		is->audio_buf = af->frame->data[0];
		resampled_data_size = data_size;
	}

	audio_clock0 = is->audio_clock;
	/* update the audio clock with the pts */
	if (!isnan(af->pts))
		is->audio_clock = af->pts + (double)af->frame->nb_samples / af->frame->sample_rate;
	else
		is->audio_clock = NAN;
	is->audio_clock_serial = af->serial;
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
		return -1;
	}
	if (spec.channels != wanted_spec.channels) {
		av_channel_layout_uninit(wanted_channel_layout);
		av_channel_layout_default(wanted_channel_layout, spec.channels);
		if (wanted_channel_layout->order != AV_CHANNEL_ORDER_NATIVE) {
			av_log(NULL, AV_LOG_ERROR,
				"SDL advised channel count %d is not supported!\n", spec.channels);
			return -1;
		}
	}

	audio_hw_params->fmt = AV_SAMPLE_FMT_S16;
	audio_hw_params->freq = spec.freq;
	if (av_channel_layout_copy(&audio_hw_params->ch_layout, wanted_channel_layout) < 0)
		return -1;
	audio_hw_params->frame_size = av_samples_get_buffer_size(NULL, audio_hw_params->ch_layout.nb_channels, 1, audio_hw_params->fmt, 1);
	audio_hw_params->bytes_per_sec = av_samples_get_buffer_size(NULL, audio_hw_params->ch_layout.nb_channels, audio_hw_params->freq, audio_hw_params->fmt, 1);
	if (audio_hw_params->bytes_per_sec <= 0 || audio_hw_params->frame_size <= 0) {
		av_log(NULL, AV_LOG_ERROR, "av_samples_get_buffer_size failed\n");
		return -1;
	}
	return spec.size;
}

/* open a given stream. Return 0 if OK */
//打开流
int VideoCtl::stream_component_open(VideoState* is, int stream_index)
{
	AVFormatContext* ic = is->ic;
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
	case AVMEDIA_TYPE_AUDIO: is->last_audio_stream = stream_index; break;
	case AVMEDIA_TYPE_SUBTITLE: is->last_subtitle_stream = stream_index; break;
	case AVMEDIA_TYPE_VIDEO: is->last_video_stream = stream_index; break;
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
	if ((ret = avcodec_open2(avctx, codec, &opts)) < 0) {
		goto fail;
	}
	if ((t = av_dict_get(opts, "", NULL, AV_DICT_IGNORE_SUFFIX))) {
		av_log(NULL, AV_LOG_ERROR, "Option %s not found.\n", t->key);
		ret = AVERROR_OPTION_NOT_FOUND;
		goto fail;
	}

	is->eof = 0;
	ic->streams[stream_index]->discard = AVDISCARD_DEFAULT;
	switch (avctx->codec_type) {
	case AVMEDIA_TYPE_AUDIO:
#if CONFIG_AVFILTER
	{
		AVFilterContext* sink;

		is->audio_filter_src.freq = avctx->sample_rate;
		ret = av_channel_layout_copy(&is->audio_filter_src.ch_layout, &avctx->ch_layout);
		if (ret < 0)
			goto fail;
		is->audio_filter_src.fmt = avctx->sample_fmt;
		double currentSpeed;
		{
			std::shared_lock<std::shared_mutex> lock(m_speedMutex);
			currentSpeed = m_fPlaybackSpeed;
		}
		auto afilters = std::format("atempo={:.2f}", currentSpeed);
		if ((ret = configure_audio_filters(is, afilters.c_str(), 0)) < 0)
		{
			print_error("configure_audio_filters", ret);
			goto fail;
		}
		sink = is->out_audio_filter;
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
		if ((ret = audio_open(is, &ch_layout, sample_rate, &is->audio_tgt)) < 0)
			goto fail;
		is->audio_hw_buf_size = ret;
		is->audio_src = is->audio_tgt;
		is->audio_buf_size = 0;
		is->audio_buf_index = 0;

		/* init averaging filter */
		is->audio_diff_avg_coef = exp(log(0.01) / AUDIO_DIFF_AVG_NB);
		is->audio_diff_avg_count = 0;
		/* since we do not have a precise anough audio FIFO fullness,
		   we correct audio sync only if larger than this threshold */
		is->audio_diff_threshold = (double)(is->audio_hw_buf_size) / is->audio_tgt.bytes_per_sec;

		is->audio_stream = stream_index;
		is->audio_st = ic->streams[stream_index];

		if ((ret = is->aud_decoder.init(avctx, &is->audioq, is->continue_read_thread)) < 0)
			goto fail;
		if (is->ic->iformat->flags & AVFMT_NOTIMESTAMPS) {
			is->aud_decoder.start_pts = is->audio_st->start_time;
			is->aud_decoder.start_pts_tb = is->audio_st->time_base;
		}

		packet_queue_start(is->aud_decoder.queue);
		is->aud_decoder.decode_thread = std::thread(&VideoCtl::audio_thread, this, is);

		SDL_PauseAudioDevice(m_sdlAudio_dev, 0);
		break;
	case AVMEDIA_TYPE_VIDEO:
		is->video_stream = stream_index;
		is->video_st = ic->streams[stream_index];

		if ((ret = is->vid_decoder.init(avctx, &is->videoq, is->continue_read_thread)) < 0)
			goto fail;
		packet_queue_start(is->vid_decoder.queue);
		is->vid_decoder.decode_thread = std::thread(&VideoCtl::video_thread, this, is);
		is->queue_attachments_req = 1;
		break;
	case AVMEDIA_TYPE_SUBTITLE:
		is->subtitle_stream = stream_index;
		is->subtitle_st = ic->streams[stream_index];

		if ((ret = is->sub_decoder.init(avctx, &is->subtitleq, is->continue_read_thread)) < 0)
			goto fail;
		packet_queue_start(is->sub_decoder.queue);
		is->sub_decoder.decode_thread = std::thread(&VideoCtl::subtitle_thread, this, is);
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
	is->read_wait_mutex = wait_mutex;
	memset(st_index, -1, sizeof(st_index));
	is->eof = 0;


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

	err = avformat_open_input(&ic, is->filename, nullptr, nullptr);
	if (err < 0) {
		print_error(is->filename, err);
		ret = -1;
		goto fail;
	}

	is->ic = ic;


	av_format_inject_global_side_data(ic);


	orig_nb_streams = ic->nb_streams;
	//读取一部分视音频数据并且获得一些相关的信息
	err = avformat_find_stream_info(ic, opts);

	//     for (i = 0; i < orig_nb_streams; i++)
	//         av_dict_free(&opts[i]);
	//     av_freep(&opts);

	if (err < 0) {
		av_log(NULL, AV_LOG_WARNING,
			"%s: could not find codec parameters\n", is->filename);
		ret = -1;
		goto fail;
	}

	if (ic->pb)
		ic->pb->eof_reached = 0; // FIXME hack, ffplay maybe should not use avio_feof() to test for the end

	is->max_frame_duration = (ic->iformat->flags & AVFMT_TS_DISCONT) ? 10.0 : 3600.0;

	is->realtime = is_realtime(ic);

	// 发送视频总时长信号，单位为秒
	SigVideoTotalSeconds(static_cast<int>(ic->duration / 1000000LL));

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

	if (is->video_stream < 0 && is->audio_stream < 0) {
		av_log(NULL, AV_LOG_FATAL, "Failed to open file '%s' or configure filtergraph\n",
			is->filename);
		ret = -1;
		goto fail;
	}

	if (infinite_buffer < 0 && is->realtime)
		infinite_buffer = 1;

	//读取视频数据
	for (;;) {
		if (is->abort_request)
			break;
		if (is->paused != is->last_paused) {
			is->last_paused = is->paused;
			if (is->paused)
				is->read_pause_return = av_read_pause(ic);
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
		if (is->seek_req) {
			int64_t seek_target = is->seek_pos;
			int64_t seek_min = is->seek_rel > 0 ? seek_target - is->seek_rel + 2 : INT64_MIN;
			int64_t seek_max = is->seek_rel < 0 ? seek_target - is->seek_rel - 2 : INT64_MAX;
			// FIXME the +-2 is due to rounding being not done in the correct direction in generation
			//      of the seek_pos/seek_rel variables

			ret = avformat_seek_file(is->ic, -1, seek_min, seek_target, seek_max, is->seek_flags);
			if (ret < 0) {
				av_log(NULL, AV_LOG_ERROR,
					"%s: error while seeking\n", is->ic->url);
			}
			else {
				if (is->audio_stream >= 0)
					packet_queue_flush(&is->audioq);
				if (is->subtitle_stream >= 0)
					packet_queue_flush(&is->subtitleq);
				if (is->video_stream >= 0)
					packet_queue_flush(&is->videoq);
				if (is->seek_flags & AVSEEK_FLAG_BYTE) {
					is->extclk.set(NAN, 0);
				}
				else {
					is->extclk.set(seek_target / (double)AV_TIME_BASE, 0);
				}
			}
			is->seek_req = 0;
			is->queue_attachments_req = 1;
			is->eof = 0;
			if (is->paused)
				step_to_next_frame(is);
		}
		if (is->queue_attachments_req) {
			if (is->video_st && is->video_st->disposition & AV_DISPOSITION_ATTACHED_PIC) {
				if ((ret = av_packet_ref(pkt, &is->video_st->attached_pic)) < 0)
					goto fail;
				packet_queue_put(&is->videoq, pkt);
				packet_queue_put_nullpacket(&is->videoq, pkt, is->video_stream);
			}
			is->queue_attachments_req = 0;
		}

		/* if the queue are full, no need to read more */
		if (infinite_buffer < 1 &&
			(is->audioq.size + is->videoq.size + is->subtitleq.size > MAX_QUEUE_SIZE
				|| (stream_has_enough_packets(is->audio_st, is->audio_stream, &is->audioq) &&
					stream_has_enough_packets(is->video_st, is->video_stream, &is->videoq) &&
					stream_has_enough_packets(is->subtitle_st, is->subtitle_stream, &is->subtitleq)))) {
			/* wait 10 ms */
			SDL_LockMutex(is->read_wait_mutex);
			SDL_CondWaitTimeout(is->continue_read_thread, is->read_wait_mutex, 10);
			SDL_UnlockMutex(is->read_wait_mutex);
			continue;
		}
		if (!is->paused &&
			(!is->audio_st || (is->aud_decoder.finished == is->audioq.serial && is->sampq.nb_remaining() == 0)) &&
			(!is->video_st || (is->vid_decoder.finished == is->videoq.serial && is->pictq.nb_remaining() == 0))) {
			if (m_loopPolicy == VideoLoopPolicy::LOOP_ALL) {
				//播放结束
				m_bPlayLoop = false;
				std::this_thread::sleep_for(std::chrono::milliseconds(10));
				SigPlayNextOne();
				continue;
			}
			else if (m_loopPolicy == VideoLoopPolicy::LOOP_SINGLE) {
				// 重新播放
				stream_seek(is, 0, 0);
			}
			else if (m_loopPolicy == VideoLoopPolicy::LOOP_RANDOM) {
				m_bPlayLoop = false;
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
		ret = av_read_frame(ic, pkt);
		if (ret < 0) {
			if ((ret == AVERROR_EOF || avio_feof(ic->pb)) && !is->eof) {
				if (is->video_stream >= 0)
					packet_queue_put_nullpacket(&is->videoq, pkt, is->video_stream);
				if (is->audio_stream >= 0)
					packet_queue_put_nullpacket(&is->audioq, pkt, is->audio_stream);
				if (is->subtitle_stream >= 0)
					packet_queue_put_nullpacket(&is->subtitleq, pkt, is->subtitle_stream);
				is->eof = 1;
			}
			if (ic->pb && ic->pb->error)
				break;
			SDL_LockMutex(is->read_wait_mutex);
			SDL_CondWaitTimeout(is->continue_read_thread, is->read_wait_mutex, 10);
			SDL_UnlockMutex(is->read_wait_mutex);
			continue;
		}
		else {
			is->eof = 0;
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
		if (pkt->stream_index == is->audio_stream && pkt_in_play_range) {
			packet_queue_put(&is->audioq, pkt);
		}
		else if (pkt->stream_index == is->video_stream && pkt_in_play_range
			&& !(is->video_st->disposition & AV_DISPOSITION_ATTACHED_PIC)) {
			packet_queue_put(&is->videoq, pkt);
		}
		else if (pkt->stream_index == is->subtitle_stream && pkt_in_play_range) {
			packet_queue_put(&is->subtitleq, pkt);
		}
		else {
			av_packet_unref(pkt);
		}
	}

	ret = 0;
fail:
	if (ic && !is->ic)
		avformat_close_input(&ic);
	// 通知 LoopThread 线程读取结束
	if (ret != 0) {
		SDL_Event event;

		event.type = FF_QUIT_EVENT;
		event.user.data1 = is;
		SDL_PushEvent(&event);
	}
	SDL_DestroyMutex(is->read_wait_mutex);
	is->read_wait_mutex = nullptr;
	return;
}

VideoState* VideoCtl::stream_open(const char* filename)
{
	VideoState* is;
	//构造视频状态类
	is = (VideoState*)av_mallocz(sizeof(VideoState));
	if (!is)
		return NULL;
	is->soundTouchHandle = soundtouch_create();
	is->audio_new_buf = NULL;
	is->audio_new_buf_size = 0;
	{
		std::shared_lock<std::shared_mutex> lock(m_speedMutex);
		is->play_rate = m_fPlaybackSpeed;
	}
	//视频文件名
	is->last_video_stream = is->video_stream = -1;
	is->last_audio_stream = is->audio_stream = -1;
	is->last_subtitle_stream = is->subtitle_stream = -1;
	is->filename = av_strdup(filename);
	if (!is->filename)
		goto fail;
	//指定输入格式
	is->ytop = 0;
	is->xleft = 0;

	/* start video display */
	//初始化视频帧队列
	if (is->pictq.init( &is->videoq, VIDEO_PICTURE_QUEUE_SIZE, 1) < 0)
		goto fail;
	//初始化字幕帧队列
	if (is->subpq.init( &is->subtitleq, SUBPICTURE_QUEUE_SIZE, 0) < 0)
		goto fail;
	//初始化音频帧队列
	if (is->sampq.init( &is->audioq, SAMPLE_QUEUE_SIZE, 1) < 0)
		goto fail;
	//初始化队列中的数据包
	if (is->videoq.init() < 0 ||
		is->audioq.init() < 0 ||
		is->subtitleq.init() < 0)
		goto fail;
	//构建 继续读取线程 信号量
	if (!(is->continue_read_thread = SDL_CreateCond())) {
		av_log(NULL, AV_LOG_FATAL, "SDL_CreateCond(): %s\n", SDL_GetError());
		goto fail;
	}
	//视频、音频 时钟
	is->vidclk.init(&is->videoq.serial);
	is->audclk.init(&is->audioq.serial);
	is->extclk.init(&is->extclk.serial);
	is->audio_clock_serial = -1;
	//音量
	if (startup_volume < 0)
		av_log(NULL, AV_LOG_WARNING, "-volume=%d < 0, setting to 0\n", startup_volume);
	if (startup_volume > 100)
		av_log(NULL, AV_LOG_WARNING, "-volume=%d > 100, setting to 100\n", startup_volume);
	startup_volume = av_clip(startup_volume, 0, 100);
	startup_volume = av_clip(SDL_MIX_MAXVOLUME * startup_volume / 100, 0, SDL_MIX_MAXVOLUME);
	is->audio_volume = startup_volume;

	SigVideoVolume(startup_volume * 1.0 / SDL_MIX_MAXVOLUME);
	SigPauseStat(is->paused != 0);

	is->av_sync_type = AV_SYNC_AUDIO_MASTER;
	//构建读取线程
	is->read_tid = std::thread(&VideoCtl::ReadThread, this, is);

	return is;

fail:
	stream_close(is);
	return NULL;
}

void VideoCtl::stream_cycle_channel(VideoState* is, int codec_type)
{
	AVFormatContext* ic = is->ic;
	int start_index, stream_index;
	int old_index;
	AVStream* st;
	AVProgram* p = NULL;
	int nb_streams = is->ic->nb_streams;

	if (codec_type == AVMEDIA_TYPE_VIDEO) {
		start_index = is->last_video_stream;
		old_index = is->video_stream;
	}
	else if (codec_type == AVMEDIA_TYPE_AUDIO) {
		start_index = is->last_audio_stream;
		old_index = is->audio_stream;
	}
	else {
		start_index = is->last_subtitle_stream;
		old_index = is->subtitle_stream;
	}
	stream_index = start_index;

	if (codec_type != AVMEDIA_TYPE_VIDEO && is->video_stream != -1) {
		p = av_find_program_from_stream(ic, NULL, is->video_stream);
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
				is->last_subtitle_stream = -1;
				goto the_end;
			}
			if (start_index == -1)
				return;
			stream_index = 0;
		}
		if (stream_index == start_index)
			return;
		st = is->ic->streams[p ? p->stream_index[stream_index] : stream_index];
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


void VideoCtl::refresh_loop_wait_event(VideoState* is, SDL_Event* event) {
	double remaining_time = 0.0;
	SDL_PumpEvents();
	while (!SDL_PeepEvents(event, 1, SDL_GETEVENT, SDL_FIRSTEVENT, SDL_LASTEVENT) && m_bPlayLoop)
	{
		if (remaining_time > 0.0)
			av_usleep((int64_t)(remaining_time * 1000000.0));
		remaining_time = REFRESH_RATE;
		if (is && (!is->paused || is->force_refresh))
			video_refresh(is, &remaining_time);
		SDL_PumpEvents();
	}
}

void VideoCtl::seek_chapter(VideoState* is, int incr)
{
	int64_t pos = get_master_clock(is) * AV_TIME_BASE;
	int i;

	if (!is->ic->nb_chapters)
		return;

	/* find the current chapter */
	for (i = 0; i < is->ic->nb_chapters; i++) {
		AVChapter* ch = is->ic->chapters[i];
		if (av_compare_ts(pos, /*AV_TIME_BASE_Q*/{ 1, AV_TIME_BASE }, ch->start, ch->time_base) < 0) {
			i--;
			break;
		}
	}

	i += incr;
	i = FFMAX(i, 0);
	if (i >= is->ic->nb_chapters)
		return;

	av_log(NULL, AV_LOG_VERBOSE, "Seeking to chapter %d.\n", i);
	stream_seek(is, av_rescale_q(is->ic->chapters[i]->start, is->ic->chapters[i]->time_base,
		/*AV_TIME_BASE_Q*/{ 1, AV_TIME_BASE }), 0);
}

//播放控制循环
void VideoCtl::LoopThread()
{
	SDL_Event event;
	double incr = 0., pos, frac;

	m_bPlayLoop = true;

	while (m_bPlayLoop)
	{
		double x;
		refresh_loop_wait_event(m_CurStream, &event);
		switch (event.type) {
		case SDL_KEYDOWN:
			switch (event.key.keysym.sym) {
			case SDLK_s: // S: Step to next frame
				step_to_next_frame(m_CurStream);
				break;
			case SDLK_a:
				stream_cycle_channel(m_CurStream, AVMEDIA_TYPE_AUDIO);
				break;
			case SDLK_v:
				stream_cycle_channel(m_CurStream, AVMEDIA_TYPE_VIDEO);
				break;
			case SDLK_c:
				stream_cycle_channel(m_CurStream, AVMEDIA_TYPE_VIDEO);
				stream_cycle_channel(m_CurStream, AVMEDIA_TYPE_AUDIO);
				stream_cycle_channel(m_CurStream, AVMEDIA_TYPE_SUBTITLE);
				break;
			case SDLK_t:
				stream_cycle_channel(m_CurStream, AVMEDIA_TYPE_SUBTITLE);
				break;

			default:
				break;
			}
			break;
		case SDL_WINDOWEVENT:
			//窗口大小改变事件
			switch (event.window.event) {
			case SDL_WINDOWEVENT_RESIZED:
				screen_width = m_CurStream->width = event.window.data1;
				screen_height = m_CurStream->height = event.window.data2;
			case SDL_WINDOWEVENT_EXPOSED:
				m_CurStream->force_refresh = 1;
			}
			break;
		case SDL_QUIT:
		case FF_QUIT_EVENT:
			do_exit(m_CurStream);
			break;
		default:
			break;
		}
	}


	do_exit(m_CurStream);

}


void VideoCtl::OnPlaySeek(double dPercent)
{
	std::shared_lock<std::shared_mutex> lock(m_streamMutex);
	if (m_CurStream == nullptr)
	{
		return;
	}
	int64_t ts = dPercent * m_CurStream->ic->duration;
	if (m_CurStream->ic->start_time != AV_NOPTS_VALUE)
		ts += m_CurStream->ic->start_time;
	stream_seek(m_CurStream, ts, 0);
}

void VideoCtl::OnPlayVolume(double dPercent)
{
	startup_volume = dPercent * SDL_MIX_MAXVOLUME;
	std::shared_lock<std::shared_mutex> lock(m_streamMutex);
	if (m_CurStream == nullptr)
	{
		return;
	}
	m_CurStream->audio_volume = startup_volume;
}

void VideoCtl::OnSeekForward()
{
	std::shared_lock<std::shared_mutex> lock(m_streamMutex);
	if (m_CurStream == nullptr)
	{
		return;
	}
	double incr = 5.0;
	double pos = get_master_clock(m_CurStream);
	if (std::isnan(pos))
		pos = (double)m_CurStream->seek_pos / AV_TIME_BASE;
	pos += incr;
	if (m_CurStream->ic->start_time != AV_NOPTS_VALUE && pos < m_CurStream->ic->start_time / (double)AV_TIME_BASE)
		pos = m_CurStream->ic->start_time / (double)AV_TIME_BASE;
	stream_seek(m_CurStream, (int64_t)(pos * AV_TIME_BASE), (int64_t)(incr * AV_TIME_BASE));
}

void VideoCtl::OnSeekBack()
{
	std::shared_lock<std::shared_mutex> lock(m_streamMutex);
	if (m_CurStream == nullptr)
	{
		return;
	}
	double incr = -5.0;
	double pos = get_master_clock(m_CurStream);
	if (std::isnan(pos))
		pos = (double)m_CurStream->seek_pos / AV_TIME_BASE;
	pos += incr;
	if (m_CurStream->ic->start_time != AV_NOPTS_VALUE && pos < m_CurStream->ic->start_time / (double)AV_TIME_BASE)
		pos = m_CurStream->ic->start_time / (double)AV_TIME_BASE;
	stream_seek(m_CurStream, (int64_t)(pos * AV_TIME_BASE), (int64_t)(incr * AV_TIME_BASE));
}

void VideoCtl::UpdateVolume(int sign, double step)
{
	std::shared_lock<std::shared_mutex> lock(m_streamMutex);
	if (m_CurStream == nullptr)
	{
		return;
	}
	double volume_level = m_CurStream->audio_volume ? (20 * log(m_CurStream->audio_volume / (double)SDL_MIX_MAXVOLUME) / log(10)) : -1000.0;
	int new_volume = lrint(SDL_MIX_MAXVOLUME * pow(10.0, (volume_level + sign * step) / 20.0));
	m_CurStream->audio_volume = av_clip(m_CurStream->audio_volume == new_volume ? (m_CurStream->audio_volume + sign) : new_volume, 0, SDL_MIX_MAXVOLUME);

	SigVideoVolume(m_CurStream->audio_volume * 1.0 / SDL_MIX_MAXVOLUME);
}

/* display the current picture, if any */
void VideoCtl::video_display(VideoState* is)
{
	if (!m_sdlWindow)
		video_open(is);
	if (m_sdlRenderer)
	{
		//恰好显示控件大小在变化，则不刷新显示
		if (g_show_rect_mutex.try_lock())
		{
			std::unique_lock<std::mutex> lock(g_show_rect_mutex, std::adopt_lock);
			// 二次检查：获锁后 renderer 可能已被 do_exit 销毁
			if (!m_sdlRenderer)
				return;
			SDL_SetRenderDrawColor(m_sdlRenderer, 0, 0, 0, 255);
			SDL_RenderClear(m_sdlRenderer);
			video_image_display(is);
			SDL_RenderPresent(m_sdlRenderer);
		}
	}

}

int VideoCtl::video_open(VideoState* is)
{
	int w, h;

	w = screen_width;
	h = screen_height;

	if (!m_sdlWindow) {
		int flags = SDL_WINDOW_SHOWN;
		flags |= SDL_WINDOW_RESIZABLE;

		m_sdlWindow = SDL_CreateWindowFrom(m_playWid);
		SDL_GetWindowSize(m_sdlWindow, &w, &h);//初始宽高设置为显示控件宽高
		SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "linear");
		if (m_sdlWindow) {
			SDL_RendererInfo info;
			if (!m_sdlRenderer)
				m_sdlRenderer = SDL_CreateRenderer(m_sdlWindow, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
			if (!m_sdlRenderer) {
				av_log(NULL, AV_LOG_WARNING, "Failed to initialize a hardware accelerated renderer: %s\n", SDL_GetError());
				m_sdlRenderer = SDL_CreateRenderer(m_sdlWindow, -1, 0);
			}
			if (m_sdlRenderer) {
				if (!SDL_GetRendererInfo(m_sdlRenderer, &info))
					av_log(NULL, AV_LOG_VERBOSE, "Initialized %s renderer.\n", info.name);
			}
		}
	}
	else {
		SDL_SetWindowSize(m_sdlWindow, w, h);
	}

	if (!m_sdlWindow || !m_sdlRenderer) {
		av_log(NULL, AV_LOG_FATAL, "SDL: could not set video mode - exiting\n");
		do_exit(is);
	}

	is->width = w;
	is->height = h;

	return 0;
}

void VideoCtl::do_exit(VideoState* is)
{
	if (is)
	{
		stream_close(is);
	}
	{
		std::unique_lock<std::shared_mutex> lock(m_streamMutex);
		m_CurStream = nullptr;
	}
	if (m_sdlRenderer)
	{
		// 先在锁保护下置空，防止 video_display 使用已销毁的 renderer
		SDL_Renderer* renderer_to_destroy = nullptr;
		{
			std::lock_guard<std::mutex> lock(g_show_rect_mutex);
			renderer_to_destroy = m_sdlRenderer;
			m_sdlRenderer = nullptr;
		}
		SDL_DestroyRenderer(renderer_to_destroy);
	}

	if (m_sdlWindow)
	{
		//SDL_DestroyWindow(window);
		m_sdlWindow = nullptr;
	}

	SigStopFinished();
}

void VideoCtl::OnAddVolume()
{
	std::shared_lock<std::shared_mutex> lock(m_streamMutex);
	if (m_CurStream == nullptr)
	{
		return;
	}
	double volume_level = m_CurStream->audio_volume ? (20 * log(m_CurStream->audio_volume / (double)SDL_MIX_MAXVOLUME) / log(10)) : -1000.0;
	int new_volume = lrint(SDL_MIX_MAXVOLUME * pow(10.0, (volume_level + SDL_VOLUME_STEP) / 20.0));
	m_CurStream->audio_volume = av_clip(m_CurStream->audio_volume == new_volume ? (m_CurStream->audio_volume + 1) : new_volume, 0, SDL_MIX_MAXVOLUME);
	SigVideoVolume(m_CurStream->audio_volume * 1.0 / SDL_MIX_MAXVOLUME);
}

void VideoCtl::OnSubVolume()
{
	std::shared_lock<std::shared_mutex> lock(m_streamMutex);
	if (m_CurStream == nullptr)
	{
		return;
	}
	double volume_level = m_CurStream->audio_volume ? (20 * log(m_CurStream->audio_volume / (double)SDL_MIX_MAXVOLUME) / log(10)) : -1000.0;
	int new_volume = lrint(SDL_MIX_MAXVOLUME * pow(10.0, (volume_level - SDL_VOLUME_STEP) / 20.0));
	m_CurStream->audio_volume = av_clip(m_CurStream->audio_volume == new_volume ? (m_CurStream->audio_volume - 1) : new_volume, 0, SDL_MIX_MAXVOLUME);
	SigVideoVolume(m_CurStream->audio_volume * 1.0 / SDL_MIX_MAXVOLUME);
}

void VideoCtl::OnPause()
{
	std::shared_lock<std::shared_mutex> lock(m_streamMutex);
	if (m_CurStream == nullptr)
	{

		return;
	}
	toggle_pause(m_CurStream);
	SigPauseStat(m_CurStream->paused != 0);
}

void VideoCtl::OnStop()
{
	// 先暂停播放循环，再退出
	m_bPlayLoop = false;
}

VideoCtl::VideoCtl() :
	m_CurStream(nullptr),
	m_bPlayLoop(false),
	screen_width(0),
	screen_height(0),
	startup_volume(30),
	m_sdlRenderer(nullptr),
	m_sdlWindow(nullptr),
	m_nFrameW(0),
	m_nFrameH(0)
{
	avdevice_register_all();
	//网络格式初始化
	avformat_network_init();
}

bool VideoCtl::Init()
{
	if (ConnectSignalSlots() == false)
	{
		return false;
	}

	if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER))
	{
		av_log(NULL, AV_LOG_FATAL, "Could not initialize SDL - %s\n", SDL_GetError());
		av_log(NULL, AV_LOG_FATAL, "(Did you set the DISPLAY variable?)\n");
		return false;
	}
	SDL_EventState(SDL_SYSWMEVENT, SDL_IGNORE);
	SDL_EventState(SDL_USEREVENT, SDL_IGNORE);

	return true;
}

bool VideoCtl::ConnectSignalSlots()
{
	SigStop.connect([this]() { OnStop(); });

	return true;
}


VideoCtl* VideoCtl::GetInstance()
{
	// Meyers' Singleton - C++11 保证线程安全初始化，程序退出时自动析构
	static VideoCtl instance;
	static bool initialized = false;
	if (!initialized) {
		if (instance.Init()) {
			initialized = true;
		} else {
			return nullptr;
		}
	}
	return &instance;
}

VideoCtl::~VideoCtl()
{
	// 确保播放循环线程在析构前完全退出
	m_bPlayLoop = false;
	if (m_tPlayLoopThread.joinable())
		m_tPlayLoopThread.join();

	avformat_network_deinit();

	SDL_Quit();

}

bool VideoCtl::StartPlay(const std::string& strFileName, void* widPlayWid)
{
	m_bPlayLoop = false;
	if (m_tPlayLoopThread.joinable())
	{
		m_tPlayLoopThread.join();
	}
	SigStartPlay(strFileName);//正式播放，发送给标题栏

	m_playWid = widPlayWid;

	VideoState* is;

	char file_name[1024];
	memset(file_name, 0, 1024);

	//打开流
	is = stream_open(strFileName.c_str());
	if (!is) {
		av_log(NULL, AV_LOG_FATAL, "Failed to initialize VideoState!\n");
		do_exit(m_CurStream);
	}

	{
		std::unique_lock<std::shared_mutex> lock(m_streamMutex);
		m_CurStream = is;
	}

	//事件循环
	m_tPlayLoopThread = std::thread(&VideoCtl::LoopThread, this);


	return true;
}
