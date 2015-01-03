
CFLAGS = \
	-Wall \
	-m32 \
	-I/usr/pkg/include \
	-L/usr/pkg/lib

LDFLAGS = \
	-lao \
	-lsndfile \
	-lpthread \
	-lncurses \
	-lmad

audioplayer:
	gcc $(CFLAGS) $(LDFLAGS) \
	-o audioplayer *.c

clean:
	rm -f audioplayer

all:
	audioplayer