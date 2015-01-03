#include <ncurses.h>
#include <pthread.h>
#include <signal.h>
#include <stdbool.h>
#include <sys/param.h>

#include "protocol.h"
#include "utils.h"

#define	status_win_width 30

int sock_fd;
WINDOW *main_win, *status_win;

struct window_dimensions {
	unsigned int width;
	unsigned int height;
	unsigned int startx;
	unsigned int starty;
};

struct window_dimensions *main_win_dimensions = NULL;
struct window_dimensions *status_win_dimensions = NULL;

static struct ui_file_list {
	struct dir_contents *contents;
	char *dir_name;
	// first file to show
	unsigned int head_idx;
	// last file to show
	unsigned int tail_idx;
	// selected file
	unsigned int cur_idx;
} file_list;

/*
 * Saved status of audio engine.
 * Used by status_win when doing window resize.
 */
volatile info_t ui_status_cache = STATUS_UNKNOWN;
pthread_mutex_t ui_status_cache_mutex = PTHREAD_MUTEX_INITIALIZER;

// socket receiver thread
pthread_t receiver_thread = NULL;
pthread_attr_t *rcv_attr = NULL;
void *rcv_arg = NULL;

void show_files(WINDOW *w);
void free_dir_list();
int init_list_for_dir();
void show_status();
void handle_resize(WINDOW *w);



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
	err = scan_dir(file_list.contents, false, false);
	if (err == -1) {
		mvwprintw(status_win, 1, 1, "ERROR in change_directory()");
		wrefresh(status_win);
		return (-1);
	}

	getmaxyx(main_win, y, x);

	file_list.head_idx = 0;
	file_list.tail_idx = y - 3;
	file_list.cur_idx = 0;

	show_files(main_win);
	return (0);
}

int
init_list_for_dir(char *dir)
{
	int i, amount;
	struct dir_contents *contents;
	fileobj **list;

	amount = count_dir_entries(".", false, false);
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
		list[i] = malloc(NAME_MAX + 1);
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

	err = scan_dir(file_list.contents, false, false);
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
	char *name, *buf;
	unsigned int buf_size;

	contents = file_list.contents;

	// file or directory name
	name = (char *)&contents->list[file_list.cur_idx]->name;

	if (is_directory(name)) {
		mvwprintw(status_win, 1, 5, "CHDIR  ");

		buf_size = strlen(name) + 1;
		buf = malloc(buf_size);
		if (!buf) {
			mvwprintw(status_win, 3, 5, "MALLOC ERROR");
			return (-1);
		}
		strncpy(buf, name, strlen(name));
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
	pthread_mutex_lock(&ui_status_cache_mutex);
	ui_status_cache = CMD_PLAY;
	pthread_mutex_unlock(&ui_status_cache_mutex);

	return (send_packet(sock_fd, cmd, name));
}


void
key_down()
{
	// DOWN - scroll files
	if (file_list.cur_idx < file_list.contents->amount - 1 &&
			file_list.cur_idx < file_list.tail_idx) {
		file_list.cur_idx += 1;
		show_files(main_win);
		return;
	}

	if (file_list.cur_idx < file_list.contents->amount - 1 &&
			file_list.cur_idx == file_list.tail_idx) {
		file_list.cur_idx += 1;
			file_list.head_idx += 1;
			file_list.tail_idx += 1;
			show_files(main_win);
	}
}

void
key_up()
{
	// UP - scroll files
	if (file_list.cur_idx > file_list.head_idx) {
		file_list.cur_idx -= 1;
		//file_list.tail_idx -= 1;
		show_files(main_win);
		return;
	}
	if (file_list.cur_idx == file_list.head_idx &&
			file_list.head_idx > 0) {
		file_list.head_idx -= 1;
		file_list.tail_idx -= 1;
		file_list.cur_idx -= 1;
		show_files(main_win);
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

void
set_main_window_size()
{
	int scr_height, scr_width;
	struct window_dimensions *dimensions;

	getmaxyx(stdscr, scr_height, scr_width);

	dimensions = main_win_dimensions;
	dimensions->width = scr_width - status_win_width;
	dimensions->height = scr_height;
	dimensions->startx = 0;
	dimensions->starty = 0;
}

void
set_status_window_size()
{
	int scr_height, scr_width;
	struct window_dimensions *dimensions;

	getmaxyx(stdscr, scr_height, scr_width);

	dimensions = status_win_dimensions;
	dimensions->width = status_win_width; //30
	dimensions->height = scr_height;
	dimensions->startx = scr_width - status_win_width;
	dimensions->starty = 0;
}

int
prepare_main_window()
{
	struct window_dimensions *dimensions;
	WINDOW *win;

	dimensions = malloc(sizeof (struct window_dimensions));
	if (!dimensions) {
		return (-1);
	}
	main_win_dimensions = dimensions;

	set_main_window_size();

	win = newwin(dimensions->height, dimensions->width,
		dimensions->starty, dimensions->startx);

	if (!win) {
		return (-1);
	}

	init_pair(1, COLOR_RED, COLOR_GREEN);
	attron(COLOR_PAIR(1));

	wbkgd(win, COLOR_PAIR(1));
	box(win, 0, 0);
	attroff(COLOR_PAIR(1));
	wrefresh(win);

	main_win = win;
	return (0);
}

int
prepare_status_window()
{
	struct window_dimensions *dimensions;
	WINDOW *win;

	dimensions = malloc(sizeof (struct window_dimensions));
	if (!dimensions) {
		return (-1);
	}
	status_win_dimensions = dimensions;

	set_status_window_size();

	win = newwin(dimensions->height, dimensions->width,
		dimensions->starty, dimensions->startx);

	if (!win) {
		return (-1);
	}

	init_pair(1, COLOR_RED, COLOR_GREEN);
	attron(COLOR_PAIR(1));

	wbkgd(win, COLOR_PAIR(1));
	box(win, 0, 0);
	attroff(COLOR_PAIR(1));
	wrefresh(win);

	status_win = win;
	return (0);
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
	pthread_mutex_lock(&ui_status_cache_mutex);
	ui_status_cache = STATUS_STOP;
	pthread_mutex_unlock(&ui_status_cache_mutex);
	show_status();
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
		case STATUS_EXIT:
			if (has_content)
				free(str_buf);
			return (NULL);
		default:
			;;
		}
		if (has_content) {
			free(str_buf);
			has_content = false;
		}
	}
	refresh();
	// we are here if error occurred
	if (has_content) {
		free(str_buf);
		has_content = false;
	}
	printw("ui_socket_receiver ERROR, exiting thread..\n");
	refresh();
	return (NULL);
}

void
show_status()
{
	info_t status;

	pthread_mutex_lock(&ui_status_cache_mutex);
	status = ui_status_cache;
	pthread_mutex_unlock(&ui_status_cache_mutex);

	switch (status) {
	case STATUS_STOP:
		mvwprintw(status_win, 1, 5, "STATUS_STOP");
		break;
	case CMD_PLAY:
		mvwprintw(status_win, 1, 5, "CMD_PLAY  ");
		break;
	default:
		;;
	}
	wrefresh(status_win);
}

void
resize_windows()
{
	struct window_dimensions *dimensions;

	set_status_window_size();
	dimensions = status_win_dimensions;
	wresize(status_win, dimensions->height, dimensions->width);
	mvwin(status_win, dimensions->starty, dimensions->startx);
	wclear(status_win);
	box(status_win, 0, 0);
	show_status();
	wrefresh(status_win);

	set_main_window_size();
	dimensions = main_win_dimensions;
	wresize(main_win, dimensions->height, dimensions->width);
	handle_resize(main_win);
	show_files(main_win);
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
		//case 410:
		case KEY_RESIZE:
			resize_windows();
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
handle_resize(WINDOW *w)
{
	unsigned int win_y, win_x, old_lines, new_lines, diff, files_amt;
	struct dir_contents *contents;

	contents = file_list.contents;
	files_amt = contents->amount;

	getmaxyx(w, win_y, win_x);

	old_lines = file_list.tail_idx - file_list.head_idx;
	new_lines = win_y - 3;

	mvwprintw(status_win, 3, 1, "new_lines %d   ", new_lines);
	mvwprintw(status_win, 4, 1, "old_lines %d   ", old_lines);
	mvwprintw(status_win, 5, 1, "head %d cur %d tail %d   ",
		file_list.head_idx, file_list.cur_idx, file_list.tail_idx);
	mvwprintw(status_win, 6, 1, "all files %d", files_amt);
	wrefresh(status_win);

	// TODO: refactor this function and remove dead code

	// screen bigger then files available
	if (new_lines >= files_amt) {
		file_list.head_idx = 0;
		file_list.tail_idx = files_amt - 1;
		mvwprintw(status_win, 0, 1, "CASE 3   ");
		wrefresh(status_win);
		return;
	}

	// bigger screen
	if (new_lines > old_lines) {
		diff = new_lines - old_lines;
		mvwprintw(status_win, 13, 20, "diff %d   ", diff);
		wrefresh(status_win);
		if (file_list.tail_idx + 1 + diff < files_amt) {
			file_list.tail_idx += diff;
			mvwprintw(status_win, 0, 1, "CASE 4-1  ");
			wrefresh(status_win);
		}
		// TODO: refactor this
		if (file_list.tail_idx + 1 + diff >= files_amt) {
			if (file_list.tail_idx - file_list.cur_idx < new_lines) {
				if (file_list.head_idx > 0) {
					file_list.head_idx -= diff;
				}
				mvwprintw(status_win, 0, 1, "CASE 4-2-1  ");
				wrefresh(status_win);
			}
		}
		return;
	}

	// smaller
	if (new_lines < old_lines) {
		diff = old_lines - new_lines;
		mvwprintw(status_win, 13, 20, "diff %d   ", diff);
		wrefresh(status_win);
		if (file_list.cur_idx - file_list.head_idx > new_lines) {
			file_list.tail_idx = file_list.cur_idx;
			file_list.head_idx = file_list.tail_idx - new_lines;
			mvwprintw(status_win, 0, 1, "CASE 5-1   ");
			wrefresh(status_win);
			return;
		}
		if (file_list.tail_idx - file_list.cur_idx < new_lines) {
			file_list.head_idx = file_list.tail_idx - new_lines;
			mvwprintw(status_win, 0, 1, "CASE 5-2   ");
			wrefresh(status_win);
			return;
		}

		if (file_list.cur_idx < new_lines) {
			file_list.tail_idx = file_list.head_idx + new_lines;
			mvwprintw(status_win, 0, 1, "CASE 5   ");
			wrefresh(status_win);
			return;
		}

		if (file_list.cur_idx >= new_lines) {
			file_list.tail_idx = file_list.cur_idx;
			file_list.head_idx = file_list.cur_idx - new_lines;
			mvwprintw(status_win, 0, 1, "CASE 5-3   ");
			wrefresh(status_win);
			return;
		}

		// TODO: probably dead code
		if (file_list.cur_idx + 1 == new_lines) {
			file_list.tail_idx = file_list.cur_idx + diff + 1;
			file_list.head_idx += diff;
			mvwprintw(status_win, 0, 1, "CASE 6   ");
			wrefresh(status_win);
		}
		// TODO: probably dead code
		if (file_list.cur_idx + diff == new_lines - 1) {
			file_list.tail_idx = file_list.cur_idx + diff + 1;
			mvwprintw(status_win, 0, 1, "CASE 7   ");
			wrefresh(status_win);
		}
	}
}

void
show_files(WINDOW *w)
{
	unsigned int idx, y_pos, win_y, win_x, files_amt;
	struct dir_contents *contents;

	contents = file_list.contents;
	files_amt = contents->amount;

	getmaxyx(w, win_y, win_x);

	wclear(w);
	box(w, 0, 0);

	// show directory name
	// TODO: cut path if too long
	mvwprintw(w, 0, 1, "%s", file_list.dir_name);

	idx = file_list.head_idx;

	y_pos = 1;

	for (; idx < contents->amount; idx++) {
		if (idx > file_list.tail_idx)
			break;
		// clear a line with spaces
		mvwprintw(w, y_pos, 1, "%*s", win_x - 2, " ");

		if (file_list.cur_idx == idx) {
			mvwprintw(w, y_pos, 1, "%s  <--", &contents->list[idx]->name);
		} else {
			mvwprintw(w, y_pos, 1, "%s", &contents->list[idx]->name);
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
	free(main_win_dimensions);

	delwin(status_win);
	free(status_win_dimensions);

	endwin();
}

int
ui_init()
{
	int err;

	if (initscr() == NULL) {
		return (-1);
	}

	noecho();

	// invisible cursor
	curs_set(0);

	err = prepare_main_window();
	if (err == -1) {
		return (-1);
	}

	err = prepare_status_window();
	if (err == -1) {
		return (-1);
	}

	return (0);
}


int
curses_ui()
{
	int err;

	err = ui_init();
	if (err == -1) {
		return (-1);
	}

	file_list.dir_name = malloc(MAXPATHLEN + 1);
	if (!file_list.dir_name) {
		mvwprintw(main_win, 0, 1, "ERROR: Can't initialize dir_name.");
		wrefresh(main_win);
		return (-1);
	}
	err = first_run_file_list(main_win);
	if (err == -1) {
		mvwprintw(main_win, 0, 1, "ERROR: Can't initialize file list.");
		wrefresh(main_win);
		return (-1);
	}

	sock_fd = get_client_socket();

	err = pthread_create(&receiver_thread, rcv_attr, ui_socket_receiver, rcv_arg);
	if (err != 0) {
		mvwprintw(main_win, 0, 1, "ERROR: receiver thread");
		wrefresh(main_win);
		return (-1);
	}

	show_files(main_win);
	curses_loop();

	// wait for receiver_thread if alive
	if (pthread_kill(receiver_thread, 0) == 0) {
		printw("UI: waiting for receiver_thread..");
		refresh();
		pthread_join(receiver_thread, NULL);
	}

	free_dir_list();
	free(file_list.dir_name);

	ui_cleanup();
	return (0);
}
