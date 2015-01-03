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

#ifndef AUDIO_SHARED_H
#define AUDIO_SHARED_H

typedef enum {
	EXIT_REASON_UNKNOWN,
	EXIT_REASON_ERROR,
	EXIT_REASON_PLAY_OTHER,
	EXIT_REASON_STOP,
	EXIT_REASON_QUIT,
	EXIT_REASON_EOF
} exit_reason_t;


extern char *current_filename;
extern ao_sample_format format;
extern ao_device *device;

extern int open_audio_device();
extern pthread_mutex_t audio_cmd_mutex;
extern info_t audio_cmd;

#endif
