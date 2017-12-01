#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#define MAX_INODE 4096
#define MAX_BLOCK 4096
#define MAX_BLOCKS_PER_FILE 1
#define MAX_FILENAME 252
#define MAX_DIRENTRY_PER_BLOCK 16
#define ERROR 0x7FFFFFFF
#define BUFFER_LEN 4096
const char *DATA_FILE = "data.dsk";

const int MODE_DIR = 1;
const int MODE_FILE = 2;

const int BLOCK_DATA = 1;
const int BLOCK_DIR_ENTRY = 2;

// 8 kb
struct super_block {
    uint8_t inode_bitmap[MAX_INODE];
    uint8_t block_bitmap[MAX_BLOCK];
};

// 32 bytes
struct inode {
    uint32_t mode;
    uint32_t file_size;
    uint8_t bitmap[16];
    uint32_t entry_count;
    uint32_t blocks[MAX_BLOCKS_PER_FILE];
};

struct entry {
    uint32_t id;
    char name[MAX_FILENAME];
};

union data {
    char data[4096];
    struct {
        struct entry entries[MAX_DIRENTRY_PER_BLOCK];
    };
};

struct file {
    struct super_block sb;
    struct inode nodes[4096];
    union data blocks[4096];
} *fp;

char cmd[BUFFER_LEN], buffer[BUFFER_LEN];
char *cur_cmd, *cmd_end;
uint32_t cur_depth = 0, temp_depth = 0;
uint32_t dir_inodes[256];
uint32_t temp_dir_inodes[256];

// Utility
uint32_t allocate_inode(uint32_t mode, uint8_t block) {
    for (uint32_t i = 0; i < MAX_INODE; i++) {
        if (fp->sb.inode_bitmap[i] == 0) {
            fp->sb.inode_bitmap[i] = 1;
            for (uint32_t j = 0; j < MAX_BLOCK; j++) {
                if (fp->sb.block_bitmap[j] == 0) {
                    fp->sb.block_bitmap[j] = block;
                    memset(fp->nodes[i].bitmap, 0, sizeof(fp->nodes[i].bitmap));
                    fp->nodes[i].blocks[0] = j;
                    fp->nodes[i].mode = mode;
                    return i;
                }
            }
            printf("ERR: No block left.\n");
            return ERROR;
        }
    }
    printf("ERR: No inode left.\n");
    return ERROR;
}


char *extract_argument() {
    while (*cur_cmd == '\0' && cur_cmd < cmd_end) cur_cmd++;
    if (cur_cmd == cmd_end)
        return NULL;
    char *result = cur_cmd;
    cur_cmd += strlen(cur_cmd);
    return result;
}

void remove_ending_slash(char *path) {
    size_t len = strlen(path);
    if (len > 1 && path[len - 1] == '/') {
        path[len - 1] = '\0';
    }
}

int check_filename_valid(char *path) {
    size_t len = strlen(path);
    if (len >= MAX_FILENAME - 1) {
        printf("ERR: Name length exceed limit.\n");
        return ERROR;
    } else if (len == 0) {
        printf("ERR: Name cannot be empty.\n");
        return ERROR;
    }
    for (size_t i = 0; i < len; i++) {
        if (!(isalnum(path[i]) || path[i] == '.' || path[i] == '_')) {
            printf("ERR: Name cannot contain invalid char.\n");
            return ERROR;
        }
    }
    if (strcmp(path, ".") == 0 || strcmp(path, "..") == 0) {
        printf("ERR: Name cannot be \"..\" or \".\".\n");
        return ERROR;
    }
    return 0;
}

void split_path(char **path, char **file_name) {
    char *rev_slash = strrchr(*path, '/');
    if (rev_slash == NULL) {
        *file_name = *path;
        *path += strlen(*path);
    } else {
        if (rev_slash == *path) {
            *file_name = *path + 1;
            *path = "/";
        } else {
            *file_name = rev_slash + 1;
            *rev_slash = '\0';
        }
    }
}

uint32_t find_path_inode(char *path) {
    size_t len = strlen(path);
    char *temp = (char *) malloc((len + 1) * sizeof(char));
    strcpy(temp, path);
    uint32_t cur_inode;
    memcpy(temp_dir_inodes, dir_inodes, sizeof(dir_inodes));
    if (len == 0) {
        temp_depth = cur_depth;
        free(temp);
        return temp_dir_inodes[temp_depth];
    }
    if (len > 0 && path[0] == '/') { // absolute
        cur_inode = temp_dir_inodes[0];
        temp_depth = 0;
    } else { // relative
        cur_inode = temp_dir_inodes[cur_depth];
        temp_depth = cur_depth;
    }
    char *name = strtok(temp, "/");
    while (name != NULL) {
        if (strcmp(name, "") == 0 || strcmp(name, ".") == 0) {
            goto next;
        } else if (strcmp(name, "..") == 0) {
            if (temp_depth == 0) {
                printf("ERR: Already at root.\n");
                free(temp);
                return ERROR;
            }
            cur_inode = temp_dir_inodes[--temp_depth];
            goto next;
        }
        uint32_t block;
        block = fp->nodes[cur_inode].blocks[0];
        int found = 0;
        for (int i = 0; i < MAX_DIRENTRY_PER_BLOCK; i++) {
            if (fp->nodes[cur_inode].bitmap[i] != 0) {
                uint32_t id = fp->blocks[block].entries[i].id;
                if (strcmp(name, fp->blocks[block].entries[i].name) == 0) {
                    found = 1;
                    cur_inode = id;
                    temp_dir_inodes[++temp_depth] = cur_inode;
                    break;
                }
            }
        }
        if (!found) {
            free(temp);
            printf("ERR: Path not found.\n");
            return ERROR;
        } else if (fp->nodes[cur_inode].mode != MODE_DIR) {
            printf("ERR: Bad path.\n");
            return ERROR;
        }
        next:
        name = strtok(NULL, "/");
    }
    free(temp);
    return cur_inode;
}

void pwd(int output) {
    uint32_t i, block;
    if (cur_depth > 0) {
        buffer[0] = '\0';
        for (i = 0; i < cur_depth; i++) {
            strcat(buffer, "/");

            block = fp->nodes[dir_inodes[i]].blocks[0];
            for (int j = 0; j < MAX_DIRENTRY_PER_BLOCK; j++) {
                if (fp->nodes[dir_inodes[i]].bitmap[j] != 0) {
                    if (dir_inodes[i + 1] == fp->blocks[block].entries[j].id) {
                        strcat(buffer, fp->blocks[block].entries[j].name);
                        break;
                    }
                }
            }
        }
    } else {
        strcpy(buffer, "/");
    }
    if (output) {
        printf("%s\n", buffer);
    }
}

void format() {
    printf("Formatting disk...\n");
    memset(fp, 0, sizeof(struct file));
    cur_depth = 0;
    uint32_t root_inode = allocate_inode(MODE_DIR, BLOCK_DIR_ENTRY);
    dir_inodes[cur_depth] = root_inode;
    printf("Formatting done...\n");
}

void read_fs() {
    printf("Reading fs from %s ...\n", DATA_FILE);
    FILE *FP = fopen(DATA_FILE, "rb");
    if (FP == NULL) {
        printf("File not found -- creating a new disk.\n");
        format();
    } else {
        fread(fp, sizeof(struct file), 1, FP);
        fclose(FP);
        printf("Reading done.\n");
    }
}

void write_fs() {
    printf("Now saving data to disk..\n");
    FILE *FP = fopen(DATA_FILE, "wb");
    if (FP == NULL) {
        fprintf(stderr, "Open %s failed. Will lose all changes.\n", DATA_FILE);
    } else {
        fwrite(fp, sizeof(struct file), 1, FP);
        fclose(FP);
        printf("Saving done.\n");
    }
}

void cd() {
    char *path;
    temp_depth = cur_depth;
    memcpy(temp_dir_inodes, dir_inodes, sizeof(dir_inodes));
    path = extract_argument();
    if (path == NULL) {
        printf("ERR: Path cannot be empty.\n");
        return;
    }

    if (find_path_inode(path) == ERROR) {
        return;
    }
    cur_depth = temp_depth;
    memcpy(dir_inodes, temp_dir_inodes, sizeof(dir_inodes));
}

void ls() {
    char *path;
    temp_depth = cur_depth;
    memcpy(temp_dir_inodes, dir_inodes, sizeof(dir_inodes));
    path = extract_argument();
    if (path != NULL && find_path_inode(path) == ERROR) {
        return;
    }

    if (temp_depth > 0) {
        printf("../\n");
    }
    printf("./\n");
    uint32_t block;
    block = fp->nodes[temp_dir_inodes[temp_depth]].blocks[0];
    for (int i = 0; i < MAX_DIRENTRY_PER_BLOCK; i++) {
        if (fp->nodes[temp_dir_inodes[temp_depth]].bitmap[i] != 0) {
            uint32_t id = fp->blocks[block].entries[i].id;
            if (fp->nodes[id].mode == MODE_DIR) {
                printf("%s/\n", fp->blocks[block].entries[i].name);
            } else if (fp->nodes[id].mode == MODE_FILE) {
                printf("%s\n", fp->blocks[block].entries[i].name);
            }
        }
    }
}

void mkdir() {
    char *path = extract_argument();
    if (path == NULL) {
        printf("ERR: Path cannot be empty.\n");
        return;
    }
    if (strcmp(path, "/") == 0) {
        printf("ERR: Cannot mkdir root.\n");
        return;
    }

    remove_ending_slash(path);
    temp_depth = cur_depth;
    memcpy(temp_dir_inodes, dir_inodes, sizeof(dir_inodes));
    char *file_name;
    split_path(&path, &file_name);
    if (find_path_inode(path) == ERROR || check_filename_valid(file_name) == ERROR)
        return;

    uint32_t id = temp_dir_inodes[temp_depth];
    uint32_t block;
    block = fp->nodes[id].blocks[0];
    for (int i = 0; i < MAX_DIRENTRY_PER_BLOCK; i++) {
        if (fp->nodes[id].bitmap[i] != 0) {
            if (strcmp(file_name, fp->blocks[block].entries[i].name) == 0) {
                printf("ERR: Name already occupied.\n");
                return;
            }
        }
    }
    uint32_t new_inode = allocate_inode(MODE_DIR, BLOCK_DIR_ENTRY);
    if (new_inode != ERROR) {
        for (int i = 0; i < MAX_DIRENTRY_PER_BLOCK; i++) {
            if (fp->nodes[id].bitmap[i] == 0) {
                fp->nodes[id].entry_count++;
                fp->nodes[id].bitmap[i] = 1;
                strcpy(fp->blocks[block].entries[i].name, file_name);
                fp->blocks[block].entries[i].id = new_inode;
                return ;
            }
        }
        printf("ERR: Dir entry full.\n");
    }
}

void rmdir_recursively(uint32_t id) {
    uint32_t block;
    block = fp->nodes[id].blocks[0];
    for (int i = 0; i < MAX_DIRENTRY_PER_BLOCK; i++) {
        if (fp->nodes[id].bitmap[i] != 0) {
            uint32_t cur_id = fp->blocks[block].entries[i].id;
            if (fp->nodes[cur_id].mode == MODE_DIR) {
                rmdir_recursively(cur_id);
            }
            fp->nodes[id].entry_count--;
            fp->nodes[id].bitmap[i] = 0;
            fp->sb.block_bitmap[fp->nodes[cur_id].blocks[0]] = 0;
            fp->sb.inode_bitmap[cur_id] = 0;
        }
    }
}

void rmdir() {
    char *path = extract_argument(), *file_name;
    if (strcmp(path, "/") == 0) {
        format();
        return;
    }

    remove_ending_slash(path);
    temp_depth = cur_depth;
    memcpy(temp_dir_inodes, dir_inodes, sizeof(dir_inodes));
    split_path(&path, &file_name);
    if (find_path_inode(path) == ERROR || check_filename_valid(file_name) == ERROR) {
        return;
    }

    uint32_t id = temp_dir_inodes[temp_depth];

    uint32_t block;
    block = fp->nodes[id].blocks[0];
    for (int i = 0; i < MAX_DIRENTRY_PER_BLOCK; i++) {
        if (fp->nodes[id].bitmap[i] != 0) {
            if (strcmp(file_name, fp->blocks[block].entries[i].name) == 0) {
                uint32_t cur_id = fp->blocks[block].entries[i].id;
                if (fp->nodes[cur_id].mode == MODE_FILE) {
                    printf("ERR: Rmdir cannot remove file.\n");
                } else if (fp->nodes[cur_id].mode == MODE_DIR) {
                    rmdir_recursively(cur_id);
                    fp->nodes[id].entry_count--;
                    fp->nodes[id].bitmap[i] = 0;
                    if (cur_id != dir_inodes[0]) { // not root
                        fp->sb.block_bitmap[fp->nodes[cur_id].blocks[0]] = 0;
                        fp->sb.inode_bitmap[cur_id] = 0;
                    }
                }
                while (fp->sb.inode_bitmap[dir_inodes[cur_depth]] == 0) {
                    cur_depth--;
                }
                printf("Changing dir to:");
                pwd(1);
                return;
            }
        }
    }
    printf("ERR: Dir not found\n");
}

void dump_inode() {
    for (int i = 0; i < MAX_INODE; i++) {
        if (fp->sb.inode_bitmap[i] == 0)
            continue;
        if (fp->nodes[i].mode == MODE_DIR) {
            printf("Inode #%d: dir\n", i);
            uint32_t block = fp->nodes[i].blocks[0];
            printf("Block #%d:\n", block);
            for (int j = 0; j < MAX_DIRENTRY_PER_BLOCK; j++) {
                if (fp->nodes[i].bitmap[j] != 0) {
                    printf("Item #%d: Id: %d Name: %s\n", j, fp->blocks[block].entries[j].id,
                           fp->blocks[block].entries[j].name);
                }
            }
        } else if (fp->nodes[i].mode == MODE_FILE) {
            printf("Inode #%d: file\n", i);
            uint32_t block = fp->nodes[i].blocks[0];
            printf("Block: %d Content: %s\n", fp->nodes[i].blocks[0], fp->blocks[block].data);
        }
    }
}

void echo() {
    char *str = extract_argument();
    char *path = extract_argument(), *file_name;
    if (str == NULL || path == NULL) {
        printf("ERR: Please input str and path.\n");
        return;
    }

    temp_depth = cur_depth;
    memcpy(temp_dir_inodes, dir_inodes, sizeof(dir_inodes));
    split_path(&path, &file_name);
    if (find_path_inode(path) == ERROR || check_filename_valid(file_name) == ERROR)
        return;

    uint32_t id = temp_dir_inodes[temp_depth];
    uint32_t block;
    block = fp->nodes[id].blocks[0];
    for (int i = 0; i < MAX_DIRENTRY_PER_BLOCK; i++) {
        if (fp->nodes[id].bitmap[i] != 0) {
            if (strcmp(file_name, fp->blocks[block].entries[i].name) == 0) {
                printf("ERR: Name already occupied.\n");
                return;
            }
        }
    }
    uint32_t new_inode = allocate_inode(MODE_FILE, BLOCK_DATA);
    if (new_inode != ERROR) {
        for (int i = 0; i < MAX_DIRENTRY_PER_BLOCK; i++) {
            if (fp->nodes[id].bitmap[i] == 0) {
                fp->nodes[id].entry_count++;
                fp->nodes[id].bitmap[i] = 1;
                strcpy(fp->blocks[block].entries[i].name, file_name);
                fp->blocks[block].entries[i].id = new_inode;
                uint32_t len = (uint32_t) strlen(str);
                fp->nodes[new_inode].file_size = len;
                memcpy(fp->blocks[fp->nodes[new_inode].blocks[0]].data, str, len);
                return ;
            }
        }
        printf("ERR: Dir entry full.\n");
    }
}

void cat() {
    char *path = extract_argument(), *file_name;
    if (path == NULL) {
        printf("ERR: Please specify file path.\n");
        return;
    }

    temp_depth = cur_depth;
    memcpy(temp_dir_inodes, dir_inodes, sizeof(dir_inodes));
    split_path(&path, &file_name);
    if (find_path_inode(path) == ERROR || check_filename_valid(file_name) == ERROR)
        return;

    uint32_t id = temp_dir_inodes[temp_depth];
    uint32_t block;
    block = fp->nodes[id].blocks[0];
    for (int i = 0; i < MAX_DIRENTRY_PER_BLOCK; i++) {
        if (fp->nodes[id].bitmap[i] != 0) {
            if (strcmp(file_name, fp->blocks[block].entries[i].name) == 0) {
                uint32_t index = fp->blocks[block].entries[i].id;
                if (fp->nodes[index].mode == MODE_DIR) {
                    printf("ERR: Cannot cat a dir.\n");
                    return;
                }
                memcpy(buffer, fp->blocks[fp->nodes[index].blocks[0]].data, fp->nodes[index].file_size);
                buffer[fp->nodes[index].file_size] = '\0';
                printf("%s\n", buffer);
                return;
            }
        }
    }
    printf("ERR: File not found.\n");
}

void rm() {
    char *path = extract_argument(), *file_name;
    if (path == NULL) {
        printf("ERR: Please specify file path.\n");
        return;
    }

    size_t len = strlen(path);
    if (len > 0 && path[len - 1] == '/') {
        printf("ERR: Use rmdir to remove dir.\n");
        return;
    }
    temp_depth = cur_depth;
    memcpy(temp_dir_inodes, dir_inodes, sizeof(dir_inodes));
    split_path(&path, &file_name);
    if (find_path_inode(path) == ERROR || check_filename_valid(file_name) == ERROR)
        return;

    uint32_t id = temp_dir_inodes[temp_depth];
    uint32_t block;
    block = fp->nodes[id].blocks[0];
    for (int i = 0; i < MAX_DIRENTRY_PER_BLOCK; i++) {
        if (fp->nodes[id].bitmap[i] != 0) {
            if (strcmp(file_name, fp->blocks[block].entries[i].name) == 0) {
                uint32_t index = fp->blocks[block].entries[i].id;
                if (fp->nodes[index].mode == MODE_DIR) {
                    printf("ERR: Use mkdir to remove dir.\n");
                    return;
                } else {
                    fp->nodes[id].entry_count--;
                    fp->nodes[id].bitmap[i] = 0;
                    printf("File removed.\n");
                }
                return;
            }
        }
    }
    printf("ERR: File not found.\n");
}

void usage() {
    printf("extfs: A persistent in-memory fs.\n"
                   "commands:\n"
                   "\tq: quit extfs.\n"
                   "\tread: read from %s.\n"
                   "\twrite: write to %s.\n"
                   "\tpwd: print working directory.\n"
                   "\tcd: change directory.\n"
                   "\tmkdir: make directory.\n"
                   "\tls: list directory.\n"
                   "\techo: write to file.\n"
                   "\tcat: show file.\n"
                   "\trm: remove file.\n"
                   "\tfmt: format disk.\n"
                   "\tdmp: dump internal presentation.\n",
           DATA_FILE, DATA_FILE);
}

int run_command() {
    char *f = extract_argument();
    if (strcmp(f, "q") == 0) {
        printf("Now quitting...\n");
        return 1;
    } else if (strcmp(f, "read") == 0) {
        read_fs();
    } else if (strcmp(f, "write") == 0) {
        write_fs();
    } else if (strcmp(f, "pwd") == 0) {
        pwd(1);
    } else if (strcmp(f, "cd") == 0) {
        cd();
    } else if (strcmp(f, "mkdir") == 0) {
        mkdir();
    } else if (strcmp(f, "ls") == 0) {
        ls();
    } else if (strcmp(f, "rmdir") == 0) {
        rmdir();
    } else if (strcmp(f, "echo") == 0) {
        echo();
    } else if (strcmp(f, "cat") == 0) {
        cat();
    } else if (strcmp(f, "rm") == 0) {
        rm();
    } else if (strcmp(f, "fmt") == 0) {
        format();
    } else if (strcmp(f, "dmp") == 0) {
        dump_inode();
    } else {
        usage();
    }
    return 0;
}

int main() {
    fp = (struct file *) malloc(sizeof(struct file));
    read_fs();
    printf(">> ");
    fflush(stdout);
    while (fgets(cmd, BUFFER_LEN, stdin) != NULL) {
        size_t len = strlen(cmd);
        if (len > 0 && cmd[len - 1] == '\n') {
            cmd[--len] = '\0';
        }
        cur_cmd = cmd;
        cmd_end = cmd + len;
        char *p = cur_cmd;
        for (; p < cmd_end; p++) {
            if (*p == ' ') {
                *p = '\0';
            } else if (*p == '"') {
                *(p++) = '\0';
                while (p < cmd_end && *p != '"') p++;
                if (p == cmd_end) {
                    printf("ERR: Quotes not balanced.\n");
                    goto next;
                }
                *p = '\0';
            }
        }

        if (run_command() == 1)
            break;

        next:
        printf(">> ");
        fflush(stdout);
    }
    write_fs();
}
