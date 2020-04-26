#ifndef STREAMINGCONTEXT_H
#define STREAMINGCONTEXT_H

#ifdef __cplusplus
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
}
#endif

struct StreamingContext
{
    StreamingContext() {}
    ~StreamingContext()
    {
        if (fileName)
            free(fileName);
//        if (codecVideo)
//            delete codecVideo;
//        if (codecCtxVideo)
//            delete codecCtxVideo;
        if (formatCtx)
            delete formatCtx;
        if (packet)
            delete packet;
        if (streamVideo)
            delete streamVideo;
        if (frame)
            delete frame;

    }
    int             streamIdxVideo  = -1;
    char            *fileName       = nullptr;
    AVCodec         *codecVideo     = nullptr;
    AVCodecContext  *codecCtxVideo  = nullptr;
    AVFormatContext *formatCtx      = nullptr;
    AVPacket        *packet         = nullptr;
    AVStream        *streamVideo    = nullptr;
    AVFrame         *frame          = nullptr;
};

#endif // STREAMINGCONTEXT_H
