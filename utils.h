#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <stdlib.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

#define	NAME_MAX 255

static const int supported_files_num = 5;
/* used by audio codecs - don't change order */
static const char *supported_files[] = {
	"aiff",
	"flac",
	"wav",
	"ogg",
	"mp3"	/* index == 4 */
};


typedef enum {
	F_UNDEFINED,
	F_NORMAL,
	F_DIR
} file_type_t;

typedef struct fileobj_t {
	unsigned short int type;
	char name[NAME_MAX];
} fileobj;

struct dir_contents {
	unsigned int amount;
	fileobj **list;
};


void show_dir_content(struct dir_contents *contents);
int scan_dir(struct dir_contents *contents, bool hidden, bool unsupported);
bool is_directory(char *name);
int count_dir_entries(char *dir_path, bool hidden, bool unsupported);
bool is_supported(char *name);
int get_file_type(char *filename);