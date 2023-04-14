#!/bin/sh

echo $1 | open_jtalk \
-m /usr/share/hts-voice/mei/mei_normal.htsvoice \
-x /var/lib/mecab/dic/open-jtalk/naist-jdic \
-ow /tmp/voice.wav \
-g 15

aplay /tmp/voice.wav
