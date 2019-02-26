/**
 * This example shows how to do HW-accelerated decoding with output
 * frames from the HW video surfaces.
 */
#if 1
#include <stdio.h>
#include <windows.h>

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/pixdesc.h>
#include <libavutil/hwcontext.h>
#include <libavutil/opt.h>
#include <libavutil/avassert.h>
#include <libavutil/imgutils.h>

AVStream *video_stream = NULL;
AVBufferRef *hw_device_ctx = NULL;
enum AVPixelFormat hw_pix_fmt = AV_PIX_FMT_NONE;

FILE *output_file = NULL;

static enum AVPixelFormat get_hw_format_func(AVCodecContext *ctx, const enum AVPixelFormat *pix_fmts)
{
    const enum AVPixelFormat *p = pix_fmts;
    for (; *p != -1; p++) 
    {
        if (*p == hw_pix_fmt)
            return *p;
    }

    fprintf(stderr, "Failed to get HW surface format.\n");
    return AV_PIX_FMT_NONE;
}

static int decode_write(AVCodecContext *avctx, AVPacket *packet)
{
    AVFrame *frame = NULL, *sw_frame = NULL;
    AVFrame *tmp_frame = NULL;
    uint8_t *buffer = NULL;
    int size;
    int ret = 0;

    ret = avcodec_send_packet(avctx, packet);
    if (ret < 0) 
    {
        fprintf(stderr, "Error during decoding\n");
        return ret;
    }

    while (1)
    {
        if (!(frame = av_frame_alloc()) || !(sw_frame = av_frame_alloc()))
        {
            fprintf(stderr, "Can not alloc frame\n");
            ret = AVERROR(ENOMEM);
            goto fail;
        }

        ret = avcodec_receive_frame(avctx, frame);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
        {
            av_frame_free(&frame);
            av_frame_free(&sw_frame);
            return 0;
        } 
        else if (ret < 0)
        {
            fprintf(stderr, "Error while decoding\n");
            goto fail;
        }

        int64_t st = frame->pts * av_q2d(video_stream->time_base);
        if (frame->format == hw_pix_fmt) 
        {
            /* retrieve data from GPU to CPU */
            if ((ret = av_hwframe_transfer_data(sw_frame, frame, 0)) < 0)
            {
                fprintf(stderr, "Error transferring the data to system memory\n");
                goto fail;
            }
            tmp_frame = sw_frame;  
            fprintf(stderr, "get frame by GPU %lld s.\n", st);
        }
        else
        {
            tmp_frame = frame;
            fprintf(stderr, "get frame by CPU %lld s.\n", st);
        }

        int buffer_align = 1;
         
        size = av_image_get_buffer_size(tmp_frame->format, tmp_frame->width, tmp_frame->height, buffer_align);
        buffer = av_malloc(size);
        if (!buffer)
        {
            fprintf(stderr, "Can not alloc buffer\n");
            ret = AVERROR(ENOMEM);
            goto fail;
        }

        ret = av_image_copy_to_buffer(buffer, size,
            (const uint8_t * const *)tmp_frame->data,
            (const int *)tmp_frame->linesize,
            tmp_frame->format,
            tmp_frame->width,
            tmp_frame->height,
            buffer_align);

        if (ret < 0)
        {
            fprintf(stderr, "Can not copy image to buffer\n");
            goto fail;
        }

        if ((ret = fwrite(buffer, 1, size, output_file)) < 0)
        {
            fprintf(stderr, "Failed to dump raw data.\n");
            goto fail;
        }

    fail:
        av_frame_free(&frame);
        av_frame_free(&sw_frame);
        av_freep(&buffer);

        if (ret < 0)
            return ret;
    }
}

int main(int argc, char *argv[])
{
    AVFormatContext *input_ctx = NULL;
    int video_stream_index, ret;
    AVCodecContext *decoder_ctx = NULL;
    AVCodec *decoder = NULL;
    AVPacket packet;
    enum AVHWDeviceType hw_type = AV_HWDEVICE_TYPE_NONE;

    while ((hw_type = av_hwdevice_iterate_types(hw_type)) != AV_HWDEVICE_TYPE_NONE)
        fprintf(stderr, "Available device types: %s\n", av_hwdevice_get_type_name(hw_type));

    hw_type = AV_HWDEVICE_TYPE_NONE;
    while ((hw_type = av_hwdevice_iterate_types(hw_type)) != AV_HWDEVICE_TYPE_NONE)
    {
        if (hw_type != AV_HWDEVICE_TYPE_NONE)
        { 
            fprintf(stderr, "\nused device types: %s\n", av_hwdevice_get_type_name(hw_type));
            break;
        }
    }
     
    if (avformat_open_input(&input_ctx, "test.mp4", NULL, NULL) != 0)
    {
        fprintf(stderr, "Cannot open input file \n");
        return -1;
    }

    if (avformat_find_stream_info(input_ctx, NULL) < 0) 
    {
        fprintf(stderr, "Cannot find input stream information.\n");
        return -1;
    }
     
    ret = av_find_best_stream(input_ctx, AVMEDIA_TYPE_VIDEO, -1, -1, &decoder, 0);
    if (ret < 0) 
    {
        fprintf(stderr, "Cannot find a video stream in the input file\n");
        return -1;
    }

    video_stream_index = ret;
    video_stream = input_ctx->streams[video_stream_index];
     
    for (int i = 0;; i++)
    {
        const AVCodecHWConfig *config = avcodec_get_hw_config(decoder, i);
        if (!config)
        {
            fprintf(stderr, "Decoder %s does not support device type %s.\n", decoder->name, av_hwdevice_get_type_name(hw_type));
            return -1;
        }

        if ((config->methods & AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX) &&
            config->device_type == hw_type) 
        {
            hw_pix_fmt = config->pix_fmt;
            break;
        }
    }

    if (!(decoder_ctx = avcodec_alloc_context3(decoder)))
        return AVERROR(ENOMEM);

    int64_t duration = video_stream->duration * av_q2d(video_stream->time_base);
    fprintf(stderr, "duration: %lld seconds \n\n", duration);

    if (avcodec_parameters_to_context(decoder_ctx, video_stream->codecpar) < 0)
        return -1;

    if ((ret = av_hwdevice_ctx_create(&hw_device_ctx, hw_type, NULL, NULL, 0)) < 0)
    {
        fprintf(stderr, "Failed to create specified HW device.\n");
        return -1;
    }

    decoder_ctx->hw_device_ctx = av_buffer_ref(hw_device_ctx);
    decoder_ctx->get_format = get_hw_format_func;

    if ((ret = avcodec_open2(decoder_ctx, decoder, NULL)) < 0) 
    {
        fprintf(stderr, "Failed to open codec for stream #%u\n", video_stream_index);
        return -1;
    }

    /* open the file to dump raw data */
    output_file = fopen("HWtestOut.flv", "w+");

    /* actual decoding and dump the raw data */
    while (ret >= 0) 
    {
        if ((ret = av_read_frame(input_ctx, &packet)) < 0)
            break;

        if (video_stream_index == packet.stream_index)
            ret = decode_write(decoder_ctx, &packet);

        av_packet_unref(&packet);
    }

    /* flush the decoder */
    packet.data = NULL;
    packet.size = 0;
    ret = decode_write(decoder_ctx, &packet);
    av_packet_unref(&packet);

    if (output_file)
        fclose(output_file);

    avcodec_free_context(&decoder_ctx);
    avformat_close_input(&input_ctx);
    av_buffer_unref(&hw_device_ctx);

    getchar();
    return 0;
}
#endif