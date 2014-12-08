
CFLAGS = \
	-Wall \
	-m32 \
	-I/usr/pkg/include \
	-L/usr/pkg/lib

LDFLAGS = \
	-lao \
	-lsndfile \
	-lpthread \
	-lncurses

audioplayer:
	gcc $(CFLAGS) $(LDFLAGS) \
	-o audioplayer audioplayer.c audio_engine.c logger.c

clean:
	rm -f audioplayer

all:
	audioplayer