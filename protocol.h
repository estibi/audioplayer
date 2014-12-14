#include <errno.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define	DAEMON_PORT 10000

typedef enum {
	CMD_UNKNOWN,
	CMD_PLAY,
	CMD_STOP,
	CMD_QUIT,
	CMD_PAUSE,
	CMD_FF,
	CMD_REV,
	STATUS_UNKNOWN,
	STATUS_PLAY,
	STATUS_STOP,
	STATUS_PAUSE,
	STATUS_EXIT,
	STATUS_ERROR
} info_t;

struct pkt_header {
	uint32_t info;
	uint32_t size;
};

int send_packet(int fd, info_t info, char *s);
