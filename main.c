#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const int MAX_INODE = 4096;
const int MAX_BLOCK = 4096;
const int MAX_BLOCKS_PER_FILE = 1;
const int MAX_FILENAME = 252;
const int MAX_DIRENTRY_PER_BLOCK = 16;
const uint32_t ERROR = 0xFFFFFFFF;

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
    uint8_t data[4096];
    struct {
        struct entry entries[MAX_DIRENTRY_PER_BLOCK];
    };
};

struct file {
    struct super_block sb;
    struct inode nodes[4096];
    union data blocks[4096];
} *fp;

char cmd[256], buffer[2048];
uint32_t cur_depth = 0, temp_depth = 0;
uint32_t dir_inodes[256];
uint32_t temp_dir_inodes[256];

uint32_t allocate_data_block() {
    for (uint32_t i = 0; i < MAX_BLOCK; i++) {
        if (fp->sb.block_bitmap[i] == 0) {
            fp->sb.block_bitmap[i] = BLOCK_DATA;
            return i;
        }
    }
    return ERROR;
}

uint32_t allocate_dir_inode() {
    for (uint32_t i = 0; i < MAX_INODE; i++) {
        if (fp->sb.inode_bitmap[i] == 0) {
            fp->sb.inode_bitmap[i] = 1;
            for (uint32_t j = 0; j < MAX_BLOCK; j++) {
                if (fp->sb.block_bitmap[j] == 0) {
                    fp->sb.block_bitmap[j] = BLOCK_DIR_ENTRY;
                    memset(fp->nodes[i].bitmap, 0, sizeof(fp->nodes[i].bitmap));
                    fp->nodes[i].blocks[0] = j;
                    fp->nodes[i].mode = MODE_DIR;
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

void format() {
    printf("Formatting disk...\n");
    memset(fp, 0, sizeof(struct file));
    cur_depth = 0;
    uint32_t root_inode = allocate_dir_inode();
    dir_inodes[cur_depth] = root_inode;
    printf("Formatting done...\n");
}

void read_fs() {
    printf("Reading fs from data.dsk ...\n");
    FILE *FP = fopen("data.dsk", "rb");
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
    FILE *FP = fopen("data.dsk", "wb");
    if (FP == NULL) {
        fprintf(stderr, "Open data.dsk failed. Will lose all changes.\n");
    } else {
        fwrite(fp, sizeof(struct file), 1, FP);
        fclose(FP);
        printf("Saving done.\n");
    }
}

void pwd(int output) {
    uint32_t i, block, index;
    if (cur_depth > 0) {
        buffer[0] = '\0';
        for (i = 0; i < cur_depth; i++) {
            strcat(buffer, "/");

            block = fp->nodes[dir_inodes[i]].blocks[0];
            for (int j = 0; j < MAX_DIRENTRY_PER_BLOCK; j++) {
                if (fp->nodes[dir_inodes[i]].bitmap[j] != 0) {
                    uint32_t id = fp->blocks[block].entries[j].id;
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

/*
void cd() {
    uint32_t block;
    struct entry *dir;
    block = fp->nodes[dir_inodes[cur_depth]].blocks[0];
    dir = &fp->blocks[block].entries[0];
    if (strtok(cmd, " ") != NULL) {
        char *name = strtok(NULL, " ");
        if (name == NULL) {
            printf("ERR: Dir name cannot be empty.\n");
            return;
        }
        if (strcmp(name, "/") == 0) {
            cur_depth = 0;
            return ;
        }
        if (strcmp(name, ".") == 0) {
            return;
        } else if (strcmp(name, "..") == 0) {
            if (cur_depth == 0) {
                printf("ERR: Already at root.\n");
            } else {
                cur_depth--;
            }
        } else {
            int found = 0;
            for (int i = 0; i < MAX_DIRENTRY_PER_BLOCK; i++) {
                if (fp->nodes[dir_inodes[cur_depth]].bitmap[i] != 0) {
                    if (strcmp(name, fp->blocks[block].entries[i].name) == 0) {
                        uint32_t id = fp->blocks[block].entries[i].id;
                        if (fp->nodes[id].mode == MODE_DIR) {
                            dir_inodes[++cur_depth] = id;
                        } else if (fp->nodes[id].mode == MODE_FILE) {
                            printf("ERR: %s is a file, not a directory.\n", name);
                        }
                        found = 1;
                    }
                }
            }
            if (found == 0)
                printf("ERR: %s not found.\n", name);
        }
    }
}*/

uint32_t find_path_inode(char *path) {
    size_t len = strlen(path);
    char *temp = (char *) malloc((len + 1) * sizeof(char));
    strcpy(temp, path);
    uint32_t cur_inode;
    memcpy(temp_dir_inodes, dir_inodes, sizeof(dir_inodes));
    if (len > 0 && path[0] == '/') {
        cur_inode = temp_dir_inodes[0];
        temp_depth = 0;
    } else {
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
            return ERROR;
        }
        next:
        name = strtok(NULL, "/");
    }
    free(temp);
    return cur_inode;
}

void cd() {
    char *path;
    temp_depth = cur_depth;
    memcpy(temp_dir_inodes, dir_inodes, sizeof(dir_inodes));
    if (strtok(cmd, " ") != NULL) {
        path = strtok(NULL, " ");
        if (path != NULL && find_path_inode(path) == ERROR) {
            return;
        }
        cur_depth = temp_depth;
        memcpy(dir_inodes, temp_dir_inodes, sizeof(dir_inodes));
    }
}

void ls() {
    char *path;
    temp_depth = cur_depth;
    memcpy(temp_dir_inodes, dir_inodes, sizeof(dir_inodes));
    if (strtok(cmd, " ") != NULL) {
        path = strtok(NULL, " ");
        if (path != NULL && find_path_inode(path) == ERROR) {
            return;
        }
    }
    if (temp_depth > 0) {
        printf("../\n");
    }
    printf(".\n");
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
    if (strtok(cmd, " ") != NULL) {
        char *path = strtok(NULL, " ");
        if (path == NULL) {
            printf("ERR: Please specify dir name.\n");
            return;
        }
        if (strcmp(path, "/") == 0) {
            printf("ERR: Cannot mkdir root.\n");
            return ;
        }

        size_t len = strlen(path);
        if (len > 0 && path[len-1] == '/') {
            path[len-1] = '\0';
        }
        temp_depth = cur_depth;
        memcpy(temp_dir_inodes, dir_inodes, sizeof(dir_inodes));
        if (strchr(path, '/') != NULL) {
            char *last_sep = strrchr(path, '/');
            *last_sep = '\0';
            uint32_t result = 0;
            if (last_sep == path) { // root
                result = find_path_inode("/");
            } else {
                result = find_path_inode(path);
            }
            if (result == ERROR) {
                return;
            }
            path = last_sep+1;
        }
        if (strlen(path) >= 252 - 1) {
            printf("ERR: Dir name length exceed limit.\n");
            return;
        }
        uint32_t id = temp_dir_inodes[temp_depth];

        uint32_t block;
        block = fp->nodes[id].blocks[0];
        for (int i = 0; i < MAX_DIRENTRY_PER_BLOCK; i++) {
            if (fp->nodes[id].bitmap[i] != 0) {
                if (strcmp(path, fp->blocks[block].entries[i].name) == 0) {
                    printf("ERR: Name already occupied.\n");
                    return;
                }
            }
        }
        uint32_t new_inode = allocate_dir_inode();
        if (new_inode != ERROR) {
            for (int i = 0; i < MAX_DIRENTRY_PER_BLOCK; i++) {
                if (fp->nodes[id].bitmap[i] == 0) {
                    fp->nodes[id].entry_count++;
                    fp->nodes[id].bitmap[i] = 1;
                    fp->nodes[new_inode].mode = MODE_DIR;
                    strcpy(fp->blocks[block].entries[i].name, path);
                    fp->blocks[block].entries[i].id = new_inode;
                    break;
                }
            }
        }
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
                fp->nodes[id].entry_count--;
                fp->nodes[id].bitmap[i] = 0;
            }
            fp->sb.block_bitmap[fp->nodes[cur_id].blocks[0]] = 0;
            fp->sb.inode_bitmap[cur_id] = 0;
        }
    }
}

void rmdir() {
    if (strtok(cmd, " ") != NULL) {
        char *path = strtok(NULL, " ");
        if (path == NULL) {
            printf("ERR: Please specify dir name.\n");
            return;
        }

        if (strcmp(path, "/") == 0) {
            format();
            return ;
        }

        size_t len = strlen(path);
        if (len > 0 && path[len-1] == '/') {
            path[len-1] = '\0';
        }

        temp_depth = cur_depth;
        memcpy(temp_dir_inodes, dir_inodes, sizeof(dir_inodes));
        if (strchr(path, '/') != NULL) {
            char *last_sep = strrchr(path, '/');
            *last_sep = '\0';
            uint32_t result = 0;
            if (last_sep == path) { // root
                result = find_path_inode("/");
            } else {
                result = find_path_inode(path);
            }
            if (result == ERROR) {
                return;
            }
            path = last_sep+1;
        }
        if (strlen(path) >= 252 - 1) {
            printf("ERR: Dir name length exceed limit.\n");
            return;
        }
        uint32_t id = temp_dir_inodes[temp_depth];

        uint32_t block;
        block = fp->nodes[id].blocks[0];
        for (int i = 0; i < MAX_DIRENTRY_PER_BLOCK; i++) {
            if (fp->nodes[id].bitmap[i] != 0) {
                if (strcmp(path, fp->blocks[block].entries[i].name) == 0) {
                    uint32_t cur_id = fp->blocks[block].entries[i].id;
                    if (fp->nodes[cur_id].mode == MODE_FILE) {
                        printf("ERR: Rmdir cannot remove file.\n");
                    } else if (fp->nodes[cur_id].mode == MODE_DIR) {
                        rmdir_recursively(cur_id);
                        fp->nodes[id].entry_count--;
                        fp->nodes[id].bitmap[i] = 0;
                        if (cur_id != dir_inodes[0])  { // not root
                            fp->sb.block_bitmap[fp->nodes[cur_id].blocks[0]] = 0;
                            fp->sb.inode_bitmap[cur_id] = 0;
                        }
                    }
                    while(fp->sb.inode_bitmap[dir_inodes[cur_depth]] == 0)  {
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
}

void dump_inode() {
    for (int i = 0; i < MAX_INODE; i++) {
        if (fp->sb.inode_bitmap[i] != 0 && fp->nodes[i].mode == MODE_DIR) {
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
            printf("Block: %d\n", fp->nodes[i].blocks[0]);
        }
    }
}

int run_command() {
    if (strcmp(cmd, "q") == 0) {
        printf("Now quitting...\n");
        return 1;
    } else if (strcmp(cmd, "read") == 0) {
        read_fs();
    } else if (strcmp(cmd, "write") == 0) {
        write_fs();
    } else if (strcmp(cmd, "pwd") == 0) {
        pwd(1);
    } else if (strncmp(cmd, "cd", 2) == 0) {
        cd();
    } else if (strncmp(cmd, "mkdir", 5) == 0) {
        mkdir();
    } else if (strncmp(cmd, "ls", 2) == 0) {
        ls();
    } else if (strncmp(cmd, "rmdir", 5) == 0) {
        rmdir();
    } else if (strncmp(cmd, "echo", 4) == 0) {

    } else if (strncmp(cmd, "cat", 3) == 0) {

    } else if (strncmp(cmd, "rm", 2) == 0) {

    } else if (strcmp(cmd, "fmt") == 0) {
        format();
    } else if (strcmp(cmd, "dmp") == 0) {
        dump_inode();
    }
    return 0;
}

int main() {
    fp = (struct file *) malloc(sizeof(struct file));
    read_fs();
    printf(">> ");
    while (fgets(cmd, 256, stdin) != NULL) {
        size_t len = strlen(cmd);
        if (len > 0 && cmd[len - 1] == '\n') {
            cmd[--len] = '\0';
        }
        if (run_command() == 1)
            break;
        printf(">> ");
    }
    write_fs();
}