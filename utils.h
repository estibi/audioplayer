#define	FILES_IN_DIR_LIMIT 20000
#define	NAME_MAX 255

typedef struct filename_t {
	char name[NAME_MAX];
} filename;

struct dir_contents {
	filename list[FILES_IN_DIR_LIMIT];
	unsigned int amount;
};


void show_dir_content(struct dir_contents *contents);
int scan_dir(char *dir_path, struct dir_contents *contents);
