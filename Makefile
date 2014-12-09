
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
	-o audioplayer main.c audio_engine.c logger.c ui.c utils.c

clean:
	rm -f audioplayer

all:
	audioplayer