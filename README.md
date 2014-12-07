audioplayer
===========

a work in progress


------------------------------------------------------------
OSX notes

$ sudo pkgin -y install libao libao-macosx libsndfile

$ make

------------------------------------------------------------
illumos notes:

pfexec pkgin -y install libao libao-oss libao-sun libsndfile

gcc -Wall -m64 -I/opt/local/include \
    -I/usr/include/ncurses -L/opt/local/lib \
    -R/opt/local/lib \
    -lao -lsndfile -lpthread -lncurses \
    -lsocket  -lnsl \
    -o audioplayer audioplayer.c audio_engine.c

