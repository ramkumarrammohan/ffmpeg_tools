#include <stdio.h>
#include "transcoder.h"
#include <QString>

int main(int argc, char *argv[])
{
    TranscodeParams t;
    t.input = (char*)"/home/ramkumar/Desktop/sample3.mp4";
    t.output = (char*)"/home/ramkumar/Desktop/output.mp4";
    t.codecVideo = (char*)"libx265";
    t.codecPrivKey = (char*)"x265-params";
    t.codecPrivVal = (char*)"keyint=60:min-keyint=60:scenecut=0";

    Transcoder transcoder;
    transcoder.transcode(t);
}
