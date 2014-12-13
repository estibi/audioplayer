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
	fileobj **list;
	unsigned int amount;
};


void show_dir_content(struct dir_contents *contents);
int scan_dir(struct dir_contents *contents);
bool is_directory(char *file);
int count_dir_entries(char *dir_path);
