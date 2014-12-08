#define	FILES_IN_DIR_LIMIT 20000
#define	NAME_MAX 255

typedef enum {
	CMD_UNKNOWN,
	CMD_PLAY,
	CMD_STOP,
	CMD_QUIT,
	CMD_PAUSE,
	CMD_FF,
	CMD_REV
} cmd_t;

struct cmd_pkt_header {
	uint32_t cmd;
	uint32_t size;
};

void engine_daemon();
