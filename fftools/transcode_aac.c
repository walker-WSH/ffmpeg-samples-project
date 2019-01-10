#include "transcode_aac.h" 

int main(int argc, char **argv)
{
    AVFormatContext *input_format_context = NULL, *output_format_context = NULL;
    AVCodecContext *input_codec_context = NULL, *output_codec_context = NULL;
  
    SwrContext *resample_context = NULL;
    AVAudioFifo *fifo = NULL; 

    if (open_input_file(srcfile, &input_format_context, &input_codec_context))
        goto cleanup;
     
    if (open_output_file(destfile, input_codec_context, &output_format_context, &output_codec_context))
        goto cleanup;
     
    if (init_resampler(input_codec_context, output_codec_context, &resample_context))
        goto cleanup;
     
    if (init_fifo(&fifo, output_codec_context))
        goto cleanup;
     
    if (write_output_file_header(output_format_context))
        goto cleanup;

    while (1)
    { 
        const int output_frame_size = output_codec_context->frame_size;  /* Use the encoder's desired frame size for processing. */

        /* Make sure that there is one frame worth of samples in the FIFO
         * buffer so that the encoder can do its work.
         * Since the decoder's and the encoder's frame size may differ, we
         * need to FIFO buffer to store as many frames worth of input samples
         * that they make up at least one frame worth of output samples. */
        int finished = 0;
        while (av_audio_fifo_size(fifo) < output_frame_size)
        {
            /* Decode one frame worth of audio samples, convert it to the
             * output sample format and put it into the FIFO buffer. */
            if (read_decode_convert_and_store(fifo, input_format_context, input_codec_context,
                                              output_codec_context, resample_context, &finished))
                goto cleanup;

            /* If we are at the end of the input file, we should continue
             * encoding the remaining audio samples to the output file. */
            if (finished)
                break;
        }

        /* If we have enough samples for the encoder, we encode them.
         * At the end of the file, we pass the remaining samples to the encoder. */
        while (av_audio_fifo_size(fifo) >= output_frame_size ||
            (finished && av_audio_fifo_size(fifo) > 0))
        {
            /* Take one frame worth of audio samples from the FIFO buffer,
            * encode it and write it to the output file. */
            if (load_encode_and_write(fifo, output_format_context, output_codec_context))
                goto cleanup;
        }

        /* If we are at the end of the input file and have encoded
         * all remaining samples, we can exit this loop and finish. */
        if (finished) 
        {
            /* Flush the encoder as it may have delayed frames. */
            int data_written;
            do 
            {
                data_written = 0;
                if (encode_audio_frame(NULL, output_format_context, output_codec_context, &data_written))
                    goto cleanup;

                if (data_written)
                    printf("flush frame success \n");
                else
                    printf("flush frame end \n");
            } while (data_written);
 
            break;
        }
    }

    /* Write the trailer of the output file container. */
    if (write_output_file_trailer(output_format_context))
        goto cleanup;
     
cleanup:
    if (fifo)
        av_audio_fifo_free(fifo);

    swr_free(&resample_context);

    if (output_codec_context)
        avcodec_free_context(&output_codec_context);

    if (output_format_context)
    {
        avio_closep(&output_format_context->pb);
        avformat_free_context(output_format_context);
    }

    if (input_codec_context)
        avcodec_free_context(&input_codec_context);

    if (input_format_context)
        avformat_close_input(&input_format_context);

    return 0;
}