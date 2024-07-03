#define FUSE_USE_VERSION 30
#include <fuse3/fuse.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <time.h>

// Define the superblock structure
typedef struct superblock {
    uint32_t fs_size;         // Size of the file system
    uint32_t block_size;      // Block size
    uint32_t free_blocks;     // Number of free blocks
    uint32_t total_inodes;    // Total number of inodes
    uint32_t free_inodes;     // Number of free inodes
    time_t mount_time;        // Last mount time
    time_t write_time;        // Last write time
    time_t last_check;        // Last file system check time
    uint32_t max_mount_count; // Maximum mount count before a check is needed
    uint32_t mount_count;     // Current mount count
} superblock_t;

// Define a simple inode structure
typedef struct inode {
    char name[256];          // Name of the file or directory
    mode_t mode;             // File mode (permissions)
    off_t size;              // Size of the file
    char *content;           // Content of the file (for files only)
    struct inode *next;      // Pointer to the next inode (for linked list)
    time_t atime;            // Last access time
    time_t mtime;            // Last modification time
    time_t ctime;            // Last status change time
} inode_t;

// Root directory inode
inode_t *root;
superblock_t superblock;

// Initialize the file system
void init_fs() {
    superblock.fs_size = 1024 * 1024 * 100; // 100 MB file system size
    superblock.block_size = 4096;           // 4 KB block size
    superblock.free_blocks = (superblock.fs_size / superblock.block_size) - 1;
    superblock.total_inodes = 1024;
    superblock.free_inodes = superblock.total_inodes - 1;
    superblock.mount_time = time(NULL);
    superblock.write_time = time(NULL);
    superblock.last_check = time(NULL);
    superblock.max_mount_count = 20;
    superblock.mount_count = 0;

    root = (inode_t *)malloc(sizeof(inode_t));
    if (root == NULL) {
        perror("Failed to allocate memory for root");
        exit(EXIT_FAILURE);
    }
    strcpy(root->name, "/");
    root->mode = S_IFDIR | 0755; // Directory with 755 permissions
    root->size = 0;
    root->content = NULL;
    root->next = NULL;
    root->atime = time(NULL);
    root->mtime = time(NULL);
    root->ctime = time(NULL);

    // Create a file in the root directory
    inode_t *file = (inode_t *)malloc(sizeof(inode_t));
    if (file == NULL) {
        perror("Failed to allocate memory for file");
        exit(EXIT_FAILURE);
    }
    strcpy(file->name, "hello.txt");
    file->mode = S_IFREG | 0644; // Regular file with 644 permissions
    file->size = 12;
    file->content = strdup("Hello World\n");
    file->next = NULL;
    file->atime = time(NULL);
    file->mtime = time(NULL);
    file->ctime = time(NULL);

    root->next = file;
}

// Implement FUSE operations
static int myfs_getattr(const char *path, struct stat *stbuf, struct fuse_file_info *fi) {
    inode_t *node = root;
    memset(stbuf, 0, sizeof(struct stat));
    
    while (node) {
        if (strcmp(path + 1, node->name) == 0) {
            stbuf->st_mode = node->mode;
            stbuf->st_nlink = (node->mode & S_IFDIR) ? 2 : 1;
            stbuf->st_size = node->size;
            stbuf->st_atime = node->atime;
            stbuf->st_mtime = node->mtime;
            stbuf->st_ctime = node->ctime;
            return 0;
        }
        node = node->next;
    }
    return -ENOENT;
}

static int myfs_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi, enum fuse_readdir_flags flags) {
    if (strcmp(path, "/") != 0)
        return -ENOENT;

    filler(buf, ".", NULL, 0, 0);
    filler(buf, "..", NULL, 0, 0);

    inode_t *node = root->next;
    while (node) {
        filler(buf, node->name, NULL, 0, 0);
        node = node->next;
    }
    return 0;
}

static int myfs_open(const char *path, struct fuse_file_info *fi) {
    inode_t *node = root->next;
    
    while (node) {
        if (strcmp(path + 1, node->name) == 0) {
            if ((fi->flags & O_ACCMODE) != O_RDONLY)
                return -EACCES;
            return 0;
        }
        node = node->next;
    }
    return -ENOENT;
}

static int myfs_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi) {
    inode_t *node = root->next;

    while (node) {
        if (strcmp(path + 1, node->name) == 0) {
            if (offset < node->size) {
                if (offset + size > node->size)
                    size = node->size - offset;
                memcpy(buf, node->content + offset, size);
            } else {
                size = 0;
            }
            return size;
        }
        node = node->next;
    }
    return -ENOENT;
}

// Create a new file
static int myfs_create(const char *path, mode_t mode, struct fuse_file_info *fi) {
    inode_t *node = root;
    
    // Check if file already exists
    while (node) {
        if (strcmp(path + 1, node->name) == 0) {
            return -EEXIST;
        }
        node = node->next;
    }
    
    // Create new inode for the file
    inode_t *new_file = (inode_t *)malloc(sizeof(inode_t));
    if (new_file == NULL) {
        return -ENOMEM;
    }
    
    strcpy(new_file->name, path + 1);
    new_file->mode = S_IFREG | mode;
    new_file->size = 0;
    new_file->content = NULL;
    new_file->next = root->next;
    new_file->atime = time(NULL);
    new_file->mtime = time(NULL);
    new_file->ctime = time(NULL);
    
    root->next = new_file;
    superblock.free_inodes--;
    return 0;
}

// Write to a file
static int myfs_write(const char *path, const char *buf, size_t size, off_t offset, struct fuse_file_info *fi) {
    inode_t *node = root->next;

    while (node) {
        if (strcmp(path + 1, node->name) == 0) {
            if (offset + size > node->size) {
                node->content = realloc(node->content, offset + size);
                node->size = offset + size;
            }
            memcpy(node->content + offset, buf, size);
            node->mtime = time(NULL);
            return size;
        }
        node = node->next;
    }
    return -ENOENT;
}

// Delete a file
static int myfs_unlink(const char *path) {
    inode_t *prev = root;
    inode_t *node = root->next;

    while (node) {
        if (strcmp(path + 1, node->name) == 0) {
            prev->next = node->next;
            free(node->content);
            free(node);
            superblock.free_inodes++;
            return 0;
        }
        prev = node;
        node = node->next;
    }
    return -ENOENT;
}

static struct fuse_operations myfs_oper = {
    .getattr    = myfs_getattr,
    .readdir    = myfs_readdir,
    .open       = myfs_open,
    .read       = myfs_read,
    .create     = myfs_create,
    .write      = myfs_write,
    .unlink     = myfs_unlink,
};

// Main function to run the file system
int main(int argc, char *argv[]) {
    init_fs();  // Initialize the file system

    superblock.mount_count++;
    superblock.mount_time = time(NULL);

    return fuse_main(argc, argv, &myfs_oper, NULL);
}
