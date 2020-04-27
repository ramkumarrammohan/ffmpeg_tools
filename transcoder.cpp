#include "transcoder.h"

#ifdef __cplusplus
extern "C" {
#include <libavutil/opt.h>
#include <libswscale/swscale.h>
}
#endif

#include <QGuiApplication>
#include <QPainter>
#include <QDebug>

#define WIDTH 640
#define HEIGHT 360

struct TranscoderData
{
    TranscoderData()
    {
        input = new StreamingContext;
        output = new StreamingContext;
    }
    ~TranscoderData()
    {
        if (input)
            delete input;
        if (output)
            delete output;
    }
    StreamingContext *input = nullptr;
    StreamingContext *output = nullptr;
    struct SwsContext *swsCtx = nullptr;
    struct SwsContext *swsRGB = nullptr;
    AVFrame *frameRGB = nullptr;
};

Transcoder::Transcoder()
{
    initTranscoderData();
}

Transcoder::~Transcoder()
{
    if (d)
        delete d;
}

void Transcoder::transcode(const TranscodeParams &params)
{
    int response = 0;
    if ((response = copyFilenames(params)) != 0)
    {
        qWarning() << "file name copy failed";
        return;
    }

    if ((response = openMediaInput(d->input)) != 0)
    {
        qWarning() << "failed to open input media file. " << averror2string(response);
        return;
    }

    if ((response = findAVStreams(d->input)) != 0)
    {
        qWarning() << "could not able to find media streams. " << averror2string(response);
        return;
    }

    if ((response = prepareDecoder(d->input->streamVideo, &d->input->codecVideo, &d->input->codecCtxVideo)) != 0)
    {
        qWarning() << "video decoder prepare failed. " << averror2string(response);
        return;
    }

    if ((response = openMediaOutput(d->output)) != 0)
    {
        qWarning() << "failed to open output media file. " << averror2string(response);
        return;
    }

    AVRational ipFrameRate = av_guess_frame_rate(d->input->formatCtx, d->input->streamVideo, NULL);
    if ((response = prepareEncoder(ipFrameRate, params, d->output)) != 0)
    {
        qWarning() << "video encoder prepare failed";
        return;
    }

    if (d->output->formatCtx->oformat->flags & AVFMT_GLOBALHEADER)
        d->output->formatCtx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

    if (!(d->output->formatCtx->oformat->flags & AVFMT_NOFILE))
    {
        if (avio_open(&d->output->formatCtx->pb, d->output->fileName, AVIO_FLAG_WRITE) < 0)
        {
            qWarning() << "could not open output file for write data";
            return;
        }
    }

    // RGB frame init & mem alloc
    d->frameRGB = av_frame_alloc();
    int numBytesRGB = avpicture_get_size(AV_PIX_FMT_RGB24,
                                         d->output->codecCtxVideo->width, d->output->codecCtxVideo->height);
    uint8_t *rgbFrameBuffer = (uint8_t *)av_malloc(numBytesRGB * sizeof(uint8_t));
    avpicture_fill((AVPicture*)d->frameRGB, rgbFrameBuffer, AV_PIX_FMT_RGB24,
                   d->output->codecCtxVideo->width, d->output->codecCtxVideo->height);
    d->frameRGB->format = AV_PIX_FMT_RGB24;
    d->frameRGB->width = d->output->codecCtxVideo->width;
    d->frameRGB->height = d->output->codecCtxVideo->height;
    d->swsRGB = sws_getContext(d->input->codecCtxVideo->width,
                               d->input->codecCtxVideo->height,
                               d->input->codecCtxVideo->pix_fmt,
                               d->output->codecCtxVideo->width,
                               d->output->codecCtxVideo->height,
                               AV_PIX_FMT_RGB24,
                               SWS_BILINEAR,
                               NULL,
                               NULL,
                               NULL
                               );

    // output frame init and mem alloc
    d->output->frame = av_frame_alloc();
    int numBytes = avpicture_get_size(d->output->codecCtxVideo->pix_fmt,
                                      d->output->codecCtxVideo->width, d->output->codecCtxVideo->height);
    uint8_t* opFrameBuffer = (uint8_t *)av_malloc(numBytes * sizeof(uint8_t));
    avpicture_fill((AVPicture*)d->output->frame, opFrameBuffer, d->output->codecCtxVideo->pix_fmt,
                   d->output->codecCtxVideo->width, d->output->codecCtxVideo->height);
    d->output->frame->format = d->output->codecCtxVideo->pix_fmt;
    d->output->frame->width = d->output->codecCtxVideo->width;
    d->output->frame->height = d->output->codecCtxVideo->height;

    // swscontext init
    d->swsCtx = sws_getContext(d->output->codecCtxVideo->width,
                               d->output->codecCtxVideo->height,
                               AV_PIX_FMT_RGB24,
                               d->output->codecCtxVideo->width,
                               d->output->codecCtxVideo->height,
                               d->input->codecCtxVideo->pix_fmt,
                               SWS_BILINEAR,
                               NULL,
                               NULL,
                               NULL
                               );


    // write output file header
    if (avformat_write_header(d->output->formatCtx, NULL) < 0)
    {
        qWarning() << "ouput file write header failed";
        return;
    }

    d->input->frame = av_frame_alloc();
    d->input->packet = av_packet_alloc();
    if (!d->input->frame || !d->input->packet)
    {
        qWarning() << "frame || packet alloc failed";
        return;
    }

    while (av_read_frame(d->input->formatCtx, d->input->packet) >= 0)
    {
        AVMediaType mediaType = d->input->formatCtx->streams[d->input->packet->stream_index]->codecpar->codec_type;
        if (mediaType != AVMEDIA_TYPE_VIDEO)
        {
            av_packet_unref(d->input->packet);
            continue;
        }

        transcodeVideo(d->input, d->output);
        av_packet_unref(d->input->packet);
    }

    // TODO: force flush data from encoder .. Note:May be the reason of last sec data missing

    av_write_trailer(d->output->formatCtx);

    avformat_close_input(&d->input->formatCtx);
    avformat_free_context(d->input->formatCtx);
    d->input->formatCtx = NULL;

    avformat_free_context(d->output->formatCtx);
    d->output->formatCtx = NULL;

    avcodec_free_context(&d->input->codecCtxVideo); d->input->codecCtxVideo = NULL;
    avcodec_free_context(&d->output->codecCtxVideo); d->output->codecCtxVideo = NULL;

    free(d->input); d->input = NULL;
    free(d->output); d->output = NULL;
    qDebug() << ".......done.......";
}

QString Transcoder::averror2string(const int &errcode)
{
    char errbuf[128];
    av_strerror(errcode, &errbuf[0], 128);
    return QString::fromStdString(&errbuf[0]);
}

void Transcoder::initTranscoderData()
{
    d = new TranscoderData;
    avformat_network_init();
    av_register_all();
    avcodec_register_all();
}

int Transcoder::copyFilenames(const TranscodeParams &params)
{
    if (!d->input || !d->output)
        return -1;
    d->input->fileName = strdup(params.input);
    d->output->fileName = strdup(params.output);
    return 0;
}

int Transcoder::openMediaInput(StreamingContext *inputContext)
{
    inputContext->formatCtx = avformat_alloc_context();
    if (!inputContext->formatCtx) return -1;

    int ret = avformat_open_input(&inputContext->formatCtx, inputContext->fileName, NULL, NULL);
    if (ret != 0) return ret;

    ret = avformat_find_stream_info(inputContext->formatCtx, NULL);
    return ret;
}

int Transcoder::findAVStreams(StreamingContext *inputContext)
{
    for (unsigned int i = 0; i < inputContext->formatCtx->nb_streams; i++)
    {
        AVMediaType mediaType = inputContext->formatCtx->streams[i]->codecpar->codec_type;
        switch (mediaType) {
        case AVMEDIA_TYPE_VIDEO:
        {
            inputContext->streamVideo = inputContext->formatCtx->streams[i];
            inputContext->streamIdxVideo = i;
        }
            break;
        case AVMEDIA_TYPE_AUDIO:
            // Use for audio decoder init
            break;
        default:
            break;
        }
    }
    if (inputContext->streamIdxVideo == -1) return -1;
    return 0;
}

int Transcoder::prepareDecoder(AVStream* stream, AVCodec **codec, AVCodecContext **codecCtx)
{
    *codec = avcodec_find_decoder(stream->codecpar->codec_id);
    if (!*codec) return -1;

    *codecCtx = avcodec_alloc_context3(*codec);
    if (!*codecCtx) return -1;

    int ret = 0;
    if ((ret = avcodec_parameters_to_context(*codecCtx, stream->codecpar)) < 0)
        return ret;

    ret = avcodec_open2(*codecCtx, *codec, NULL);
    return ret;
}

int Transcoder::openMediaOutput(StreamingContext *outputContext)
{
    int ret = avformat_alloc_output_context2(&outputContext->formatCtx, NULL, NULL, outputContext->fileName);
    if (!outputContext->formatCtx) return -1;
    return ret;
}

int Transcoder::prepareEncoder(const AVRational &ipFrameRate, const TranscodeParams &params, StreamingContext *outputContext)
{
    outputContext->streamVideo = avformat_new_stream(outputContext->formatCtx, NULL);

    outputContext->codecVideo = avcodec_find_encoder_by_name(params.codecVideo);
    if (!outputContext->codecVideo) return -1;

    outputContext->codecCtxVideo = avcodec_alloc_context3(outputContext->codecVideo);
    if (!outputContext->codecCtxVideo) return -1;

    av_opt_set(outputContext->codecCtxVideo->priv_data, "preset", "fast", 0);
    if (params.codecPrivKey && params.codecPrivVal)
        av_opt_set(outputContext->codecCtxVideo->priv_data, params.codecPrivKey, params.codecPrivVal, 0);

    outputContext->codecCtxVideo->width = WIDTH;
    outputContext->codecCtxVideo->height = HEIGHT;
//    outputContext->codecCtxVideo->width = d->input->codecCtxVideo->width;
//    outputContext->codecCtxVideo->height = d->input->codecCtxVideo->height;
    outputContext->codecCtxVideo->sample_aspect_ratio = d->input->codecCtxVideo->sample_aspect_ratio;
    if (outputContext->codecVideo->pix_fmts)
        outputContext->codecCtxVideo->pix_fmt = outputContext->codecVideo->pix_fmts[0];
    else
        outputContext->codecCtxVideo->pix_fmt = d->input->codecCtxVideo->pix_fmt;

//    outputContext->codecCtxVideo->bit_rate = 2 * 1000 * 1000;
//    outputContext->codecCtxVideo->rc_max_rate = 2 * 1000 * 1000;
//    outputContext->codecCtxVideo->rc_min_rate = 2.5 * 1000 * 1000;
//    outputContext->codecCtxVideo->rc_buffer_size = 4 * 1000 * 1000;

    outputContext->codecCtxVideo->time_base = av_inv_q(ipFrameRate);
    outputContext->streamVideo->time_base = outputContext->codecCtxVideo->time_base;
//    outputContext->codecCtxVideo->framerate = ipFrameRate;

    int ret = 0;
    if ((ret = avcodec_open2(outputContext->codecCtxVideo, outputContext->codecVideo, NULL)) < 0)
        return ret;

    ret = avcodec_parameters_from_context(outputContext->streamVideo->codecpar, outputContext->codecCtxVideo);
    return ret;
}

int Transcoder::transcodeVideo(StreamingContext *input, StreamingContext *output)
{
    int ret = avcodec_send_packet(input->codecCtxVideo, input->packet);
    if (ret < 0) return ret;

    while (ret >= 0)
    {
        ret = avcodec_receive_frame(input->codecCtxVideo, input->frame);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
        {
            break;
        } else if (ret < 0)
        {
            qWarning() << "Error from decoder";
            return ret;
        }

        overlayPainting(d->input, d->output);

//        sws_scale(d->swsCtx, (uint8_t const * const *)input->frame->data,
//                  input->frame->linesize, 0, input->frame->height,
//                  output->frame->data, output->frame->linesize);
        encodeVideo(input, output);
        av_frame_unref(input->frame);
    }
    return 0;
}

int Transcoder::encodeVideo(StreamingContext *input, StreamingContext *output)
{
    if (input->frame)
        input->frame->pict_type = AV_PICTURE_TYPE_NONE;
    if (output->frame)
        output->frame->pict_type = AV_PICTURE_TYPE_NONE;

    d->output->packet = av_packet_alloc();
    if (!d->output->packet)
    {
        qWarning() << "output packet alloc failed";
        return -1;
    }

    output->frame->pts = input->frame->pts; // TODO: Replace copy of inFrame by proper formula. ref link: http://thompsonng.blogspot.com/2011/09/ffmpeg-avinterleavedwriteframe-return.html
    int ret = avcodec_send_frame(output->codecCtxVideo, output->frame); // deploy frame copy in the middele if resizing or not// use sws scale

    while (ret >= 0)
    {
        ret = avcodec_receive_packet(output->codecCtxVideo, output->packet);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
        {
            break;
        } else if (ret < 0)
        {
            qWarning() << "Error while receiving packet from encoder. " << averror2string(ret);
            return -1;
        }

        output->packet->stream_index = input->streamIdxVideo;
//        output->packet->pts = av_rescale_q_rnd(input->packet->pts, input->streamVideo->time_base, output->streamVideo->time_base, AV_ROUND_PASS_MINMAX);
//        output->packet->dts = av_rescale_q_rnd(input->packet->dts, input->streamVideo->time_base, output->streamVideo->time_base, AV_ROUND_PASS_MINMAX);
//        output->packet->duration = av_rescale_q(input->packet->duration, input->streamVideo->time_base, output->streamVideo->time_base);
        output->packet->pos = -1;

        output->packet->duration = output->streamVideo->time_base.den / output->streamVideo->time_base.num / input->streamVideo->avg_frame_rate.num * input->streamVideo->avg_frame_rate.den;
        av_packet_rescale_ts(output->packet, input->streamVideo->time_base, output->streamVideo->time_base);

        ret = av_interleaved_write_frame(output->formatCtx, output->packet);
        if (ret != 0)
        {
            qWarning() << "write op packet error";
            return -1;
        }
    }
    av_packet_unref(output->packet);
    av_packet_free(&output->packet);
    return ret;
}

int Transcoder::overlayPainting(StreamingContext *input, StreamingContext *output)
{
    static int paintFrameCount = 0;

    sws_scale(d->swsRGB, (uint8_t const * const *)input->frame->data,
              input->frame->linesize, 0, input->frame->height,
              d->frameRGB->data, d->frameRGB->linesize);

    QImage qImage(d->frameRGB->data[0], d->frameRGB->width, d->frameRGB->height,
            d->frameRGB->linesize[0], QImage::Format_RGB888);
    drawTextOnImg(qImage);

    d->frameRGB->data[0] = qImage.bits();

    sws_scale(d->swsCtx, (uint8_t const * const *)d->frameRGB->data,
              d->frameRGB->linesize, 0, d->frameRGB->height,
              output->frame->data, output->frame->linesize);

    return 0;
}

void Transcoder::drawTextOnImg(QImage &image)
{
    QRect frameRect = image.rect();
    QPainter osdDataPainter(&image);
    QPen pen;
    pen.setColor(Qt::white);
    pen.setWidth(5);
    osdDataPainter.setPen(pen);
    osdDataPainter.setFont(QFont("Arial", 25));
    osdDataPainter.drawText(frameRect, Qt::AlignCenter, QString("Hello there..."));
}
