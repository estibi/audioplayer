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

typedef enum {
	STATUS_UNKNOWN,
	STATUS_PLAY,
	STATUS_STOP,
	STATUS_PAUSE,
	STATUS_EXIT,
	STATUS_ERROR
} status_t;

struct status_pkt_header {
	uint32_t status;
	uint32_t size;
};

