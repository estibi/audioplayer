#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <dirent.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <limits.h>

#include <ncurses.h>
#include <signal.h>
#include <sys/socket.h>
#include <netinet/in.h>

#include "audio_engine.h"


typedef struct filename_t {
	char name[NAME_MAX];
} filename;

typedef struct dir_contents_t {
	filename list[FILES_IN_DIR_LIMIT];
	unsigned int amount;
} dir_contents;



int signal_cond_event();
void show_dir_content(dir_contents *contents);



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


int
send_command(int sock_fd, cmd_t cmd, char *s)
{
	int len;
	unsigned int str_size, buf_size;
	const int MAX_STRING = 256;
	char *p, *raw_buf;

	struct cmd_pkt_header pkt_hdr;

	pkt_hdr.cmd = htonl(cmd);

	if (s) {
		str_size = strnlen(s, MAX_STRING);
		pkt_hdr.size = htonl(str_size + 1);
		buf_size = sizeof (pkt_hdr) + str_size + 1;
	} else {
		str_size = 0;
		pkt_hdr.size = htonl(0);
		buf_size = sizeof (pkt_hdr);
	}
	// printf("DEBUG: sender buf_size: %d\n", buf_size);
	raw_buf = malloc(buf_size);
	if (!raw_buf) {
		printf("malloc error: %s\n", strerror(errno));
		return (-1);
	}

	// copy packet header
	p = raw_buf;
	memcpy(p, &pkt_hdr, sizeof (pkt_hdr));

	if (s) {
		// copy command string
		p += sizeof (pkt_hdr);
		memcpy(p, s, str_size);

		// terminate string with 0
		p += str_size;
		memset(p, '\0', 1);
	}

	len = write(sock_fd, raw_buf, buf_size);
	if (len != buf_size) {
		printf("write error: %s\n", strerror(errno));
		printf("len: %d\n", len);
		printf("buf_size: %d\n", (unsigned int) buf_size);
		free(raw_buf);
		return (-1);
	}
	free(raw_buf);
	return (0);
}


int
send_play_command(int sock_fd)
{
	cmd_t cmd = CMD_PLAY;
	return (send_command(sock_fd, cmd, "this_is_not_used.wav"));
}

int
send_pause_command(int sock_fd)
{
	cmd_t cmd = CMD_PAUSE;
	return (send_command(sock_fd, cmd, NULL));
}

int
send_quit_command(int sock_fd)
{
	cmd_t cmd = CMD_QUIT;
	return (send_command(sock_fd, cmd, NULL));
}

int
send_stop_command(int sock_fd)
{
	cmd_t cmd = CMD_STOP;
	return (send_command(sock_fd, cmd, NULL));
}

int
send_ff_command(int sock_fd)
{
	cmd_t cmd = CMD_FF;
	return (send_command(sock_fd, cmd, NULL));
}

int
send_rev_command(int sock_fd)
{
	cmd_t cmd = CMD_REV;
	return (send_command(sock_fd, cmd, NULL));
}


WINDOW*
prepare_window()
{
	// int w_height = 20, w_width = 80, w_starty = 0, w_startx = 0;
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
	return (win);
}

int
get_client_socket()
{
	int sock_fd, err;
	struct sockaddr_in addr;

	memset(&addr, 0, sizeof (addr));
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	addr.sin_port = htons(10000);

	sock_fd = socket(PF_INET, SOCK_STREAM, IPPROTO_IP);
	// printf("sock_fd: %d\n", sock_fd);
	if (!sock_fd) {
		printf("socket error: %s\n", strerror(errno));
	}

	// TODO:
	sleep(2);

	err = connect(sock_fd, (struct sockaddr *)&addr, sizeof (addr));
	if (err != 0) {
		printf("connect error: %s\n", strerror(errno));
		return (-1);
	}
	return (sock_fd);
}

void
curses_loop()
{
	int key;
	WINDOW *main_win;
	int sock_fd;

	sock_fd = get_client_socket();

	main_win = prepare_window();

	for (;;) {
		mvprintw(0, 0, "press p to play, s to stop, q to quit..");
		refresh();

		key = getch();
		switch (key) {
		case KEY_UP:
			break;
		case 'p':
			send_play_command(sock_fd);
			break;
		case ' ':
			send_pause_command(sock_fd);
			break;
		case 'q':
			send_quit_command(sock_fd);
			return;
		case 's':
			send_stop_command(sock_fd);
			break;
		case 68:
			send_rev_command(sock_fd);
			break;
		case 67:
			send_ff_command(sock_fd);
			break;
		default:
			mvprintw(24, 0, "pressed = %3d as '%c'", key, key);
			break;
		}
		timeout(5000);
	}
}

int
init_audio_engine()
{
	int pid;
	pid = fork();
	printf("pid: %d\n", pid);
	if (pid < 0) {
		printf("fork error, exiting..\n");
		exit(1);
	}
	if (pid > 0) {
		// parent process
		return (pid);
	}

	// we are a child process
	engine_daemon();
	return (0);
}

int
main(int argc, char **argv)
{
	int daemon_pid;
	daemon_pid = init_audio_engine();

	initscr();
	noecho();
	curses_loop();

	// TODO: wait for audio engine

	endwin();
	return (0);
}
