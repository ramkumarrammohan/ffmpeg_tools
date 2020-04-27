#ifndef TRANSCODER_H
#define TRANSCODER_H

#include <QString>
#include <QImage>

#include "streamingcontext.h"

struct TranscodeParams
{
    char *input;
    char *output;
    char *codecVideo;
    char *codecPrivKey;
    char *codecPrivVal;
};


struct TranscoderData;
class Transcoder
{
public:
    Transcoder();
    ~Transcoder();

    void transcode(const TranscodeParams &params);

private:
    TranscoderData *d;

    QString averror2string(const int &errcode);
    void initTranscoderData();

    // input realted functions
    int copyFilenames(const TranscodeParams &params);
    int openMediaInput(StreamingContext *inputContext);
    int findAVStreams(StreamingContext *inputContext);
    int prepareDecoder(AVStream* stream, AVCodec **codec, AVCodecContext **codecCtx);

    // output realted functions
    int openMediaOutput(StreamingContext *outputContext);
    int prepareEncoder(const AVRational &ipFrameRate, const TranscodeParams &params, StreamingContext *outputContext);
    int transcodeVideo(StreamingContext *input, StreamingContext *output);
    int encodeVideo(StreamingContext *input, StreamingContext *output);
    int overlayPainting(StreamingContext *input, StreamingContext *output);
    void drawTextOnImg(QImage &image);
};

#endif // TRANSCODER_H
