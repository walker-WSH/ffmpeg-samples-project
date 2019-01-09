#include "muxing.h"
 
//-------------------------------------------
int main(int argc, char **argv)
{
    AVFormatContext* pFormatContext = NULL;
    AVOutputFormat* pOutputFormat = NULL;
    OutputStream videoOutStream = { 0 };
    OutputStream audioOutStream = { 0 };
    AVCodec* pAudioCodec = NULL;
    AVCodec* pVideoCodec = NULL;
    AVDictionary* pAVDictionary = NULL;

    int bHaveVideo = 0;
    int bHaveAudio = 0;
    int bContinueEncodeVideo = 0;
    int bContinueEncodeAudio = 0;

    int ret; 
 
    /* allocate the output media context */
    avformat_alloc_output_context2(&pFormatContext, NULL, NULL, OUTPUT_FILE_NAME);
    if (!pFormatContext) 
    {
        printf("Could not deduce output format from file extension: using MPEG.\n");

        avformat_alloc_output_context2(&pFormatContext, NULL, "mpeg", OUTPUT_FILE_NAME);
        if (!pFormatContext)
            return 1;
    }

    pOutputFormat = pFormatContext->oformat;

    /* Add the audio and video streams using the default format codecs and initialize the codecs. */
    if (pOutputFormat->video_codec != AV_CODEC_ID_NONE) 
    {
        add_stream(&videoOutStream, pFormatContext, &pVideoCodec, pOutputFormat->video_codec);
        bHaveVideo = 1;
        bContinueEncodeVideo = 1;
    }

    if (pOutputFormat->audio_codec != AV_CODEC_ID_NONE)
    {
        add_stream(&audioOutStream, pFormatContext, &pAudioCodec, pOutputFormat->audio_codec);
        bHaveAudio = 1;
        bContinueEncodeAudio = 1;
    }

    /* Now that all the parameters are set, we can open the audio and video codecs and allocate the necessary encode buffers. */
    if (bHaveVideo)
        open_video(pFormatContext, pVideoCodec, &videoOutStream, pAVDictionary);

    if (bHaveAudio)
        open_audio(pFormatContext, pAudioCodec, &audioOutStream, pAVDictionary);

    av_dump_format(pFormatContext, 0, OUTPUT_FILE_NAME, 1);

    /* open the output file, if needed */
    if (!(pOutputFormat->flags & AVFMT_NOFILE)) 
    {
        ret = avio_open(&pFormatContext->pb, OUTPUT_FILE_NAME, AVIO_FLAG_WRITE);
        if (ret < 0) 
        {
            fprintf(stderr, "Could not open '%s': %s\n", OUTPUT_FILE_NAME, av_err2str(ret));
            return 1;
        }
    }

    /* Write the stream header, if any. */
    ret = avformat_write_header(pFormatContext, &pAVDictionary);
    if (ret < 0) 
    {
        fprintf(stderr, "Error occurred when opening output file: %s\n", av_err2str(ret));
        return 1;
    }

    while (bContinueEncodeVideo || bContinueEncodeAudio) 
    {
        if (bContinueEncodeVideo)
        {
            int cmp = av_compare_ts(videoOutStream.next_pts, videoOutStream.pAVCodecContext->time_base,
                                    audioOutStream.next_pts, audioOutStream.pAVCodecContext->time_base);
            if (!bContinueEncodeAudio || cmp <= 0)
            { 
                bContinueEncodeVideo = !write_video_frame(pFormatContext, &videoOutStream);
                continue;
            }
        }

        bContinueEncodeAudio = !write_audio_frame(pFormatContext, &audioOutStream);
    }

    /* The trailer must be written before you close the CodecContexts open when you wrote the header; 
    otherwise av_write_trailer() may try to use memory that was freed on av_codec_close(). */
    av_write_trailer(pFormatContext);
     
    if (bHaveVideo)
        close_stream(pFormatContext, &videoOutStream);

    if (bHaveAudio)
        close_stream(pFormatContext, &audioOutStream);

    if (!(pOutputFormat->flags & AVFMT_NOFILE)) 
        avio_closep(&pFormatContext->pb);
     
    avformat_free_context(pFormatContext); 
    return 0;
}