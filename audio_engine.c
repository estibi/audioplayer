#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <stdbool.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include <ao/ao.h>
#include <math.h>
#include <sndfile.h>

#include "protocol.h"
#include "logger.h"

/*
 * API:
 * http://www.mega-nerd.com/libsndfile/api.html
 * http://www.xiph.org/ao/doc/ao_sample_format.html
 * http://www.mega-nerd.com/SRC/api_full.html
 */

// libsndfile handler
SNDFILE *sndfile;
SF_INFO sfinfo;
int mode = SFM_READ;

// libao settings
static ao_device *device;
ao_sample_format format;
int default_driver;


// audio engine thread
pthread_t ao_thread = NULL;
pthread_attr_t *aot_attr = NULL;
void *ao_arg = NULL;

// socket sender thread
pthread_t sender_thread = NULL;
pthread_attr_t *sender_attr = NULL;
void *sender_arg = NULL;


volatile cmd_t audio_cmd;

pthread_mutex_t audio_cmd_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t ao_event_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t ao_event = PTHREAD_COND_INITIALIZER;

// used by audio thread
int *buffer;

static int sock_fd, conn_fd;

void stop_command();
void quit_command();
int engine_socket_receiver();
int signal_cond_event();
int get_connection_fd();
int init_network();
void *engine_socket_sender();

int
engine_daemon()
{
	int err;

	logger("########################################\n");
	logger("engine_daemon()\n");

	sock_fd = init_network();
	if (!sock_fd) {
		return (-1);
	}
	conn_fd = get_connection_fd();
	if (conn_fd == -1) {
		return (-1);
	}

	ao_initialize();
	default_driver = ao_default_driver_id();

	err = pthread_create(&sender_thread, sender_attr,
		engine_socket_sender, sender_arg);
	if (err != 0) {
		logger("ERROR: sender thread");
	}

	err = engine_socket_receiver();
	ao_shutdown();

	return (err);
}

/*
 * Thread - sends audio status to UI.
 */
void *
engine_socket_sender()
{
	int len, x;

	for (;;) {
		len = write(conn_fd, &x, sizeof (x));
		logger("socket_sender: sent %d bytes\n", len);
		if (len == -1) {
			logger("socket_sender: %s\n", strerror(errno));
		}
		sleep(5);
	}
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
	logger("CLEANING UP: ao_close()\n");
	ao_close(device);

	logger("CLEANING UP: sf_close()\n");
	sf_close(sndfile);

	logger("CLEANING UP: free()\n");
	free(buffer);

	logger("CLEANING UP: mutex()\n");
	pthread_mutex_unlock(&audio_cmd_mutex);

	logger("CLEANING UP - DONE\n");
}

void
exit_sndfile()
{
	logger("CLEANING UP: sf_close()\n");
	sf_close(sndfile);
}

void
set_audio_format()
{
	memset(&format, 0, sizeof (format));
	format.channels = sfinfo.channels;
	format.rate = sfinfo.samplerate;
	format.byte_format = AO_FMT_NATIVE;
	format.bits = 32;	// 32 for sf_read_int(), 16 for sf_read_short()
}

int
open_file_sf(char *str_buf)
{
	sndfile = sf_open(str_buf, mode, &sfinfo);
	if (sndfile == NULL) {
		logger("sf_open error: %s\n", strerror(errno));
		return (-1);
	}
	return (0);
}

int
open_audio_device()
{
	// libao
	device = ao_open_live(default_driver, &format, NULL);
	if (device == NULL) {
		logger("ao_open_live() error: %s\n", strerror(errno));
		return (-1);
	}
	return (0);
}

/*
 * This is an audio I/O thread.
 */
void *
ao_play_file()
{
	int buf_len, buf_size;
	static bool paused = false, shifted = false;
	bool last_read = false;
	sfinfo.format = 0;
	int read_cnt = 0;
	int i, play_chunk;
	char *bufp = NULL;
	sf_count_t count, seek_ret, seek_frames;

	pthread_cleanup_push(cancel_routine, NULL);

	// buf_len = format.bits/8 * format.channels * format.rate / 4;
	buf_len = format.bits/8 * format.channels * format.rate;
	seek_frames = buf_len * 4;
	buf_size = buf_len * sizeof (int);
	play_chunk = buf_size / 16;
	logger("play_chunk: %d\n", play_chunk);
	buffer = malloc(buf_size);
	if (buffer == NULL) {
		logger("buffer error: %s\n", strerror(errno));
		pthread_exit(NULL);
	}

	// reading a file
	for (;;) {
		count = sf_read_int(sndfile, buffer, buf_len);
		logger("read from file: %d\n", (int)count);
		logger("read_cnt: %d\n", read_cnt);
		if ((int)count == 0) {
			// end of file
			pthread_mutex_lock(&audio_cmd_mutex);
			audio_cmd = CMD_STOP;
			pthread_exit(NULL);
		}
		if (count < buf_len) {
			if (count <= play_chunk) {
				ao_play(device, (char *)buffer, count);
				pthread_mutex_lock(&audio_cmd_mutex);
				audio_cmd = CMD_STOP;
				pthread_exit(NULL);
			}
			last_read = true;
		}

		// play in small chunks
		bufp = (char *)buffer;
		for (i = 0; i < 16; i++) {
			// checking for the command
			pthread_mutex_lock(&audio_cmd_mutex);
			switch (audio_cmd) {
			case CMD_STOP:
				logger("ao_play_file - CMD_STOP\n");
				pthread_exit(NULL);
			case CMD_PAUSE:
				logger("ao_play_file - CMD_PAUSE\n");
				paused = true;
				break;
			case CMD_FF:
				logger("ao_play_file - CMD_FF\n");
				seek_ret = sf_seek(sndfile, seek_frames, SEEK_CUR);
				logger("seek_ret: %lld\n", seek_ret);
				if (seek_ret == -1)
					sf_seek(sndfile, 0, SEEK_END);
				shifted = true;
				audio_cmd = CMD_PLAY;
				break;
			case CMD_REV:
				logger("ao_play_file - CMD_REV\n");
				seek_ret = sf_seek(sndfile, 0, SEEK_CUR);
				if (seek_ret <= seek_frames)
					seek_ret = sf_seek(sndfile, 0, SEEK_SET);
				else
					seek_ret = sf_seek(sndfile, -seek_frames, SEEK_CUR);
				logger("seek_ret: %lld\n", seek_ret);
				audio_cmd = CMD_PLAY;
				shifted = true;
				break;
			default:
				;;
			}
			pthread_mutex_unlock(&audio_cmd_mutex);

			// CMD_FF, CMD_REV
			if (shifted) {
				shifted = false;
				break;
			}

			if (paused) {
				pthread_mutex_lock(&ao_event_mutex);
				// waiting for an event
				if (pthread_cond_wait(&ao_event, &ao_event_mutex) != 0) {
					logger("ERROR: pthread_cond_wait failed\n");
					// TODO ?
				}
				paused = false;
				pthread_mutex_unlock(&ao_event_mutex);
			}

			// check if STOP command was sent
			pthread_mutex_lock(&audio_cmd_mutex);
			if (audio_cmd == CMD_STOP) {
				logger("CMD_STOP\n");
				pthread_exit(NULL);
			}
			pthread_mutex_unlock(&audio_cmd_mutex);

			// play sound
			ao_play(device, bufp, play_chunk);
			bufp += play_chunk;
			if (last_read) {
				// TODO
				logger("count: %d\n", count);
				logger("play_chunk: %d\n", play_chunk);
				logger("bufp - (char *)buffer: %d\n", bufp - (char *)buffer);
				if (bufp - (char *)buffer >= count) {
					break;
				}
			}
		}
		read_cnt++;
	}

	pthread_cleanup_pop(1);
	pthread_exit(NULL);
}

void
play_file(char *str_buf)
{
	// quit current thread if alive
	if (pthread_kill(ao_thread, 0) == 0) {
		stop_command();
	}

	// wait for current thread to exit
	pthread_join(ao_thread, NULL);

	// reset 'stop' command
	pthread_mutex_lock(&audio_cmd_mutex);
	audio_cmd = CMD_PLAY;
	pthread_mutex_unlock(&audio_cmd_mutex);

	if (open_file_sf(str_buf) == -1) {
		logger("ERROR: can't open audio file %s\n", str_buf);
		return;
	}

	set_audio_format();

	if (open_audio_device() == -1) {
		logger("ERROR: can't open audio device\n");
		sf_close(sndfile);
		return;
	}

	if (pthread_create(&ao_thread, aot_attr, ao_play_file, ao_arg) != 0) {
		logger("pthread_create error\n");
	}
}

void
stop_command()
{
	pthread_mutex_lock(&audio_cmd_mutex);
	audio_cmd = CMD_STOP;
	pthread_mutex_unlock(&audio_cmd_mutex);

	if (signal_cond_event() != 0) {
		logger("ERROR: QUIT COMMAND FAILED\n");
	}
}

void
ff_command()
{
	pthread_mutex_lock(&audio_cmd_mutex);
	audio_cmd = CMD_FF;
	pthread_mutex_unlock(&audio_cmd_mutex);

	if (signal_cond_event() != 0) {
		logger("ERROR: FF COMMAND FAILED\n");
	}
}

void
rev_command()
{
	pthread_mutex_lock(&audio_cmd_mutex);
	audio_cmd = CMD_REV;
	pthread_mutex_unlock(&audio_cmd_mutex);

	if (signal_cond_event() != 0) {
		logger("ERROR: REV COMMAND FAILED\n");
	}
}

int
signal_cond_event()
{
	/*
	 *
	 */
	pthread_mutex_lock(&ao_event_mutex);
	int ret = pthread_cond_signal(&ao_event);
	if (ret != 0) {
		logger("ERROR: pthread_cond_signal: %d\n", ret);
	}
	pthread_mutex_unlock(&ao_event_mutex);
	return (0);
}


void
pause_command()
{
	pthread_mutex_lock(&audio_cmd_mutex);
	if (audio_cmd == CMD_PLAY) {
		audio_cmd = CMD_PAUSE;
	} else if (audio_cmd == CMD_PAUSE) {
		audio_cmd = CMD_PLAY;
		pthread_mutex_lock(&ao_event_mutex);
		int ret = pthread_cond_signal(&ao_event);
		if (ret != 0) {
			logger("ERROR: pthread_cond_signal: %d\n", ret);
		}
		pthread_mutex_unlock(&ao_event_mutex);
	}
	pthread_mutex_unlock(&audio_cmd_mutex);
}

int
init_network()
{
	int sock_fd, err;
	//int err;
	struct sockaddr_in addr;
	int in_queue = 5;

	memset(&addr, 0, sizeof (addr));
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	addr.sin_port = htons(10000);

	logger("socket_daemon - socket()\n");
	sock_fd = socket(PF_INET, SOCK_STREAM, IPPROTO_IP);
	if (!sock_fd) {
		logger("ERROR: socket error: %s\n", strerror(errno));
		return (0);
	}

	logger("socket_daemon - bind()\n");
	err = bind(sock_fd, (struct sockaddr *)&addr, sizeof (addr));
	if (err) {
		logger("ERROR: bind error: %s\n", strerror(errno));
		close(sock_fd);
		return (0);
	}

	logger("socket_daemon - listen()\n");
	err = listen(sock_fd, in_queue);
	if (err) {
		logger("ERROR: listen error: %s\n", strerror(errno));
		close(sock_fd);
		return (0);
	}

	return (sock_fd);
}

int
get_connection_fd()
{
	int conn_fd;
	logger("socket_daemon - waiting for new connection..\n");
	conn_fd = accept(sock_fd, NULL, NULL);
	if (conn_fd == -1) {
		logger("ERROR: accept error: %s\n", strerror(errno));
		close(sock_fd);
		return (-1);
	}
	logger("new connection, conn_fd: %d\n", conn_fd);
	return (conn_fd);
}

/*
 * Receives commands from ui using socket connection.
 */
int
engine_socket_receiver()
{
	int len;
	char *str_buf;
	bool has_content = false;

	struct cmd_pkt_header pkt_hdr, host_pkt_hdr;
	host_pkt_hdr.cmd = CMD_UNKNOWN;

	for (;;) {
		logger("reading from socket..\n");
		len = read(conn_fd, &pkt_hdr, sizeof (pkt_hdr));

		logger("read %d bytes from socket\n", len);
		if (len == 0) {
			logger("ERROR: - connection closed (initial read)\n");
			break;
		}
		if (len < sizeof (pkt_hdr)) {
			logger("ERROR: - received less bytes from socket\n");
			break;
		}

		host_pkt_hdr.cmd = ntohl(pkt_hdr.cmd);
		host_pkt_hdr.size = ntohl(pkt_hdr.size);
		logger("receiver host_pkg_hdr.cmd: %d\n", (unsigned int)host_pkt_hdr.cmd);
		logger("receiver host_pkg_hdr.size: %d\n", (unsigned int)host_pkt_hdr.size);

		if (host_pkt_hdr.size > 0) {
			str_buf = malloc(host_pkt_hdr.size);
			if (!str_buf) {
				logger("ERROR: malloc error: %s\n", strerror(errno));
				break;
			}
			has_content = true;

			len = read(conn_fd, str_buf, host_pkt_hdr.size);
			if (len == 0) {
				logger("ERROR: socket_daemon() - connection closed\n");
				break;
			} else if (len < sizeof (str_buf)) {
				logger("ERROR: read less then buffer\n");
				break;
			}
		} else {
			has_content = false;
		}

		logger("received command: %d\n", host_pkt_hdr.cmd);
		if (has_content) {
			logger("received string: %s\n", str_buf);
		}

		switch (host_pkt_hdr.cmd) {
		case CMD_PLAY:
			logger("socket_daemon received CMD_PLAY\n");
			play_file(str_buf);
			break;
		case CMD_PAUSE:
			logger("socket_daemon received CMD_PAUSE\n");
			pause_command();
			break;
		case CMD_STOP:
			logger("socket_daemon received CMD_STOP\n");
			stop_command();
			break;
		case CMD_QUIT:
			logger("socket_daemon received CMD_QUIT\n");
			stop_command();
			close(conn_fd);
			close(sock_fd);
			if (pthread_kill(ao_thread, 0) == 0) {
				logger("waiting for audio thread..\n");
				pthread_join(ao_thread, NULL);
			}
			return (0);
		case CMD_FF:
			logger("socket_daemon received CMD_FF\n");
			ff_command();
			break;
		case CMD_REV:
			logger("socket_daemon received CMD_REV\n");
			rev_command();
			break;
		default:
			;;
		}
		if (has_content) {
			free(str_buf);
			has_content = false;
		}
	}
	// we are here if error occurred
	stop_command();
	if (has_content) {
		free(str_buf);
		has_content = false;
	}
	close(conn_fd);
	close(sock_fd);
	if (pthread_kill(ao_thread, 0) == 0) {
		logger("waiting for audio thread..\n");
		pthread_join(ao_thread, NULL);
	}
	return (-1);
}
