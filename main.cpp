#include <QGuiApplication>
#include <stdio.h>
#include "transcoder.h"
#include <QString>

int main(int argc, char *argv[])
{
    QCoreApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
    QGuiApplication app(argc, argv);

    TranscodeParams t;
    t.input = (char*)"/home/ramkumar/Desktop/sample3.mp4";
//    t.input = (char*)"udp://127.0.0.1:23000";
    t.output = (char*)"/home/ramkumar/Desktop/output.mp4";
    t.codecVideo = (char*)"libx264";
    t.codecPrivKey = (char*)"x264-params";
    t.codecPrivVal = (char*)"keyint=60:min-keyint=60:scenecut=0";

    Transcoder transcoder;
    transcoder.transcode(t);

    app.exec();
}
