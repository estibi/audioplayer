#include <netinet/in.h>
#include <stdbool.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include <ao/ao.h>
#include <math.h>
#include <sndfile.h>

#include <pthread.h>

#include "audio_engine.h"

#define	AUDIO_FILE "test.wav"

/*
 * API:
 * http://www.mega-nerd.com/libsndfile/api.html
 * http://www.xiph.org/ao/doc/ao_sample_format.html
 * http://www.mega-nerd.com/SRC/api_full.html
 */

// libsndfile handler
SNDFILE * sndfile;

// libao settings
ao_device *device;
ao_sample_format format;
int default_driver;

// audio engine thread
pthread_t ao_thread = NULL;
pthread_attr_t *aot_attr = NULL;
void *ao_arg = NULL;

volatile cmd_t audio_cmd;

pthread_mutex_t audio_cmd_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t ao_event_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t ao_event = PTHREAD_COND_INITIALIZER;

int *buffer;

void quit_command();
void socket_daemon();
int signal_cond_event();

void
engine_daemon()
{
	ao_initialize();
	default_driver = ao_default_driver_id();
	socket_daemon();

	ao_shutdown();
}

void
show_sf_info(SF_INFO * sfinfo)
{
	printf("FRAMES: %lld\n", (long long)sfinfo->frames);
	printf("SAMPLE RATE: %d\n", sfinfo->samplerate);
	printf("CHANNELS: %d\n", sfinfo->channels);
	printf("FORMAT: %d\n", sfinfo->format);
	printf("SECTIONS: %d\n", sfinfo->sections);
	printf("SEEKABLE: %d\n", sfinfo->seekable);
}

void
cancel_routine(void *arg)
{
	printf("CLEANING UP: ao_close()\n");
	ao_close(device);

	printf("CLEANING UP: sf_close()\n");
	sf_close(sndfile);

	printf("CLEANING UP: free()\n");
	free(buffer);

	printf("CLEANING UP: mutex()\n");
	pthread_mutex_unlock(&audio_cmd_mutex);

	printf("CLEANING UP - DONE\n");
}

void
exit_sndfile()
{
	printf("CLEANING UP: sf_close()\n");
	sf_close(sndfile);
}

void *
ao_play_file()
{
	int buf_len, buf_size;
	bool paused = false, shifted = false;
	SF_INFO sfinfo;
	int mode = SFM_READ;
	sfinfo.format = 0;
	int read_cnt = 0;
	int i, play_chunk;
	char *bufp = NULL;
	sf_count_t count, seek_ret, seek_frames;

	pthread_cleanup_push(cancel_routine, NULL);

	sndfile = sf_open(AUDIO_FILE, mode, &sfinfo);
	if (sndfile == NULL) {
		printf("sf_open error: %s\n", strerror(errno));
		pthread_exit(NULL);
	}

	memset(&format, 0, sizeof (format));
	format.channels = sfinfo.channels;
	format.rate = sfinfo.samplerate;
	format.byte_format = AO_FMT_NATIVE;
	format.bits = 32;	// 32 for sf_read_int(), 16 for sf_read_short()

	// buf_len = format.bits/8 * format.channels * format.rate / 4;
	buf_len = format.bits/8 * format.channels * format.rate;
	seek_frames = buf_len * 4;
	buf_size = buf_len * sizeof (int);
	play_chunk = buf_size / 16;
	printf("play_chunk: %d\n", play_chunk);
	buffer = malloc(buf_size);
	if (buffer == NULL) {
		printf("buffer error: %s\n", strerror(errno));
		pthread_exit(NULL);
	}

	// libao
	device = ao_open_live(default_driver, &format, NULL);
	if (device == NULL) {
		printf("error: %s\n", strerror(errno));
		pthread_exit(NULL);
	}

	// reading a file
	for (;;) {
		count = sf_read_int(sndfile, buffer, buf_len);
		printf("counter: %d\n", (int)count);
		printf("read_cnt: %d\n", read_cnt);
		if ((int)count == 0) {
			// end of file
			pthread_mutex_lock(&audio_cmd_mutex);
			audio_cmd = CMD_STOP;
			pthread_exit(NULL);
		}

		// play in small chunks
		bufp = (char *)buffer;
		for (i = 0; i < 16; i++) {
			// checking for the command
			pthread_mutex_lock(&audio_cmd_mutex);
			switch (audio_cmd) {
			case CMD_STOP:
				printf("CMD_STOP\n");
				pthread_exit(NULL);
			case CMD_PAUSE:
				printf("CMD_PAUSE\n");
				paused = true;
				break;
			case CMD_FF:
				printf("CMD_FF\n");
				seek_ret = sf_seek(sndfile, seek_frames, SEEK_CUR);
				printf("seek_ret: %lld\n", seek_ret);
				if (seek_ret == -1)
					sf_seek(sndfile, 0, SEEK_END);
				shifted = true;
				audio_cmd = CMD_PLAY;
				break;
			case CMD_REV:
				printf("CMD_REV\n");
				seek_ret = sf_seek(sndfile, 0, SEEK_CUR);
				if (seek_ret <= seek_frames)
					seek_ret = sf_seek(sndfile, 0, SEEK_SET);
				else
					seek_ret = sf_seek(sndfile, -seek_frames, SEEK_CUR);
				printf("seek_ret: %lld\n", seek_ret);
				audio_cmd = CMD_PLAY;
				shifted = true;
				break;
			default:
				;;
			}
			pthread_mutex_unlock(&audio_cmd_mutex);

			if (shifted) {
				shifted = false;
				break;
			}

			if (paused) {
				pthread_mutex_lock(&ao_event_mutex);
				// waiting for an event
				if (pthread_cond_wait(&ao_event, &ao_event_mutex) != 0) {
					printf("pthread_cond_wait failed\n");
					// TODO
					// getch();
				}
				pthread_mutex_unlock(&ao_event_mutex);
				paused = false;
			}
			// check if STOP command was sent
			pthread_mutex_lock(&audio_cmd_mutex);
			if (audio_cmd == CMD_STOP) {
				printf("CMD_STOP\n");
				pthread_exit(NULL);
			}
			pthread_mutex_unlock(&audio_cmd_mutex);

			ao_play(device, bufp, play_chunk);
			bufp += play_chunk;
		}
		read_cnt++;
	}

	pthread_cleanup_pop(1);
	pthread_exit(NULL);
}

void
play_file()
{
	// quit current thread if alive
	if (pthread_kill(ao_thread, 0) == 0) {
		quit_command();
	}

	// wait for current thread to exit
	pthread_join(ao_thread, NULL);

	// reset 'exit' command
	pthread_mutex_lock(&audio_cmd_mutex);
	audio_cmd = CMD_PLAY;
	pthread_mutex_unlock(&audio_cmd_mutex);

	if (pthread_create(&ao_thread, aot_attr, ao_play_file, ao_arg) != 0) {
		printf("pthread_create error\n");
	}
}

void
quit_command()
{
	printf("QUIT COMMAND: start\n");

	pthread_mutex_lock(&audio_cmd_mutex);
	audio_cmd = CMD_STOP;
	pthread_mutex_unlock(&audio_cmd_mutex);

	if (signal_cond_event() != 0) {
		printf("QUIT COMMAND FAILED\n");
	}

	printf("QUIT COMMAND: done\n");
}

void
ff_command()
{
	printf("FF COMMAND: start\n");

	pthread_mutex_lock(&audio_cmd_mutex);
	audio_cmd = CMD_FF;
	pthread_mutex_unlock(&audio_cmd_mutex);

	if (signal_cond_event() != 0) {
		printf("FF COMMAND FAILED\n");
	}

	printf("FF COMMAND: done\n");
}

void
rev_command()
{
	printf("REV COMMAND: start\n");

	pthread_mutex_lock(&audio_cmd_mutex);
	audio_cmd = CMD_REV;
	pthread_mutex_unlock(&audio_cmd_mutex);

	if (signal_cond_event() != 0) {
		printf("REV COMMAND FAILED\n");
	}

	printf("REV COMMAND: done\n");
}

int
signal_cond_event()
{
	pthread_mutex_lock(&ao_event_mutex);
	printf("@@@ pthread_cond_signal, starting..\n");
	int ret = pthread_cond_signal(&ao_event);
	printf("@@@ pthread_cond_signal: %d\n", ret);
	pthread_mutex_unlock(&ao_event_mutex);
	return (0);
}


void
pause_command()
{
	printf("PAUSE COMMAND: start\n");

	pthread_mutex_lock(&audio_cmd_mutex);
	if (audio_cmd == CMD_PLAY) {
		audio_cmd = CMD_PAUSE;
	} else if (audio_cmd == CMD_PAUSE) {
		audio_cmd = CMD_PLAY;
		pthread_mutex_lock(&ao_event_mutex);
		printf("@@@ pthread_cond_signal, starting..\n");
		int ret = pthread_cond_signal(&ao_event);
		printf("@@@ pthread_cond_signal: %d\n", ret);
		pthread_mutex_unlock(&ao_event_mutex);
	}
	pthread_mutex_unlock(&audio_cmd_mutex);

	printf("PAUSE COMMAND: done\n");
}

void
socket_daemon()
{
	int sock_fd, conn_fd, err, len;
	struct sockaddr_in addr;
	int in_queue = 5;
	cmd_t cmd = CMD_UNKNOWN;

	memset(&addr, 0, sizeof (addr));
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	addr.sin_port = htons(10000);

	printf("socket()\n");
	sock_fd = socket(PF_INET, SOCK_STREAM, IPPROTO_IP);
	// printf("sock_fd: %d\n", sock_fd);
	if (!sock_fd) {
		printf("socket error: %s\n", strerror(errno));
	}

	printf("bind()\n");
	err = bind(sock_fd, (struct sockaddr *)&addr, sizeof (addr));
	// printf("err: %d\n", err);
	if (err) {
		printf("bind error: %s\n", strerror(errno));
	}

	printf("listen()\n");
	err = listen(sock_fd, in_queue);
	if (err) {
		printf("listen error: %s\n", strerror(errno));
	}

	printf("initial cmd: %d\n", cmd);

	printf("waiting for new connection..\n");
	conn_fd = accept(sock_fd, NULL, NULL);
	if (conn_fd == -1) {
		printf("accept error: %s\n", strerror(errno));
		close(conn_fd);
		return;
	}
	printf("new connection\n");
	printf("conn_fd: %d\n", conn_fd);

	for (;;) {
		printf("reading..\n");
		len = read(conn_fd, &cmd, sizeof (cmd));

		if (len == 0) {
			printf("connection closed by remote peer\n");
			break;
		}

		// printf("len: %d\n", len);
		// printf("sizeof(cmd): %d\n", (unsigned int)sizeof(cmd));

		if (len != sizeof (cmd)) {
			printf("ERROR\n");
		} else {
			printf("received: %d\n", cmd);
		}

		printf("cmd: %d\n", cmd);
		switch (cmd) {
		case CMD_PLAY:
			printf("daemon received CMD_PLAY\n");
			play_file();
			break;
		case CMD_PAUSE:
			printf("daemon received CMD_PAUSE\n");
			pause_command();
			break;
		case CMD_STOP:
			printf("daemon received CMD_STOP\n");
			// TODO ?
			quit_command();
			break;
		case CMD_FF:
			printf("daemon received CMD_FF\n");
			ff_command();
			break;
		case CMD_REV:
			printf("daemon received CMD_REV\n");
			rev_command();
			break;
		default:
			;;
		}
	}
	printf("close connection\n");
	close(conn_fd);

	// printf("waiting for a thread..\n");
	// pthread_join(ao_thread, NULL);
}
