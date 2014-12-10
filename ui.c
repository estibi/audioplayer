#include <errno.h>
#include <ncurses.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <unistd.h>

#include "audio_engine.h"

int sock_fd;

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
	return (send_command(sock_fd, cmd, "test.wav"));
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
	if (!sock_fd) {
		printw("ui socket error: %s\n", strerror(errno));
		refresh();
	}

	// FIXME: waiting for a server (audio daemon)
	sleep(1);

	for (;;) {
		err = connect(sock_fd, (struct sockaddr *)&addr, sizeof (addr));
		if (err == 0) {
			return (sock_fd);
		}

		printw("ui connect error: %s\n", strerror(errno));
		refresh();
		sleep(1);
	}
}

void
curses_loop()
{
	int key;

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

void
init_curses_ui()
{
	WINDOW *main_win;

	initscr();
	noecho();
	main_win = prepare_window();
	sock_fd = get_client_socket();

	curses_loop();
	endwin();
}
