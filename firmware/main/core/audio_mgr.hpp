/*
 * Shared speaker-volume controller.
 */
#pragma once

#include "esp_codec_dev.h"

namespace sys {

class Audio {
public:
    // Worker-only hardware update; never overwrites a newer cached preference.
    static bool applyVolume(int value);

    static void suspendForStandby(); // worker only; does not initialize a codec
    static bool resumeFromStandby(); // playback stays lazy

    // One foreground media stream. All codec access serialized off the GUI.
    // Recording reads interleaved MIC1/MIC2 signed 16-bit stereo PCM; playback
    // writes signed 16-bit mono PCM. Byte counts always describe that format.
    static bool openStream(bool record,int rate);
    static bool readStream(void *data,int bytes);
    static bool writeStream(void *data,int bytes);
    // Worker owner stops I/O first; then release codecs, I2S and DMA.
    static void closeStream();
    static bool releaseIdle(); // nonblocking; never closes an active stream
private:
    static bool init();
};

}// namespace sys
