#define	FILES_IN_DIR_LIMIT 20000
#define	NAME_MAX 255

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
	fileobj list[FILES_IN_DIR_LIMIT];
	unsigned int amount;
};


void show_dir_content(struct dir_contents *contents);
int scan_dir(char *dir_path, struct dir_contents *contents);
bool is_directory(char *file);
