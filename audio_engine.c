#include <math.h>
#include <sndfile.h>

#include "audio_shared.h"
#include "audio_codec_mad.h"
#include "logger.h"
#include "protocol.h"
#include "utils.h"

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
ao_device *device;
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

// cached of current audio file name
char *current_filename = NULL;


/*
 *
 */
pthread_mutex_t audio_cmd_mutex = PTHREAD_MUTEX_INITIALIZER;
char *audio_cmd_str;
info_t audio_cmd;

// for event notifications (pause/unpause/play/exit..)
pthread_mutex_t ao_event_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t ao_event = PTHREAD_COND_INITIALIZER;


/*
 * Used by engine_socket_sender to notify UI.
 */
pthread_mutex_t audio_status_for_ui_mutex = PTHREAD_MUTEX_INITIALIZER;
volatile info_t audio_status_for_ui;

pthread_mutex_t status_event_for_ui_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t status_event_for_ui = PTHREAD_COND_INITIALIZER;


/*
 * TODO
 */
pthread_mutex_t codec_status_mutex = PTHREAD_MUTEX_INITIALIZER;
codec_status_t codec_status;


// used by audio thread
int *buffer;

static int sock_fd, conn_fd;


bool use_codec;

void stop_command();
void quit_command();
int engine_socket_receiver();
int signal_cond_event();
int get_connection_fd();
int init_network();
void notify_packet_sender(info_t status);

void cleanup_native_codec();

void *engine_socket_sender();
void *engine_ao();


/*
 * Entry point of audio daemon.
 */
int
engine_daemon()
{
	int err;
	pid_t ppid;

	logger("########################################\n");
	logger("engine_daemon - START\n");

	// buffer for audio filename string received from UI
	audio_cmd_str = malloc(NAME_MAX + 1);
	if (audio_cmd_str == NULL) {
		logger("ERROR: can't alloc memory for audio_cmd_str\n");
		return (-1);
	}

	current_filename = malloc(NAME_MAX + 1);
	if (!current_filename) {
		logger("ERROR: Can't initialize current_filename.");
		free(audio_cmd_str);
		return (-1);
	}

	logger("starting network..\n");
	sock_fd = init_network();
	if (!sock_fd) {
		logger("ERROR: init_network()\n");
		free(audio_cmd_str);
		free(current_filename);
		return (-1);
	}

	// network is initialized, notify parent
	ppid = getppid();
	logger("ppid: %d\n", ppid);

	err = kill(ppid, SIGUSR1);
	if (err == -1) {
		logger("ERROR: can't send SIGUSR1 to the parent\n");
		free(audio_cmd_str);
		free(current_filename);
		ao_shutdown();
		return (err);
	}

	conn_fd = get_connection_fd();
	if (conn_fd == -1) {
		logger("ERROR: get_connection_fd()\n");
		free(audio_cmd_str);
		free(current_filename);
		return (-1);
	}

	logger("starting audio subsystem..\n");
	ao_initialize();
	default_driver = ao_default_driver_id();

	logger("starting sender thread..\n");
	err = pthread_create(&sender_thread, sender_attr,
		engine_socket_sender, sender_arg);
	if (err != 0) {
		logger("ERROR: sender thread\n");
		free(audio_cmd_str);
		free(current_filename);
		ao_shutdown();
	}

	logger("starting ao thread..\n");
	err = pthread_create(&ao_thread, aot_attr, engine_ao, ao_arg);
	if (err != 0) {
		logger("ERROR: engine_ao failed\n");
		free(audio_cmd_str);
		free(current_filename);
		ao_shutdown();
	}

	logger("waiting for commands..\n");
	// main loop - receiving commands from UI
	err = engine_socket_receiver();


	// wait for ao_thread if alive
	if (pthread_kill(ao_thread, 0) == 0) {
		logger("engine_daemon - waiting for ao_thread..\n");
		pthread_join(ao_thread, NULL);
	}

	// finishing sender_thread
	notify_packet_sender(CMD_QUIT);

	if (pthread_kill(sender_thread, 0) == 0) {
		logger("engine_daemon - waiting for sender_thread..\n");
		pthread_join(sender_thread, NULL);
	}

	ao_shutdown();
	free(audio_cmd_str);
	free(current_filename);

	logger("engine_daemon - STOP\n");
	return (err);
}

/*
 * Thread - sends audio status to UI.
 */
void *
engine_socket_sender()
{
	int err;
	info_t status_copy;

	for (;;) {
		pthread_mutex_lock(&status_event_for_ui_mutex);
		// waiting for an event
		if (pthread_cond_wait(&status_event_for_ui,
				&status_event_for_ui_mutex) != 0) {
			logger("ERROR: pthread_cond_wait in engine_socket_sender()\n");
		}
		pthread_mutex_unlock(&status_event_for_ui_mutex);

		// reading status variable
		pthread_mutex_lock(&audio_status_for_ui_mutex);
		status_copy = audio_status_for_ui;
		pthread_mutex_unlock(&audio_status_for_ui_mutex);

		if (status_copy == CMD_QUIT)
			pthread_exit(NULL);

		// send audio status to UI
		err = send_packet(conn_fd, status_copy, NULL);
		if (err == -1) {
			logger("ERROR: send_packet failed\n");
		}
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
cleanup_native_codec()
{
	logger("CLEANING UP: ao_close()\n");
	ao_close(device);

	logger("CLEANING UP: sf_close()\n");
	sf_close(sndfile);

	logger("CLEANING UP: free()\n");
	free(buffer);

	logger("CLEANING UP - DONE\n");
}

void
exit_sndfile()
{
	logger("CLEANING UP: sf_close()\n");
	sf_close(sndfile);
}

static void
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

void
notify_packet_sender(info_t status)
{
	int ret;

	// set for packet sender
	pthread_mutex_lock(&audio_status_for_ui_mutex);
	audio_status_for_ui = status;
	pthread_mutex_unlock(&audio_status_for_ui_mutex);

	// send event signal to packet sender
	pthread_mutex_lock(&status_event_for_ui_mutex);
	ret = pthread_cond_signal(&status_event_for_ui);
	if (ret != 0) {
		logger("ERROR: pthread_cond_signal: %d\n", ret);
	}
	pthread_mutex_unlock(&status_event_for_ui_mutex);
}

void
notify_ui_eof()
{
	notify_packet_sender(STATUS_STOP);
}

int
prepare_native_codec()
{
	int err;
	err = open_file_sf(current_filename);
	if (err == -1) {
		logger("ERROR: can't open audio file\n");
		return (-1);
	}

	set_audio_format();

	if (open_audio_device() == -1) {
		logger("ERROR: can't open audio device\n");
		sf_close(sndfile);
		return (-1);
	}

	return (0);
}

int
prepare_audio_file_and_codec()
{
	int err, idx;

	pthread_mutex_lock(&audio_cmd_mutex);
	strncpy(current_filename, audio_cmd_str, NAME_MAX);
	pthread_mutex_unlock(&audio_cmd_mutex);

	// getting index for file type from supported_files[]
	idx = get_file_type(current_filename);
	if (idx == -1) {
		logger("ERROR: can't get file type\n");
		return (-1);
	}

	if (idx == 4) {
		// use 'mad' codec for mp3
		err = prepare_mad_codec();
		if (err == -1) {
			logger("ERROR: can't prepare mad codec\n");
			return (-1);
		}
		use_codec = true;
	} else {
		err = prepare_native_codec();
		use_codec = false;
	}
	return (err);
}

exit_reason_t
play_file_using_native_codec()
{
	int buf_len, buf_size;
	static bool paused = false, shifted = false;
	bool last_read = false;
	sfinfo.format = 0;
	int read_cnt = 0;
	int i, play_chunk;
	char *bufp = NULL;
	sf_count_t count, seek_ret, seek_frames;
	info_t command;

	// buf_len = format.bits/8 * format.channels * format.rate / 4;
	buf_len = format.bits/8 * format.channels * format.rate;
	seek_frames = buf_len * 4;
	buf_size = buf_len * sizeof (int);
	play_chunk = buf_size / 16;
	logger("play_chunk: %d\n", play_chunk);
	buffer = malloc(buf_size);
	if (buffer == NULL) {
		logger("buffer error: %s\n", strerror(errno));
		return (EXIT_REASON_ERROR);
	}

	// reading a file
	for (;;) {
		count = sf_read_int(sndfile, buffer, buf_len);
		logger("read from file: %d\n", (int)count);
		logger("read_cnt: %d\n", read_cnt);
		if ((int)count == 0) {
			// end of file
			notify_ui_eof();
			pthread_mutex_lock(&audio_cmd_mutex);
			audio_cmd = STATUS_STOP;
			pthread_mutex_unlock(&audio_cmd_mutex);
			return (EXIT_REASON_EOF);
		}
		if (count < buf_len) {
			if (count <= play_chunk) {
				// play the rest of the buffer
				ao_play(device, (char *)buffer, count);
				notify_ui_eof();

				pthread_mutex_lock(&audio_cmd_mutex);
				audio_cmd = STATUS_STOP;
				pthread_mutex_unlock(&audio_cmd_mutex);
				return (EXIT_REASON_EOF);
			}
			last_read = true;
		}

		// play in small chunks
		bufp = (char *)buffer;
		for (i = 0; i < 16; i++) {
			// checking for the command
			pthread_mutex_lock(&audio_cmd_mutex);
			command = audio_cmd;
			pthread_mutex_unlock(&audio_cmd_mutex);

			switch (command) {
			case CMD_PLAY:
				logger("engine_ao - CMD_PLAY\n");
				return (EXIT_REASON_PLAY_OTHER);
				break;
			case CMD_STOP:
				logger("engine_ao - CMD_STOP\n");
				return (EXIT_REASON_STOP);
			case CMD_QUIT:
				logger("engine_ao - CMD_QUIT\n");
				return (EXIT_REASON_QUIT);
			case CMD_PAUSE:
				logger("engine_ao - CMD_PAUSE\n");
				paused = true;
				break;
			case CMD_FF:
				logger("engine_ao - CMD_FF\n");
				seek_ret = sf_seek(sndfile, seek_frames, SEEK_CUR);
				logger("seek_ret: %lld\n", seek_ret);
				if (seek_ret == -1)
					sf_seek(sndfile, 0, SEEK_END);
				shifted = true;
				audio_cmd = STATUS_ACK;
				break;
			case CMD_REV:
				logger("engine_ao - CMD_REV\n");
				seek_ret = sf_seek(sndfile, 0, SEEK_CUR);
				if (seek_ret <= seek_frames)
					seek_ret = sf_seek(sndfile, 0, SEEK_SET);
				else
					seek_ret = sf_seek(sndfile, -seek_frames, SEEK_CUR);
				logger("seek_ret: %lld\n", seek_ret);
				audio_cmd = STATUS_ACK;
				shifted = true;
				break;
			case STATUS_ACK:
				break;
			default:
				logger("engine_ao - TODO: %d\n", command);
				;;
			}

			// CMD_FF, CMD_REV, CMD_PLAY (replay)
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

			// check for STOP or PLAY commands
			pthread_mutex_lock(&audio_cmd_mutex);
			command = audio_cmd;
			pthread_mutex_unlock(&audio_cmd_mutex);

			if (command == CMD_STOP) {
				logger("engine_ao - CMD_STOP\n");
				return (EXIT_REASON_STOP);
			} else if (command == CMD_PLAY) {
				logger("engine_ao - CMD_PLAY\n");
				return (EXIT_REASON_PLAY_OTHER);
			}

			// play sound
			ao_play(device, bufp, play_chunk);
			bufp += play_chunk;
			if (last_read) {
				// TODO
				logger("count: %d\n", count);
				logger("play_chunk: %d\n", play_chunk);
				logger("bufp - (char *)buffer: %d\n", bufp - (char *)buffer);
				if (bufp - (char *)buffer >= count) {
					notify_ui_eof();
					break;
				}
			}
		}
		read_cnt++;
	}
	return (EXIT_REASON_EOF);
}

/*
 * This is an audio I/O thread.
 */
void *
engine_ao()
{
	unsigned int command;
	exit_reason_t ret = EXIT_REASON_UNKNOWN;

	for (;;) {
		switch (ret) {
		case (EXIT_REASON_UNKNOWN):
		case (EXIT_REASON_ERROR):
		case (EXIT_REASON_STOP):
		case (EXIT_REASON_EOF):
			// waiting for new event
			pthread_mutex_lock(&ao_event_mutex);
			if (pthread_cond_wait(&ao_event, &ao_event_mutex) != 0) {
				logger("ERROR: pthread_cond_wait failed\n");
				// TODO
			}
			pthread_mutex_unlock(&ao_event_mutex);
			break;
		default:
			;;
		}

		pthread_mutex_lock(&audio_cmd_mutex);
		command = audio_cmd;
		if (command == CMD_PLAY)
			audio_cmd = STATUS_ACK;
		pthread_mutex_unlock(&audio_cmd_mutex);

		switch (command) {
		case CMD_PLAY:
			logger("engine_ao - CMD_PLAY\n");
			if (prepare_audio_file_and_codec() == -1) {
				ret = EXIT_REASON_ERROR;
				break;
			}
			if (use_codec) {
				ret = play_file_using_mad_codec();
				cleanup_mad_codec();
			} else {
				ret = play_file_using_native_codec();
				cleanup_native_codec();
			}
			break;
		case CMD_QUIT:
			logger("engine_ao - CMD_QUIT\n");
			pthread_exit(NULL);
		default:
			;;
		}
	}
}

void
play_command(char *filename)
{
	pthread_mutex_lock(&audio_cmd_mutex);
	audio_cmd = CMD_PLAY;
	snprintf(audio_cmd_str, NAME_MAX, "%s", filename);
	pthread_mutex_unlock(&audio_cmd_mutex);

	if (signal_cond_event() != 0) {
		logger("ERROR: PLAY COMMAND FAILED\n");
	}
}

void
stop_command()
{
	pthread_mutex_lock(&audio_cmd_mutex);
	audio_cmd = CMD_STOP;
	pthread_mutex_unlock(&audio_cmd_mutex);

	if (signal_cond_event() != 0) {
		logger("ERROR: STOP COMMAND FAILED\n");
	}
}

void
quit_command()
{
	pthread_mutex_lock(&audio_cmd_mutex);
	audio_cmd = CMD_QUIT;
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
	if (audio_cmd == CMD_PLAY || audio_cmd == STATUS_ACK) {
		audio_cmd = CMD_PAUSE;
	} else if (audio_cmd == CMD_PAUSE) {
		audio_cmd = STATUS_ACK;
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
	int i, err, sock_fd;
	struct sockaddr_in addr;
	int in_queue = 5;

	memset(&addr, 0, sizeof (addr));
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	addr.sin_port = htons(DAEMON_PORT);

	sock_fd = socket(PF_INET, SOCK_STREAM, IPPROTO_IP);
	if (!sock_fd) {
		logger("ERROR: socket error: %s\n", strerror(errno));
		return (0);
	}

	// use loop to avoid 'Address already in use' bind error
	for (i = 0;; i++) {
		err = bind(sock_fd, (struct sockaddr *)&addr, sizeof (addr));
		if (err == 0)
			break;

		if (i == 15) {
			close(sock_fd);
			return (0);
		}

		logger("ERROR: bind error: %s\n", strerror(errno));
		sleep(3);
	}

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

	struct pkt_header pkt_hdr, host_pkt_hdr;
	host_pkt_hdr.info = CMD_UNKNOWN;

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

		host_pkt_hdr.info = ntohl(pkt_hdr.info);
		host_pkt_hdr.size = ntohl(pkt_hdr.size);
		logger("receiver host_pkg_hdr.info: %d\n", (unsigned int)host_pkt_hdr.info);
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

		logger("received command: %d\n", host_pkt_hdr.info);
		if (has_content) {
			logger("received string: %s\n", str_buf);
		}

		switch (host_pkt_hdr.info) {
		case CMD_PLAY:
			logger("socket_daemon received CMD_PLAY\n");
			play_command(str_buf);
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
			quit_command();
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
