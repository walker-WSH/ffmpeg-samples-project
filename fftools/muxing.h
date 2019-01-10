#pragma  once
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <windows.h>
 
#include <libavutil/avassert.h>
#include <libavutil/channel_layout.h>
#include <libavutil/opt.h>
#include <libavutil/mathematics.h>
#include <libavutil/timestamp.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h> 

#define OUTPUT_FILE_NAME    "testmuxing.mp4"
#define STREAM_DURATION     20.0

#define STREAM_FRAME_RATE   25 // fps
#define STREAM_PIX_FMT      AV_PIX_FMT_YUV420P /* default pix_fmt */
 
//----------------------------------------------
typedef struct OutputStream
{ 
    float t, tincr, tincr2; // 作为生成虚假声音的参数

    /* pts of the next frame that will be generated */
    int64_t next_pts; // 视频每产生一个AVFrame增加1， 音频每产生一个AVFrame增加nb_samples
    int samples_count;

    //----------------------------------------------
    // User is required to call avcodec_close() and avformat_free_context() to
    // clean up the allocation by avformat_new_stream().
    AVStream *pAVSteam; 

    AVCodecContext *pAVCodecContext;

    AVFrame *frame;  
    AVFrame *tmp_frame; // for converting source data to the required output format

    struct SwsContext *sws_ctx_video;
    struct SwrContext *swr_ctx_audio;    

}OutputStream;

//---------------------------------------------- 
static void add_stream(OutputStream *ostOutput, AVFormatContext *fmtCtx, AVCodec **codecOutput, enum AVCodecID codec_id)
{
    int i;

    AVCodec* pAVCodec = avcodec_find_encoder(codec_id);

    *codecOutput = pAVCodec;
    if (!(*codecOutput))
    {
        fprintf(stderr, "Could not find encoder for '%s'\n", avcodec_get_name(codec_id));
        exit(1);
    }

    ostOutput->pAVSteam = avformat_new_stream(fmtCtx, NULL);
    if (!ostOutput->pAVSteam)
    {
        fprintf(stderr, "Could not allocate stream\n");
        exit(1);
    }

    ostOutput->pAVSteam->id = fmtCtx->nb_streams - 1;

    AVCodecContext* codecContext = avcodec_alloc_context3(pAVCodec);
    if (!codecContext)
    {
        fprintf(stderr, "Could not alloc an encoding context\n");
        exit(1);
    }

    ostOutput->pAVCodecContext = codecContext;

    switch (pAVCodec->type)
    {
    case AVMEDIA_TYPE_AUDIO:
        codecContext->sample_fmt = pAVCodec->sample_fmts ? pAVCodec->sample_fmts[0] : AV_SAMPLE_FMT_FLTP;
        codecContext->bit_rate = 64000;
        codecContext->sample_rate = 44100;
        if (pAVCodec->supported_samplerates)
        {
            codecContext->sample_rate = pAVCodec->supported_samplerates[0];
            for (i = 0; pAVCodec->supported_samplerates[i]; i++)
            {
                if (pAVCodec->supported_samplerates[i] == 44100)
                    codecContext->sample_rate = 44100;
            }
        }

        codecContext->channel_layout = AV_CH_LAYOUT_STEREO;
        if (pAVCodec->channel_layouts)
        {
            codecContext->channel_layout = pAVCodec->channel_layouts[0];
            for (i = 0; pAVCodec->channel_layouts[i]; i++)
            {
                if (pAVCodec->channel_layouts[i] == AV_CH_LAYOUT_STEREO)
                    codecContext->channel_layout = AV_CH_LAYOUT_STEREO;
            }
        }
        codecContext->channels = av_get_channel_layout_nb_channels(codecContext->channel_layout);
        ostOutput->pAVSteam->time_base = (AVRational) { 1, codecContext->sample_rate };
        break;

    case AVMEDIA_TYPE_VIDEO:
        codecContext->codec_id = codec_id;

        codecContext->bit_rate = 400000;
        codecContext->width = 352; /* Resolution must be a multiple of two. */
        codecContext->height = 288; /* Resolution must be a multiple of two. */

                                    /* timebase: This is the fundamental unit of time (in seconds) in terms
                                    * of which frame timestamps are represented. For fixed-fps content,
                                    * timebase should be 1/framerate and timestamp increments should be
                                    * identical to 1. */
        AVRational timeRational = { 1, STREAM_FRAME_RATE };
        ostOutput->pAVSteam->time_base = timeRational;
        codecContext->time_base = timeRational;

        codecContext->gop_size = 12; /* emit one intra frame every twelve frames at most */
        codecContext->pix_fmt = STREAM_PIX_FMT;

        if (codecContext->codec_id == AV_CODEC_ID_MPEG2VIDEO)
        {
            codecContext->max_b_frames = 2; // just for testing, we also add B-frames
        }
        if (codecContext->codec_id == AV_CODEC_ID_MPEG1VIDEO)
        {
            /* Needed to avoid using macroblocks in which some coeffs overflow.
            * This does not happen with normal video, it just happens here as
            * the motion of the chroma plane does not match the luma plane. */
            codecContext->mb_decision = 2;
        }
        break;

    default:
        break;
    }

    /* Some formats want stream headers to be separate. */
    if (fmtCtx->oformat->flags & AVFMT_GLOBALHEADER)
        codecContext->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
}

static void log_packet(const AVFormatContext *fmt_ctx, const AVPacket *pkt)
{
    AVRational *time_base = &fmt_ctx->streams[pkt->stream_index]->time_base;

    printf("pts:%s pts_time:%s dts:%s dts_time:%s duration:%s duration_time:%s stream_index:%d\n",
        av_ts2str(pkt->pts), av_ts2timestr(pkt->pts, time_base),
        av_ts2str(pkt->dts), av_ts2timestr(pkt->dts, time_base),
        av_ts2str(pkt->duration), av_ts2timestr(pkt->duration, time_base),
        pkt->stream_index);
}

static int write_frame(AVFormatContext *fmt_ctx, const AVRational *time_base, AVStream *st, AVPacket *pkt)
{
    /* rescale output packet timestamp values from codec to stream timebase */
    av_packet_rescale_ts(pkt, *time_base, st->time_base);
    pkt->stream_index = st->index;

    /* Write the compressed frame to the media file. */
    log_packet(fmt_ctx, pkt);
    return av_interleaved_write_frame(fmt_ctx, pkt);
}

static void close_stream(AVFormatContext *oc, OutputStream *ost)
{
    avcodec_free_context(&ost->pAVCodecContext);

    av_frame_free(&ost->frame);
    av_frame_free(&ost->tmp_frame);
    
    sws_freeContext(ost->sws_ctx_video);
    swr_free(&ost->swr_ctx_audio);
}

//----------------------------------------------------------------------------
static AVFrame *alloc_picture(enum AVPixelFormat pix_fmt, int width, int height)
{
    AVFrame* picture = av_frame_alloc();
    if (!picture)
        return NULL;

    picture->format = pix_fmt;
    picture->width = width;
    picture->height = height;

    /* allocate the buffers for the frame data */
    int ret = av_frame_get_buffer(picture, 32);
    if (ret < 0)
    {
        fprintf(stderr, "Could not allocate frame data.\n");
        exit(1);
    }

    return picture;
}

static void open_video(AVFormatContext *oc, AVCodec *codec, OutputStream *ost, AVDictionary *opt_arg)
{
    AVCodecContext *codecContext = ost->pAVCodecContext;

    AVDictionary *opt = NULL;
    av_dict_copy(&opt, opt_arg, 0);

    int ret = avcodec_open2(codecContext, codec, &opt);
    av_dict_free(&opt);
    if (ret < 0)
    {
        fprintf(stderr, "Could not open video codec: %s\n", av_err2str(ret));
        exit(1);
    }

    /* allocate and init a re-usable frame */
    ost->frame = alloc_picture(codecContext->pix_fmt, codecContext->width, codecContext->height);
    if (!ost->frame)
    {
        fprintf(stderr, "Could not allocate video frame\n");
        exit(1);
    }

    /* If the output format is not YUV420P, then a temporary YUV420P picture is needed too.
    It is then converted to the required output format. */
    ost->tmp_frame = NULL;
    if (codecContext->pix_fmt != AV_PIX_FMT_YUV420P)
    {
        ost->tmp_frame = alloc_picture(AV_PIX_FMT_YUV420P, codecContext->width, codecContext->height);
        if (!ost->tmp_frame)
        {
            fprintf(stderr, "Could not allocate temporary picture\n");
            exit(1);
        }
    }

    /* copy the stream parameters to the muxer */
    ret = avcodec_parameters_from_context(ost->pAVSteam->codecpar, codecContext);
    if (ret < 0)
    {
        fprintf(stderr, "Could not copy the stream parameters\n");
        exit(1);
    }
}

static void fill_yuv_image(AVFrame *pict, int frame_index, int width, int height)
{
    int i = frame_index;

    /* Y */
    for (int y = 0; y < height; y++)
        for (int x = 0; x < width; x++)
            pict->data[0][y * pict->linesize[0] + x] = x + y + i * 3;

    /* Cb and Cr */
    for (int y = 0; y < height / 2; y++)
    {
        for (int x = 0; x < width / 2; x++)
        {
            pict->data[1][y * pict->linesize[1] + x] = 128 + y + i * 2;
            pict->data[2][y * pict->linesize[2] + x] = 64 + x + i * 5;
        }
    }
}

static AVFrame *get_video_frame(OutputStream *ost)
{
    AVCodecContext* codecContext = ost->pAVCodecContext;
     
    if (av_compare_ts(ost->next_pts, codecContext->time_base, STREAM_DURATION, (AVRational) { 1, 1 }) >= 0)
        return NULL; // 如果时长够了 就停止

    /* when we pass a frame to the encoder, it may keep a reference to it internally;
    make sure we do not overwrite it here */
    if (av_frame_make_writable(ost->frame) < 0)
        exit(1);

    if (codecContext->pix_fmt != AV_PIX_FMT_YUV420P)
    {
        /* as we only generate a YUV420P picture, we must convert it to the codec pixel format if needed */
        if (!ost->sws_ctx_video)
        {
            ost->sws_ctx_video = sws_getContext(codecContext->width, codecContext->height, AV_PIX_FMT_YUV420P,
                codecContext->width, codecContext->height, codecContext->pix_fmt,
                SWS_BICUBIC, NULL, NULL, NULL);
            if (!ost->sws_ctx_video)
            {
                fprintf(stderr, "Could not initialize the conversion context\n");
                exit(1);
            }
        }

        fill_yuv_image(ost->tmp_frame, ost->next_pts, codecContext->width, codecContext->height);
        sws_scale(ost->sws_ctx_video,
            (const uint8_t* const*)ost->tmp_frame->data, ost->tmp_frame->linesize, 0, codecContext->height,
            ost->frame->data, ost->frame->linesize);
    }
    else
    {
        fill_yuv_image(ost->frame, ost->next_pts, codecContext->width, codecContext->height);
    }

    ost->frame->pts = ost->next_pts++;
    return ost->frame;
}

/* encode one video frame and send it to the muxer. Return 1 when encoding is finished, 0 otherwise */
static int write_video_frame(AVFormatContext *oc, OutputStream *ost)
{
    AVCodecContext* codecContext = ost->pAVCodecContext;

    AVFrame* frame = get_video_frame(ost);
    avcodec_send_frame(codecContext, frame); // 如果frame是null 就是flush缓存中的帧

    AVPacket pkt = { 0 };
    av_init_packet(&pkt);

    int got_packet = (0 == avcodec_receive_packet(codecContext, &pkt));
    if (got_packet)
    {
        int ret = write_frame(oc, &codecContext->time_base, ost->pAVSteam, &pkt);
        if (ret < 0)
        {
            fprintf(stderr, "Error while writing video frame: %s\n", av_err2str(ret));
            exit(1);
        }
    }

    return (frame || got_packet) ? 0 : 1; // 0 : 成功，app应该继续调用该函数进行编码
}

//----------------------------------------------------------------------------
static AVFrame *alloc_audio_frame(enum AVSampleFormat sample_fmt, uint64_t channel_layout, int sample_rate, int nb_samples)
{
    AVFrame *frame = av_frame_alloc();
    if (!frame)
    {
        fprintf(stderr, "Error allocating an audio frame\n");
        exit(1);
    }

    frame->format = sample_fmt;
    frame->channel_layout = channel_layout;
    frame->sample_rate = sample_rate;
    frame->nb_samples = nb_samples;

    if (nb_samples)
    {
        int ret = av_frame_get_buffer(frame, 0);
        if (ret < 0)
        {
            fprintf(stderr, "Error allocating an audio buffer\n");
            exit(1);
        }
    }

    return frame;
}

static void open_audio(AVFormatContext *oc, AVCodec *codec, OutputStream *ost, AVDictionary *opt_arg)
{
    AVCodecContext* c = ost->pAVCodecContext;

    AVDictionary *opt = NULL;
    av_dict_copy(&opt, opt_arg, 0);

    int ret = avcodec_open2(c, codec, &opt);
    av_dict_free(&opt);
    if (ret < 0)
    {
        fprintf(stderr, "Could not open audio codec: %s\n", av_err2str(ret));
        exit(1);
    }

    ost->t = 0;
    ost->tincr = 2 * M_PI * 110.0 / c->sample_rate;
    ost->tincr2 = 2 * M_PI * 110.0 / c->sample_rate / c->sample_rate;

    int nb_samples;
    if (c->codec->capabilities & AV_CODEC_CAP_VARIABLE_FRAME_SIZE)
        nb_samples = 10000;
    else
        nb_samples = c->frame_size;

    ost->frame = alloc_audio_frame(c->sample_fmt, c->channel_layout, c->sample_rate, nb_samples);
    ost->tmp_frame = alloc_audio_frame(AV_SAMPLE_FMT_S16, c->channel_layout, c->sample_rate, nb_samples);

    /* copy the stream parameters to the muxer */
    ret = avcodec_parameters_from_context(ost->pAVSteam->codecpar, c);
    if (ret < 0)
    {
        fprintf(stderr, "Could not copy the stream parameters\n");
        exit(1);
    }

    /* create resampler context */
    ost->swr_ctx_audio = swr_alloc();
    if (!ost->swr_ctx_audio)
    {
        fprintf(stderr, "Could not allocate resampler context\n");
        exit(1);
    }

    /* set input options */
    av_opt_set_int(ost->swr_ctx_audio, "in_channel_count", c->channels, 0);
    av_opt_set_int(ost->swr_ctx_audio, "in_sample_rate", c->sample_rate, 0);
    av_opt_set_sample_fmt(ost->swr_ctx_audio, "in_sample_fmt", AV_SAMPLE_FMT_S16, 0);

    /* set output options */
    av_opt_set_int(ost->swr_ctx_audio, "out_channel_count", c->channels, 0);
    av_opt_set_int(ost->swr_ctx_audio, "out_sample_rate", c->sample_rate, 0);
    av_opt_set_sample_fmt(ost->swr_ctx_audio, "out_sample_fmt", c->sample_fmt, 0);

    if ((ret = swr_init(ost->swr_ctx_audio)) < 0)
    {
        fprintf(stderr, "Failed to initialize the resampling context\n");
        exit(1);
    }
}

/* Prepare a 16 bit dummy audio frame of 'frame_size' samples and nb_channels' channels. */
static AVFrame *get_audio_frame(OutputStream *ost)
{
    /* check if we want to generate more frames */
    if (av_compare_ts(ost->next_pts, ost->pAVCodecContext->time_base, STREAM_DURATION, (AVRational) { 1, 1 }) >= 0)
        return NULL;

    AVFrame* frame = ost->tmp_frame;
    int16_t* q = (int16_t*)frame->data[0];
    for (int j = 0; j < frame->nb_samples; j++)
    {
        int v = (int)(sin(ost->t) * 10000);
        for (int i = 0; i < ost->pAVCodecContext->channels; i++)
            *q++ = v;

        ost->t += ost->tincr;
        ost->tincr += ost->tincr2;
    }

    frame->pts = ost->next_pts;
    ost->next_pts += frame->nb_samples;

    return frame;
}

/* encode one audio frame and send it to the muxer return 1 when encoding is finished, 0 otherwise */
static int write_audio_frame(AVFormatContext *oc, OutputStream *ost)
{
    int ret;

    AVPacket pkt = { 0 }; // data and size must be 0;
    av_init_packet(&pkt);

    AVCodecContext *c = ost->pAVCodecContext;
    AVFrame *frame = get_audio_frame(ost);

    if (frame)
    {
        /* convert samples from native format to destination codec format, using the resampler */
        /* compute destination number of samples */
        int dst_nb_samples = av_rescale_rnd(swr_get_delay(ost->swr_ctx_audio, c->sample_rate) + frame->nb_samples, c->sample_rate, c->sample_rate, AV_ROUND_UP);
        av_assert0(dst_nb_samples == frame->nb_samples);

        /* when we pass a frame to the encoder, it may keep a reference to it
        * internally; make sure we do not overwrite it here */
        ret = av_frame_make_writable(ost->frame);
        if (ret < 0)
            exit(1);

        /* convert to destination format */
        ret = swr_convert(ost->swr_ctx_audio,
            ost->frame->data, dst_nb_samples,
            (const uint8_t **)frame->data, frame->nb_samples);
        if (ret < 0)
        {
            fprintf(stderr, "Error while converting\n");
            exit(1);
        }

        frame = ost->frame;
        frame->pts = av_rescale_q(ost->samples_count, (AVRational) { 1, c->sample_rate }, c->time_base);
        ost->samples_count += dst_nb_samples;
    }

    avcodec_send_frame(c, frame); // 如果frame是null 就是flush缓存中的帧
    int got_packet = (0 == avcodec_receive_packet(c, &pkt));
    if (got_packet)
    {
        ret = write_frame(oc, &c->time_base, ost->pAVSteam, &pkt);
        if (ret < 0)
        {
            fprintf(stderr, "Error while writing audio frame: %s\n", av_err2str(ret));
            exit(1);
        }
    }

    return (frame || got_packet) ? 0 : 1; // 0 : 成功，app应该继续调用该函数进行编码
}