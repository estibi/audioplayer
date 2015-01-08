#ifndef AUDIO_SHARED_H
#define AUDIO_SHARED_H

#include <ao/ao.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdbool.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include "protocol.h"

typedef enum {
	EXIT_REASON_UNKNOWN,
	EXIT_REASON_ERROR,
	EXIT_REASON_PLAY_OTHER,
	EXIT_REASON_STOP,
	EXIT_REASON_QUIT,
	EXIT_REASON_EOF
} exit_reason_t;

typedef enum {
	CODEC_STATUS_UNKNOWN,
	CODEC_STATUS_ERROR,
	CODEC_STATUS_PLAY,
	CODEC_STATUS_EXIT_PLAY_OTHER,
	CODEC_STATUS_STOP,
	CODEC_STATUS_PAUSE,
	CODEC_STATUS_QUIT,
	CODEC_STATUS_EOF
} codec_status_t;


extern char *current_filename;
extern ao_sample_format format;
extern ao_device *device;

extern int open_audio_device();
extern pthread_mutex_t audio_cmd_mutex;
extern info_t audio_cmd;

extern pthread_mutex_t codec_status_mutex;
extern codec_status_t codec_status;

#endif
