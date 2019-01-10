#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include <libavcodec/avcodec.h>
#include <libavutil/channel_layout.h>
#include <libavutil/common.h>
#include <libavutil/frame.h>
#include <libavutil/samplefmt.h>
 
static int is_sample_fmt_supported_by_encoder(const AVCodec *codec, enum AVSampleFormat sample_fmt);

// 选择距离44100最近的采样率
static int select_sample_rate(const AVCodec *codec);

// 选择最高的输出channel
static int select_channel_layout(const AVCodec *codec);

static void encode(AVCodecContext *ctx, AVFrame *frame, AVPacket *pkt, FILE *output);

int main(int argc, char **argv)
{ 
    const AVCodec *codec = NULL;
    AVCodecContext *codec_context= NULL;
    AVFrame *frame = NULL;
    AVPacket *pkt = NULL;

    char * filename = "encodeAudio.mp3";
    FILE *fpWriter = NULL;
    fpWriter = fopen(filename, "wb");
    if (!fpWriter)
    {
        fprintf(stderr, "Could not open %s\n", filename);
        exit(1);
    }

    enum AVCodecID codecID = AV_CODEC_ID_MP2;
    enum AVSampleFormat format = AV_SAMPLE_FMT_S16;
      
    codec = avcodec_find_encoder(codecID);
    if (!codec)
    {
        fprintf(stderr, "Codec not found\n");
        exit(1);
    }

    if (!is_sample_fmt_supported_by_encoder(codec, format))
    {
        fprintf(stderr, "Encoder does not support sample format %s", av_get_sample_fmt_name(format));
        exit(1);
    }

    codec_context = avcodec_alloc_context3(codec);
    if (!codec_context)
    {
        fprintf(stderr, "Could not allocate audio codec context\n");
        exit(1);
    }

    codec_context->bit_rate       = 64000; // 64 Kbps
    codec_context->sample_fmt     = format;
    codec_context->sample_rate    = select_sample_rate(codec);
    codec_context->channel_layout = select_channel_layout(codec);
    codec_context->channels       = av_get_channel_layout_nb_channels(codec_context->channel_layout);

    if (avcodec_open2(codec_context, codec, NULL) < 0) 
    {
        fprintf(stderr, "Could not open codec\n");
        exit(1);
    }

    pkt = av_packet_alloc();   /* packet for holding encoded output */
    if (!pkt)
    {
        fprintf(stderr, "could not allocate the packet\n");
        exit(1);
    }

    frame = av_frame_alloc();    /* frame containing input raw audio */
    if (!frame) 
    {
        fprintf(stderr, "Could not allocate audio frame\n");
        exit(1);
    }

    frame->format = codec_context->sample_fmt;
    frame->nb_samples     = codec_context->frame_size;
    frame->channel_layout = codec_context->channel_layout;

    int ret = av_frame_get_buffer(frame, 0);    /* allocate the data buffers */
    if (ret < 0) 
    {
        fprintf(stderr, "Could not allocate audio data buffers\n");
        exit(1);
    }

    float t = 0;
    float tincr = 2 * M_PI * 440.0 / codec_context->sample_rate;
    for (int i = 0; i < 200; i++) 
    {
        /* make sure the frame is writable -- makes a copy if the encoder kept a reference internally */
        ret = av_frame_make_writable(frame);
        if (ret < 0)
            exit(1);

        uint16_t* samples = (uint16_t*)frame->data[0];
        for (int j = 0; j < codec_context->frame_size; j++)  // 生成模拟的数据
        {
            samples[2*j] = (int)(sin(t) * 10000);
            for (int k = 1; k < codec_context->channels; k++)
                samples[2*j + k] = samples[2*j];
            t += tincr;
        }

        encode(codec_context, frame, pkt, fpWriter);
    }

    encode(codec_context, NULL, pkt, fpWriter);    /* flush the encoder */

    fclose(fpWriter);

    av_frame_free(&frame);
    av_packet_free(&pkt);
    avcodec_free_context(&codec_context);

    return 0;
}

static int is_sample_fmt_supported_by_encoder(const AVCodec *codec, enum AVSampleFormat sample_fmt)
{
    const enum AVSampleFormat *p = codec->sample_fmts;
    while (*p != AV_SAMPLE_FMT_NONE)
    {
        if (*p == sample_fmt)
            return 1;
        p++;
    }
    return 0;
}
 
static int select_sample_rate(const AVCodec *codec)
{
    const int *p;

    if (!codec->supported_samplerates)
        return 44100;

    int best_samplerate = 0;
    p = codec->supported_samplerates;
    while (*p)
    {
        if (!best_samplerate || abs(44100 - *p) < abs(44100 - best_samplerate))
            best_samplerate = *p;
        p++;
    }
    return best_samplerate;
}

static int select_channel_layout(const AVCodec *codec)
{
    if (!codec->channel_layouts)
        return AV_CH_LAYOUT_STEREO;

    uint64_t best_ch_layout = 0;
    int best_nb_channels = 0;

    const uint64_t *p = codec->channel_layouts;
    while (*p)
    {
        int nb_channels = av_get_channel_layout_nb_channels(*p);
        if (nb_channels > best_nb_channels)
        {
            best_ch_layout = *p;
            best_nb_channels = nb_channels;
        }
        p++;
    }
    return best_ch_layout;
}

static void encode(AVCodecContext *ctx, AVFrame *frame, AVPacket *pkt, FILE *output)
{
    if (frame)
        printf("-------------------Send frame  \n");

    int ret = avcodec_send_frame(ctx, frame);
    if (ret < 0)
    {
        fprintf(stderr, "Error sending the frame to the encoder\n");
        exit(1);
    }

    /* read all the available output packets (in general there may be any number of them */
    while (ret >= 0)
    {
        ret = avcodec_receive_packet(ctx, pkt);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
            return;

        if (ret < 0)
        {
            fprintf(stderr, "Error encoding audio frame\n");
            exit(1);
        }

        fwrite(pkt->data, 1, pkt->size, output);
        printf("Write packet pkt::size=%5d \n", pkt->size);

        av_packet_unref(pkt);
    }
}