#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <dirent.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <limits.h>

#include <ao/ao.h>
#include <math.h>
#include <sndfile.h>

#include <ncurses.h>
#include <signal.h>
#include <pthread.h>

#define	AUDIO_FILE "test.wav"
#define	FILES_IN_DIR_LIMIT 20000
#define	NAME_MAX 255

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

typedef enum {
	CMD_PLAY,
	CMD_STOP,
	CMD_EXIT,
	CMD_PAUSE,
	CMD_FF,
	CMD_REV
} ao_cmd_t;

typedef struct filename_t {
	char name[NAME_MAX];
} filename;

typedef struct dir_contents_t {
	filename list[FILES_IN_DIR_LIMIT];
	unsigned int amount;
} dir_contents;


pthread_mutex_t ao_cmd_mutex = PTHREAD_MUTEX_INITIALIZER;
volatile ao_cmd_t ao_cmd;

pthread_mutex_t ao_event_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t ao_event = PTHREAD_COND_INITIALIZER;

int *buffer;

void quit_command();
int signal_cond_event();
void show_dir_content(dir_contents *contents);


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
	printw("CLEANING UP: ao_close()\n");
	refresh();
	ao_close(device);
	printw("CLEANING UP: sf_close()\n");
	refresh();
	sf_close(sndfile);
	printw("CLEANING UP: free()\n");
	refresh();
	free(buffer);
	printw("CLEANING UP: mutex()\n");
	refresh();
	pthread_mutex_unlock(&ao_cmd_mutex);
	printw("CLEANING UP - DONE\n");
	refresh();
}

void
exit_sndfile()
{
	printw("CLEANING UP: sf_close()\n");
	refresh();
	sf_close(sndfile);
}

static void
scan_dir(char *dir_path, dir_contents *contents)
{
	DIR *dirp = NULL;
	struct dirent *ent = NULL;

	int index = 0;

	dirp = opendir(dir_path);
	if (!dirp) {
		printf("ERROR: Can't open '%s':\n", dir_path);
		printf("%s\n", strerror(errno));
		return;
	}

	while ((ent = readdir(dirp)) != NULL) {
		if (ent->d_name[0] == '.')
			continue; /* skip . and .. */
		snprintf((char *)&contents->list[index],
			sizeof (filename),
			"%s",
			ent->d_name);
		index++;
	}
	contents->amount = index;
	(void) closedir(dirp);
}


void
show_dir_content(dir_contents *contents)
{
	int index;
	for (index = 0; index < contents->amount; index++) {
		printw("file: %s\n", &contents->list[index]);
	}
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
		printw("sf_open error: %s\n", strerror(errno));
		refresh();
		pthread_exit(NULL);
	}

	memset(&format, 0, sizeof (format));
	format.channels = sfinfo.channels;
	format.rate = sfinfo.samplerate;
	format.byte_format = AO_FMT_NATIVE;
	format.bits = 32;	// 32 for sf_read_int(), 16 for sf_read_short()

	//buf_len = format.bits/8 * format.channels * format.rate / 4;
	buf_len = format.bits/8 * format.channels * format.rate;
	seek_frames = buf_len * 4;
	buf_size = buf_len * sizeof (int);
	play_chunk = buf_size / 16;
	printw("play_chunk: %d\n", play_chunk);
	refresh();
	buffer = malloc(buf_size);
	if (buffer == NULL) {
		printw("buffer error: %s\n", strerror(errno));
		refresh();
		pthread_exit(NULL);
	}

	// libao
	device = ao_open_live(default_driver, &format, NULL);
	if (device == NULL) {
		printw("error: %s\n", strerror(errno));
		refresh();
		pthread_exit(NULL);
	}

	// reading a file
	for (;;) {
		count = sf_read_int(sndfile, buffer, buf_len);
		printw("counter: %d\n", (int)count);
		printw("read_cnt: %d\n", read_cnt);
		refresh();
		if ((int)count == 0) {
			// end of file
			pthread_mutex_lock(&ao_cmd_mutex);
			ao_cmd = CMD_STOP;
			pthread_exit(NULL);
		}

		// play in small chunks
		bufp = (char *)buffer;
		for (i = 0; i < 16; i++) {
			// checking for the command
			pthread_mutex_lock(&ao_cmd_mutex);
			switch (ao_cmd) {
			case CMD_STOP:
				printw("CMD_STOP\n");
				refresh();
				pthread_exit(NULL);
			case CMD_PAUSE:
				printw("CMD_PAUSE\n");
				refresh();
				paused = true;
				break;
			case CMD_FF:
				printw("CMD_FF\n");
				refresh();
		        seek_ret = sf_seek(sndfile, seek_frames, SEEK_CUR);
				printw("seek_ret: %d\n", seek_ret);
				refresh();
				if (seek_ret == -1)
			        sf_seek(sndfile, 0, SEEK_END);
				shifted = true;
				ao_cmd = CMD_PLAY;
				break;
			case CMD_REV:
				printw("CMD_REV\n");
				refresh();
				seek_ret = sf_seek(sndfile, 0, SEEK_CUR);
				if (seek_ret <= seek_frames)
					seek_ret = sf_seek(sndfile, 0, SEEK_SET);
				else
					seek_ret = sf_seek(sndfile, -seek_frames, SEEK_CUR);
				printw("seek_ret: %d\n", seek_ret);
				refresh();
				ao_cmd = CMD_PLAY;
				shifted = true;
				break;
			default:
				;;
			}
			pthread_mutex_unlock(&ao_cmd_mutex);

			if (shifted) {
				shifted = false;
				break;
			}

			if (paused) {
				pthread_mutex_lock(&ao_event_mutex);
				// waiting for an event
				if (pthread_cond_wait(&ao_event, &ao_event_mutex) != 0) {
					printw("pthread_cond_wait failed\n");
					refresh();
					getch();
				}
				pthread_mutex_unlock(&ao_event_mutex);
				paused = false;
			}
			// check if STOP command was sent
			pthread_mutex_lock(&ao_cmd_mutex);
			if (ao_cmd == CMD_STOP) {
				printw("CMD_STOP\n");
				refresh();
				pthread_exit(NULL);
			}
			pthread_mutex_unlock(&ao_cmd_mutex);

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
	pthread_mutex_lock(&ao_cmd_mutex);
	ao_cmd = CMD_PLAY;
	pthread_mutex_unlock(&ao_cmd_mutex);

	if (pthread_create(&ao_thread, aot_attr, ao_play_file, ao_arg) != 0) {
		printw("pthread_create error\n");
		refresh();
	}
}

void
quit_command()
{
	printw("QUIT COMMAND: start\n");
	refresh();

	pthread_mutex_lock(&ao_cmd_mutex);
	ao_cmd = CMD_STOP;
	pthread_mutex_unlock(&ao_cmd_mutex);
	
	if (signal_cond_event() != 0) {
		printw("QUIT COMMAND FAILED\n");
		refresh();
	}

	printw("QUIT COMMAND: done\n");
	refresh();
}

void
ff_command()
{
	printw("FF COMMAND: start\n");
	refresh();

	pthread_mutex_lock(&ao_cmd_mutex);
	ao_cmd = CMD_FF;
	pthread_mutex_unlock(&ao_cmd_mutex);
	
	if (signal_cond_event() != 0) {
		printw("FF COMMAND FAILED\n");
		refresh();
	}

	printw("FF COMMAND: done\n");
	refresh();
}

void
rev_command()
{
	printw("REV COMMAND: start\n");
	refresh();

	pthread_mutex_lock(&ao_cmd_mutex);
	ao_cmd = CMD_REV;
	pthread_mutex_unlock(&ao_cmd_mutex);
	
	if (signal_cond_event() != 0) {
		printw("REV COMMAND FAILED\n");
		refresh();
	}

	printw("REV COMMAND: done\n");
	refresh();
}

int
signal_cond_event()
{
	pthread_mutex_lock(&ao_event_mutex);
	printw("@@@ pthread_cond_signal, starting..\n");
	refresh();
	int ret = pthread_cond_signal(&ao_event);
	printw("@@@ pthread_cond_signal: %d\n", ret);
	refresh();
	pthread_mutex_unlock(&ao_event_mutex);
	return (0);
}

void
pause_command()
{
	printw("PAUSE COMMAND: start\n");
	refresh();

	pthread_mutex_lock(&ao_cmd_mutex);
	if (ao_cmd == CMD_PLAY) {
		ao_cmd = CMD_PAUSE;
	} else if (ao_cmd == CMD_PAUSE){
		ao_cmd = CMD_PLAY;
		pthread_mutex_lock(&ao_event_mutex);
		printw("@@@ pthread_cond_signal, starting..\n");
		refresh();
		int ret = pthread_cond_signal(&ao_event);
		printw("@@@ pthread_cond_signal: %d\n", ret);
		refresh();
		pthread_mutex_unlock(&ao_event_mutex);
	}
	pthread_mutex_unlock(&ao_cmd_mutex);

	printw("PAUSE COMMAND: done\n");
	refresh();
}

WINDOW*
prepare_window()
{
	//int w_height = 20, w_width = 80, w_starty = 0, w_startx = 0;
	int w_height, w_width, w_starty = 0, w_startx = 0;

	getmaxyx(stdscr, w_height, w_width);
	WINDOW * win = newwin(w_height, w_width, w_starty, w_startx);

	init_pair(1, COLOR_RED, COLOR_GREEN);
	attron(COLOR_PAIR(1));

	wbkgd(win, COLOR_PAIR(1));
	box(win, 0, 0);
	mvwprintw(win, 1, 1, "testing win..");
	refresh();
	wrefresh(win);
	attroff(COLOR_PAIR(1));
	return win;
}

void
curses_loop()
{
	int key;
	dir_contents *contents;
	WINDOW *main_win;

	contents = malloc(sizeof (dir_contents));
	if (contents == NULL) {
		printw("malloc error for directory content");
		refresh();
		getch();
		return;
	}
	main_win = prepare_window();

	//printw("SIZE: %d", sizeof (dir_contents));
	//refresh();
	//getch();
	scan_dir(".", contents);
	for (;;) {
		mvprintw(0, 0, "press p to play, s to stop, q to quit..");
		refresh();

		key = getch();
		switch(key) {
		case KEY_UP:
			break;
		case 'p':
			play_file();
			break;
		case ' ':
			pause_command();
			break;
		case 'q':
			quit_command();
			free(contents);
			return;
		case 's':
			quit_command();
			break;
		case 68:
			rev_command();
			break;
		case 67:
			ff_command();
			break;
		default:
			mvprintw(24, 0, "pressed = %3d as '%c'", key, key);
			break;
		}
		timeout(5000);
	}
}

int
main(int argc, char **argv) 
{
	ao_initialize();
	default_driver = ao_default_driver_id();

	initscr();
	noecho();
	curses_loop();

	printw("waiting for a thread..\n");
	pthread_join(ao_thread, NULL);

	endwin();
	ao_shutdown();
	return (0);
}
