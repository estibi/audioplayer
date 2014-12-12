#include <errno.h>
#include <ncurses.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <unistd.h>

#include "audio_engine.h"
#include "utils.h"

int sock_fd;
WINDOW *main_win, *status_win;

struct ui_file_list {
	struct dir_contents *contents;
	// first file to show
	unsigned int head_idx;
	// last file to show
	unsigned int tail_idx;
	// selected file
	unsigned int cur_idx;
} file_list;

void show_files(WINDOW *w);

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
prepare_main_window()
{
	int w_height, w_width, w_starty = 0, w_startx = 0;

	getmaxyx(stdscr, w_height, w_width);
	WINDOW *win = newwin(w_height, w_width - 20, w_starty, w_startx);

	init_pair(1, COLOR_RED, COLOR_GREEN);
	attron(COLOR_PAIR(1));

	wbkgd(win, COLOR_PAIR(1));
	box(win, 0, 0);
	attroff(COLOR_PAIR(1));
	wrefresh(win);
	return (win);
}

WINDOW*
prepare_status_window()
{
	// screen size
	int scr_height, scr_width;
	// window size
	int w_height, w_width, w_starty, w_startx;

	getmaxyx(stdscr, scr_height, scr_width);

	w_height = 20;
	w_width = 20;
	w_starty = 0;
	w_startx = scr_width - w_width;
	WINDOW *win = newwin(w_height, w_width, w_starty, w_startx);

	init_pair(1, COLOR_RED, COLOR_GREEN);
	attron(COLOR_PAIR(1));

	wbkgd(win, COLOR_PAIR(1));
	box(win, 0, 0);
	attroff(COLOR_PAIR(1));
	wrefresh(win);
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
	notimeout(main_win, true);

	for (;;) {
		mvwprintw(main_win, 0, 1, "press p to play, s to stop, q to quit..");
		wrefresh(main_win);

		key = wgetch(main_win);
		switch (key) {
		case KEY_UP:
			break;
		case 'p':
			mvwprintw(status_win, 1, 5, "CMD: PLAY ");
			send_play_command(sock_fd);
			break;
		case ' ':
			mvwprintw(status_win, 1, 5, "CMD: PAUSE");
			send_pause_command(sock_fd);
			break;
		case 'q':
			mvwprintw(status_win, 1, 5, "CMD: QUIT ");
			send_quit_command(sock_fd);
			wrefresh(status_win);
			return;
		case 's':
			mvwprintw(status_win, 1, 5, "CMD: STOP ");
			send_stop_command(sock_fd);
			break;
		case 68:
			mvwprintw(status_win, 1, 5, "CMD: REV  ");
			send_rev_command(sock_fd);
			break;
		case 67:
			mvwprintw(status_win, 1, 5, "CMD: FF   ");
			send_ff_command(sock_fd);
			break;
		case 66:
			// DOWN - scroll files
		if (file_list.cur_idx < file_list.contents->amount -1 &&
					file_list.cur_idx < file_list.tail_idx) {
				file_list.cur_idx += 1;
				show_files(main_win);
				break;
			}
			if (file_list.cur_idx < file_list.contents->amount -1 &&
					file_list.cur_idx == file_list.tail_idx) {
				file_list.cur_idx += 1;
				file_list.head_idx += 1;
				file_list.tail_idx += 1;
				show_files(main_win);
				break;
			}
			break;
		case 65:
			// UP - scroll files
			if (file_list.cur_idx > file_list.head_idx) {
				file_list.cur_idx -= 1;
				//file_list.tail_idx -= 1;
				show_files(main_win);
				break;
			}
			if (file_list.cur_idx == file_list.head_idx &&
					file_list.head_idx > 0) {
				file_list.head_idx -= 1;
				file_list.tail_idx -= 1;
				file_list.cur_idx -= 1;
				show_files(main_win);
				break;
			}
			break;
		default:
			mvwprintw(status_win, 10, 1, "pressed:");
			mvwprintw(status_win, 11, 1, "%3d as '%c'", key, key);
			break;
		}
		wrefresh(status_win);
	}
}

int
init_file_list(WINDOW *w)
{
	int y, x;
	struct dir_contents *contents;
	contents = malloc(sizeof (struct dir_contents));
	if (!contents) {
		return (0);
	}
	file_list.contents = contents;

	getmaxyx(w, y, x);

	file_list.head_idx = 0;
	file_list.tail_idx = y - 3;
	file_list.cur_idx = 0;

	return (1);
}

void
fini_file_list()
{
	free(file_list.contents);
}

void
show_files(WINDOW *w)
{
	int index, err, y_pos, win_y, win_x;
	struct dir_contents *contents;
	char *dir_path = ".";

	contents = file_list.contents;
	getmaxyx(w, win_y, win_x);

	err = scan_dir(dir_path, contents);
	if (err == -1) {
		mvwprintw(w, 1, 1, "ERROR - CAN'T LOAD FILES");
		wrefresh(w);
		return;
	}

	index = file_list.head_idx;

	y_pos = 1;

	/*
	mvwprintw(w, 0, 0, "%*s", win_x - 2, " ");
	mvwprintw(w, 0, 0,
		"/.. DEBUG: index: %d head_idx %d head_tail: %d current: %d amount: %d",
		index, file_list.head_idx, file_list.tail_idx,
		file_list.cur_idx, file_list.contents->amount);
	*/
	for (; index < contents->amount; index++) {
		if (index > file_list.tail_idx)
			break;
		// clear a line with spaces
		mvwprintw(w, y_pos, 1, "%*s", win_x - 2, " ");

		if (file_list.cur_idx == index) {
			mvwprintw(w, y_pos, 1, "%s  <--", &contents->list[index]);
		} else {
			mvwprintw(w, y_pos, 1, "%s", &contents->list[index]);
		}
		y_pos++;
	}
	wrefresh(w);
}

void
ui_cleanup()
{
	close(sock_fd);

	delwin(main_win);
	delwin(status_win);
	endwin();
}

void
ui_init()
{
	initscr();
	noecho();
	main_win = prepare_main_window();
	status_win = prepare_status_window();
}


void
curses_ui()
{
	int err;
	ui_init();

	err = init_file_list(main_win);
	if (err == 0) {
		// TODO
		return;
	}

	sock_fd = get_client_socket();

	show_files(main_win);
	curses_loop();

	fini_file_list();
	ui_cleanup();
}
