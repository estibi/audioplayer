audioplayer
===========

a work in progress


------------------------------------------------------------
OSX notes

$ sudo pkgin -y install libao libao-macosx libsndfile libmad ncurses

$ make

./audioplayer

------------------------------------------------------------
illumos notes:

pfexec pkgin -y install libao libao-oss libao-sun libsndfile ncurses

gcc -Wall -m64 -I/opt/local/include \
    -I/usr/include/ncurses -L/opt/local/lib \
    -R/opt/local/lib \
    -lao -lsndfile -lpthread -lncurses \
    -lsocket  -lnsl -lmad \
    -o audioplayer *.c

