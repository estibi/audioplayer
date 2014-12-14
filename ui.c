#include <ncurses.h>
#include <pthread.h>
#include <signal.h>
#include <stdbool.h>
#include <sys/param.h>

#include "protocol.h"
#include "utils.h"

int sock_fd;
WINDOW *main_win, *status_win;

struct ui_file_list {
	struct dir_contents *contents;
	char *dir_name;
	// first file to show
	unsigned int head_idx;
	// last file to show
	unsigned int tail_idx;
	// selected file
	unsigned int cur_idx;
} file_list;

// socket receiver thread
pthread_t receiver_thread = NULL;
pthread_attr_t *rcv_attr = NULL;
void *rcv_arg = NULL;

void show_files(WINDOW *w, bool);
void free_dir_list();
int init_list_for_dir();


int
change_directory(char *dir)
{
	int err, y, x;

	if (chdir(dir) == -1) {
		mvwprintw(status_win, 3, 1, "can't change dir: %s", dir);
		return (-1);
	}

	if (getcwd(file_list.dir_name, MAXPATHLEN) == 0) {
		mvwprintw(status_win, 3, 1, "can't get current directory");
		return (-1);
	}

	// reset cursor/file position after changing directory
	file_list.cur_idx = 0;

	// clean up old list
	free_dir_list();

	// allocate new list
	init_list_for_dir();

	//populate list with file/directory names
	err = scan_dir(file_list.contents);
	if (err == -1) {
		mvwprintw(status_win, 1, 1, "ERROR in change_directory()");
		wrefresh(status_win);
		return (-1);
	}

	getmaxyx(main_win, y, x);

	file_list.head_idx = 0;
	file_list.tail_idx = y - 3;
	file_list.cur_idx = 0;

	show_files(main_win, true);
	return (0);
}

int
init_list_for_dir(char *dir)
{
	int i, amount;
	struct dir_contents *contents;
	fileobj **list;

	amount = count_dir_entries(".");
	if (amount == 0)
		return (-1);

	contents = malloc(sizeof (contents));
	if (!contents) {
		return (-1);
	}

	list = malloc(sizeof (contents->list) * amount);
	if (!list) {
		return (-1);
	}

	for (i = 0; i < amount; i++) {
		list[i] = malloc(NAME_MAX);
		if (!list[i]) {
			return (-1);
		}
	}

	contents->list = list;
	contents->amount = amount;

	file_list.contents = contents;

	return (0);
}

void
free_dir_list()
{
	int i;

	for (i = 0; i < file_list.contents->amount; i++) {
		free(file_list.contents->list[i]);
	}

	free(file_list.contents->list);
	free(file_list.contents);
}

int
first_run_file_list(WINDOW *w)
{
	int y, x, err;

	if (init_list_for_dir(".") == -1) {
		return (-1);
	}

	if (getcwd(file_list.dir_name, MAXPATHLEN) == 0) {
		mvwprintw(status_win, 3, 1, "can't get current directory");
		return (-1);
	}

	err = scan_dir(file_list.contents);
	if (err == -1) {
		mvwprintw(w, 1, 1, "ERROR - CAN'T LOAD FILES");
		wrefresh(w);
		return (-1);
	}

	getmaxyx(w, y, x);

	file_list.head_idx = 0;
	file_list.tail_idx = y - 3;
	file_list.cur_idx = 0;

	return (0);
}

int
key_enter()
{
	info_t cmd = CMD_PLAY;
	struct dir_contents *contents;
	char *ptr, *buf;
	unsigned int buf_size;

	contents = file_list.contents;

	// file of directory name
	ptr = (char *)&contents->list[file_list.cur_idx]->name;

	if (is_directory(ptr)) {
		mvwprintw(status_win, 1, 5, "CHDIR  ");

		buf_size = strlen(ptr) + 1;
		buf = malloc(buf_size);
		if (!buf) {
			mvwprintw(status_win, 3, 5, "MALLOC ERROR");
			return (-1);
		}
		strncpy(buf, ptr, strlen(ptr));
		buf[buf_size - 1] = '\0';
		if (change_directory(buf) == -1) {
			mvwprintw(status_win, 3, 5, "ERROR: %s", buf);
			free(buf);
			return (-1);
		}
		free(buf);
		return (0);
	}

	mvwprintw(status_win, 1, 5, "CMD: PLAY ");
	return (send_packet(sock_fd, cmd, ptr));
}


void
key_down()
{
	// DOWN - scroll files
	if (file_list.cur_idx < file_list.contents->amount -1 &&
			file_list.cur_idx < file_list.tail_idx) {
		file_list.cur_idx += 1;
		show_files(main_win, false);
		return;
	}

	if (file_list.cur_idx < file_list.contents->amount -1 &&
			file_list.cur_idx == file_list.tail_idx) {
		file_list.cur_idx += 1;
			file_list.head_idx += 1;
			file_list.tail_idx += 1;
			show_files(main_win, false);
	}
}

void
key_up()
{
	// UP - scroll files
	if (file_list.cur_idx > file_list.head_idx) {
		file_list.cur_idx -= 1;
		//file_list.tail_idx -= 1;
		show_files(main_win, false);
		return;
	}
	if (file_list.cur_idx == file_list.head_idx &&
			file_list.head_idx > 0) {
		file_list.head_idx -= 1;
		file_list.tail_idx -= 1;
		file_list.cur_idx -= 1;
		show_files(main_win, false);
	}
}

int
send_pause_command(int sock_fd)
{
	info_t cmd = CMD_PAUSE;
	return (send_packet(sock_fd, cmd, NULL));
}

int
send_quit_command(int sock_fd)
{
	info_t cmd = CMD_QUIT;
	return (send_packet(sock_fd, cmd, NULL));
}

int
send_stop_command(int sock_fd)
{
	info_t cmd = CMD_STOP;
	return (send_packet(sock_fd, cmd, NULL));
}

int
send_ff_command(int sock_fd)
{
	info_t cmd = CMD_FF;
	return (send_packet(sock_fd, cmd, NULL));
}

int
send_rev_command(int sock_fd)
{
	info_t cmd = CMD_REV;
	return (send_packet(sock_fd, cmd, NULL));
}

WINDOW*
prepare_main_window()
{
	int w_height, w_width, w_starty = 0, w_startx = 0;

	getmaxyx(stdscr, w_height, w_width);
	WINDOW *win = newwin(w_height, w_width - 30, w_starty, w_startx);

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

	w_height = scr_height;
	w_width = 30;
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
	addr.sin_port = htons(DAEMON_PORT);

	sock_fd = socket(PF_INET, SOCK_STREAM, IPPROTO_IP);
	if (!sock_fd) {
		printw("ui socket error: %s\n", strerror(errno));
		refresh();
	}

	// FIXME: waiting for a server (audio daemon)
	sleep(2);

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
received_status_stop()
{
	//int stdscr, w_height, w_width;
	//getmaxyx(status_win, w_height, w_width);

	mvwprintw(status_win, 1, 5, "STATUS_STOP\n");
	wrefresh(status_win);
}

/*
 * Thread - receives status packets from audio engine.
 */
void *
ui_socket_receiver()
{
	int len;
	char *str_buf;
	bool has_content = false;

	struct pkt_header pkt_hdr, host_pkt_hdr;
	host_pkt_hdr.info = CMD_UNKNOWN;

	for (;;) {
		len = read(sock_fd, &pkt_hdr, sizeof (pkt_hdr));

		//printw("read %d bytes from socket\n", len);
		if (len == 0) {
			printw("ERROR: - connection closed (initial read)\n");
			break;
		}
		if (len < sizeof (pkt_hdr)) {
			printw("ERROR: - received less bytes from socket\n");
			break;
		}

		host_pkt_hdr.info = ntohl(pkt_hdr.info);
		host_pkt_hdr.size = ntohl(pkt_hdr.size);
		//printw("receiver host_pkg_hdr.info: %d\n",
		//	(unsigned int)host_pkt_hdr.info);
		//printw("receiver host_pkg_hdr.size: %d\n",
		//	(unsigned int)host_pkt_hdr.size);
		//refresh();

		if (host_pkt_hdr.size > 0) {
			str_buf = malloc(host_pkt_hdr.size);
			if (!str_buf) {
				printw("ERROR: ui_socket_receiver() - malloc error: %s\n",
					strerror(errno));
				break;
			}
			has_content = true;

			len = read(sock_fd, str_buf, host_pkt_hdr.size);
			if (len == 0) {
				printw("ERROR: socket_daemon() - connection closed\n");
				break;
			} else if (len < sizeof (str_buf)) {
				printw("ERROR: read less then buffer\n");
				break;
			}
		} else {
			has_content = false;
		}

		//printw("received command: %d\n", host_pkt_hdr.info);
		if (has_content) {
			printw("received string: %s\n", str_buf);
		}

		switch (host_pkt_hdr.info) {
		case STATUS_STOP:
			received_status_stop();
			break;
		default:
			;;
		}
		if (has_content) {
			free(str_buf);
			has_content = false;
		}
	}
	refresh();
	// TODO
	// we are here if error occurred
	if (has_content) {
		free(str_buf);
		has_content = false;
	}
	for (;;) {
		sleep(120);
	}
}

void
curses_loop()
{
	int key, w_height, w_width;

	notimeout(main_win, true);

	for (;;) {
		getmaxyx(status_win, w_height, w_width);
		mvwprintw(status_win, w_height - 2 , 1, "p - play, s - stop, q - quit");
		wrefresh(status_win);

		key = wgetch(main_win);
		switch (key) {
		case KEY_UP:
			break;
		case 10:	// 10 == ENTER
		case 'p':
			key_enter();
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
			key_down();
			break;
		case 65:
			// UP - scroll files
			key_up();
			break;
		default:
			mvwprintw(status_win, 10, 1, "pressed:");
			mvwprintw(status_win, 11, 1, "%3d as '%c'", key, key);
			break;
		}
		wrefresh(status_win);
	}
}

void
show_files(WINDOW *w, bool clear)
{
	int index, y_pos, win_y, win_x;
	struct dir_contents *contents;

	contents = file_list.contents;
	getmaxyx(w, win_y, win_x);

	if (clear)
		wclear(w);
		box(w, 0, 0);

	// show directory name
	// TODO: cut if longer
	mvwprintw(w, 0, 1, "%s", file_list.dir_name);

	index = file_list.head_idx;

	y_pos = 1;

	for (; index < contents->amount; index++) {
		if (index > file_list.tail_idx)
			break;
		// clear a line with spaces
		mvwprintw(w, y_pos, 1, "%*s", win_x - 2, " ");

		if (file_list.cur_idx == index) {
			mvwprintw(w, y_pos, 1, "%s  <--", &contents->list[index]->name);
		} else {
			mvwprintw(w, y_pos, 1, "%s", &contents->list[index]->name);
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

	// invisible cursor
	curs_set(0);

	main_win = prepare_main_window();
	status_win = prepare_status_window();
}


void
curses_ui()
{
	int err;
	ui_init();

	file_list.dir_name = malloc(MAXPATHLEN + 1);
	if (!file_list.dir_name) {
		mvwprintw(main_win, 0, 1, "ERROR: Can't initialize dir_name.");
		wrefresh(main_win);
		return;
	}
	err = first_run_file_list(main_win);
	if (err == -1) {
		mvwprintw(main_win, 0, 1, "ERROR: Can't initialize file list.");
		wrefresh(main_win);
		return;
	}

	sock_fd = get_client_socket();

	err = pthread_create(&receiver_thread, rcv_attr, ui_socket_receiver, rcv_arg);
	if (err != 0) {
		mvwprintw(main_win, 0, 1, "ERROR: receiver thread");
		wrefresh(main_win);
	}

	show_files(main_win, false);
	curses_loop();

	free_dir_list();
	free(file_list.dir_name);

	ui_cleanup();
}
