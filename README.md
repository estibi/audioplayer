audioplayer
===========

a work in progress


------------------------------------------------------------
OSX notes

sudo pkgin -y install libao libao-macosx libsndfile


gcc -Wall -m32 -I/usr/pkg/include -L/usr/pkg/lib \
    -lao -lsndfile -lpthread -lncurses\
    -o audioplayer audioplayer.c


------------------------------------------------------------
illumos notes:

pfexec pkgin -y install libao libao-oss libao-sun libsndfile

gcc -Wall -m64 -I/opt/local/include \
    -I/usr/include/ncurses -L/opt/local/lib \
    -R/opt/local/lib \
    -lao -lsndfile -lpthread -lncurses \
    -o audioplayer audioplayer.c

