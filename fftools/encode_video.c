#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <libavcodec/avcodec.h>
#include <libavutil/opt.h>
#include <libavutil/imgutils.h>

static void encode(AVCodecContext *enc_ctx, AVFrame *frame, AVPacket *pkt, FILE *outfile)
{ 
    if (frame)
        printf("-------------------Send frame %lld \n", frame->pts);

    int ret = avcodec_send_frame(enc_ctx, frame);
    if (ret < 0) 
    {
        // 此处如果错误码是AVERROR_EOF，可能还可以拿到一个包，应该接着调用avcodec_receive_packet
        fprintf(stderr, "Error sending a frame for encoding\n");
        exit(1);
    }

    while (ret >= 0) 
    {
        ret = avcodec_receive_packet(enc_ctx, pkt);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
            return;

        if (ret < 0)
        {
            fprintf(stderr, "Error during encoding\n");
            exit(1);
        }

        printf("Write packet pkt::pts=%lld   pkt::size=%5d \n", pkt->pts, pkt->size);
        fwrite(pkt->data, 1, pkt->size, outfile);

        av_packet_unref(pkt);
    }
}

int main(int argc, char **argv)
{ 
    const AVCodec *codec = NULL;
    AVCodecContext *codec_context = NULL;
    AVFrame *frame;
    AVPacket *pkt;

    FILE *fpWrite;
    const char* filename = "encodeVideo.avi";
    
    //enum AVCodecID codec_id = AV_CODEC_ID_MPEG4;
    //enum AVCodecID codec_id = AV_CODEC_ID_H265;
    enum AVCodecID codec_id = AV_CODEC_ID_H264;

    //enum AVPixelFormat video_format = AV_PIX_FMT_RGB24; // RGB类型的格式 avcodec_open2会失败
    //enum AVPixelFormat video_format = AV_PIX_FMT_YUV420P;
    enum AVPixelFormat video_format = AV_PIX_FMT_YUV422P;

    codec = avcodec_find_encoder(codec_id);
    if (!codec) 
    {
        fprintf(stderr, "Codec '%u' not found\n", codec_id);
        exit(1);
    }

    codec_context = avcodec_alloc_context3(codec);
    if (!codec_context)
    {
        fprintf(stderr, "Could not allocate video codec context\n");
        exit(1);
    }

    pkt = av_packet_alloc();
    if (!pkt)
        exit(1);

    codec_context->bit_rate = 400000;

    /* resolution must be a multiple of two */
    codec_context->width = 352;
    codec_context->height = 288;

    // fps == 25
    codec_context->time_base = (AVRational){1, 25};
    codec_context->framerate = (AVRational){25, 1};

    codec_context->gop_size = 10; // 如果frame->pict_type是AV_PICTURE_TYPE_I 编码器会忽略gop 每一帧都会编码为I帧
    codec_context->max_b_frames = 1;
    codec_context->pix_fmt = video_format;

    if (codec->id == AV_CODEC_ID_H264)
        av_opt_set(codec_context->priv_data, "preset", "slow", 0); // priv_data 属于每个编码器特有的设置域，用av_opt_set 设置
     
    int ret = avcodec_open2(codec_context, codec, NULL);
    if (ret < 0)
    {
        fprintf(stderr, "Could not open codec: %s\n", av_err2str(ret));
        exit(1);
    }

    fpWrite = fopen(filename, "wb");
    if (!fpWrite) 
    {
        fprintf(stderr, "Could not open %s\n", filename);
        exit(1);
    }

    frame = av_frame_alloc();
    if (!frame)
    {
        fprintf(stderr, "Could not allocate video frame\n");
        exit(1);
    }
    frame->format = codec_context->pix_fmt;
    frame->width  = codec_context->width;
    frame->height = codec_context->height;

    ret = av_frame_get_buffer(frame, 32);
    if (ret < 0) 
    {
        fprintf(stderr, "Could not allocate the video frame data\n");
        exit(1);
    }

    /* encode 1 second of video */
    for (int i = 0; i < 25; i++)
    {
        fflush(stdout);

        /* make sure the frame data is writable */
        ret = av_frame_make_writable(frame);
        if (ret < 0)
            exit(1);

        if (video_format == AV_PIX_FMT_YUV420P)
        {
            /* Y */
            for (int y = 0; y < codec_context->height; y++)
            {
                for (int x = 0; x < codec_context->width; x++)
                {
                    frame->data[0][y * frame->linesize[0] + x] = x + y + i * 3;
                }
            }
            /* Cb and Cr */
            for (int y = 0; y < codec_context->height / 2; y++)
            {
                for (int x = 0; x < codec_context->width / 2; x++)
                {
                    frame->data[1][y * frame->linesize[1] + x] = 128 + y + i * 2;
                    frame->data[2][y * frame->linesize[2] + x] = 64 + x + i * 5;
                }
            }
        }
        else
        {

        }
         
        frame->pts = i;
        encode(codec_context, frame, pkt, fpWrite);
    }

    /* flush the encoder */
    encode(codec_context, NULL, pkt, fpWrite);

    /* add sequence end code to have a real MPEG file */
    uint8_t endcode[] = { 0, 0, 1, 0xb7 };
    fwrite(endcode, 1, sizeof(endcode), fpWrite);
    fclose(fpWrite);

    avcodec_free_context(&codec_context);
    av_frame_free(&frame);
    av_packet_free(&pkt);

    return 0;
}