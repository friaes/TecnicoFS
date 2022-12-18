#include "operations.h"
#include "config.h"
#include "state.h"
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

#include "betterassert.h"

pthread_mutex_t mutex_ops = PTHREAD_MUTEX_INITIALIZER;

tfs_params tfs_default_params() {
    tfs_params params = {
        .max_inode_count = 64,
        .max_block_count = 1024,
        .max_open_files_count = 16,
        .block_size = 1024,
    };
    return params;
}

int tfs_init(tfs_params const *params_ptr) {
    tfs_params params;
    if (params_ptr != NULL) {
        params = *params_ptr;
    } else {
        params = tfs_default_params();
    }

    if (state_init(params) != 0) {
        return -1;
    }

    // create root inode
    int root = inode_create(T_DIRECTORY);
    if (root != ROOT_DIR_INUM) {
        return -1;
    }

    return 0;
}

int tfs_destroy() {
    if (state_destroy() != 0) {
        return -1;
    }
    return 0;
}

static bool valid_pathname(char const *name) {
    return name != NULL && strlen(name) > 1 && name[0] == '/';
}

/**
 * Looks for a file.
 *
 * Note: as a simplification, only a plain directory space (root directory only)
 * is supported.
 *
 * Input:
 *   - name: absolute path name
 *   - root_inode: the root directory inode
 * Returns the inumber of the file, -1 if unsuccessful.
 */
static int tfs_lookup(char const *name, inode_t const *root_inode) {
    // TODO: assert that root_inode is the root directory
    if (!valid_pathname(name)) {
        return -1;
    }

    // skip the initial '/' character
    name++;

    return find_in_dir(root_inode, name);
}

int tfs_open(char const *name, tfs_file_mode_t mode) {
    // Checks if the path name is valid
    if (!valid_pathname(name)) {
        return -1;
    }

    inode_t *root_dir_inode = inode_get(ROOT_DIR_INUM);
    ALWAYS_ASSERT(root_dir_inode != NULL,
                  "tfs_open: root dir inode must exist");

    // locking until we're sure the file exists
    if (pthread_mutex_lock(&mutex_ops) != 0)
        exit(EXIT_FAILURE);
    int inum = tfs_lookup(name, root_dir_inode);
    size_t offset;

    if (inum >= 0) {
        // The file already exists
        if (pthread_mutex_unlock(&mutex_ops) != 0)
            exit(EXIT_FAILURE);
        inode_t *inode = inode_get(inum);
        ALWAYS_ASSERT(inode != NULL,
                      "tfs_open: directory files must have an inode");

        if (inode->is_sym_link == true) {
            char *target_path = data_block_get(inode->i_data_block);
            // checks if the soft link's target file still exists
            if ((inum = tfs_lookup(target_path, root_dir_inode)) == -1)
                return -1;
            inode = inode_get(inum);
        }

        // Truncate (if requested)
        if (mode & TFS_O_TRUNC) {
            if (inode->i_size > 0) {
                data_block_free(inode->i_data_block);
                inode->i_size = 0;
            }
        }
        // Determine initial offset
        if (mode & TFS_O_APPEND) {
            offset = inode->i_size;
        } else {
            offset = 0;
        }
    } else if (mode & TFS_O_CREAT) {
        // The file does not exist; the mode specified that it should be created
        // Create inode
        inum = inode_create(T_FILE);
        if (inum == -1) {
            if (pthread_mutex_unlock(&mutex_ops) != 0)
                exit(EXIT_FAILURE);
            return -1; // no space in inode table
        }

        // Add entry in the root directory
        if (add_dir_entry(root_dir_inode, name + 1, inum) == -1) {
            inode_delete(inum);
            if (pthread_mutex_unlock(&mutex_ops) != 0)
                exit(EXIT_FAILURE);
            return -1; // no space in directory
        }

        // The file has already been created and added to the directory, so we
        // can safely unlock now
        if (pthread_mutex_unlock(&mutex_ops) != 0)
            exit(EXIT_FAILURE);
        offset = 0;
    } else {
        if (pthread_mutex_unlock(&mutex_ops) != 0)
            exit(EXIT_FAILURE);
        return -1;
    }

    // Finally, add entry to the open file table and return the corresponding
    // handle
    return add_to_open_file_table(inum, offset);

    // Note: for simplification, if file was created with TFS_O_CREAT and there
    // is an error adding an entry to the open file table, the file is not
    // opened but it remains created
}

int tfs_sym_link(char const *target, char const *link_name) {
    inode_t *root = inode_get(ROOT_DIR_INUM);

    // Verifies if the target exists
    int inum_target = tfs_lookup(target, root);
    if (inum_target == -1)
        return -1;
    
    // Creates the soft link if it doesn't exist
    int fhandle_link = tfs_open(link_name, TFS_O_CREAT);
    if (fhandle_link == -1)
        return -1; 
    tfs_close(fhandle_link);

    int inumber_link = tfs_lookup(link_name, root);
    inode_t *inode_link = inode_get(inumber_link);
    inode_link->i_data_block = data_block_alloc();

    char *data_block_link = data_block_get(inode_link->i_data_block);
    size_t path_length = strlen(target);

    if (path_length >= state_block_size())
        return -1;
    
    for (int i = 0; i < path_length; i++)
        data_block_link[i] = target[i];

    inode_link->is_sym_link = true;

    return 0;
}

int tfs_link(char const *target, char const *link_name) {
    inode_t *root = inode_get(ROOT_DIR_INUM);
    int inumber_target = tfs_lookup(target, root);
    
    // Verifies if the target exists
    if (inumber_target == -1)
        return -1;

    inode_t* inode_target = inode_get(inumber_target);

    // Cannot create hard link for soft link
    if (inode_target->is_sym_link == true)
        return -1;

    if (add_dir_entry(root, link_name + 1, inumber_target) == -1)
        return -1;
    
    // Increases the target's hard link counter
    inode_target->hl_count++;

    return 0;
}

int tfs_close(int fhandle) {
    open_file_entry_t *file = get_open_file_entry(fhandle);
    if (file == NULL) {
        return -1; // invalid fd
    }

    remove_from_open_file_table(fhandle);

    return 0;
}

ssize_t tfs_write(int fhandle, void const *buffer, size_t to_write) {
    open_file_entry_t *file = get_open_file_entry(fhandle);
    if (file == NULL) {
        return -1;
    }

    //  From the open file table entry, we get the inode
    inode_t *inode = inode_get(file->of_inumber);
    ALWAYS_ASSERT(inode != NULL, "tfs_write: inode of open file deleted");

    // Determine how many bytes to write
    size_t block_size = state_block_size();

    pthread_rwlock_rdlock(&file->rwl);
    if (to_write + file->of_offset > block_size) {
        to_write = block_size - file->of_offset;
    }
    pthread_rwlock_unlock(&file->rwl);

    pthread_rwlock_rdlock(&inode->rwl);
    if (to_write > 0 && inode->is_sym_link == false) {
        if (inode->i_size == 0) {
            pthread_rwlock_unlock(&file->rwl);
            // If empty file, allocate new block
            int bnum = data_block_alloc();
            if (bnum == -1) {
                return -1; // no space
            }
            
            inode->i_data_block = bnum;
        }

        pthread_rwlock_rdlock(&inode->rwl);
        void *block = data_block_get(inode->i_data_block);
        pthread_rwlock_unlock(&inode->rwl);
        ALWAYS_ASSERT(block != NULL, "tfs_write: data block deleted mid-write");

        pthread_rwlock_rdlock(&file->rwl);
        // Perform the actual write
        memcpy(block + file->of_offset, buffer, to_write);
        pthread_rwlock_unlock(&file->rwl);

        pthread_rwlock_rdlock(&file->rwl);
        // The offset associated with the file handle is incremented accordingly
        file->of_offset += to_write;
        if (file->of_offset > inode->i_size) {
            inode->i_size = file->of_offset;
        }
    }
    pthread_rwlock_unlock(&inode->rwl);
    pthread_rwlock_unlock(&file->rwl);

    return (ssize_t)to_write;
}

ssize_t tfs_read(int fhandle, void *buffer, size_t len) {
    open_file_entry_t *file = get_open_file_entry(fhandle);
    if (file == NULL) {
        return -1;
    }

    // From the open file table entry, we get the inode
    inode_t const *inode = inode_get(file->of_inumber);
    ALWAYS_ASSERT(inode != NULL, "tfs_read: inode of open file deleted");

    // Determine how many bytes to read
    size_t to_read = inode->i_size - file->of_offset;
    if (to_read > len) {
        to_read = len;
    }

    if (to_read > 0) {
        void *block = data_block_get(inode->i_data_block);
        ALWAYS_ASSERT(block != NULL, "tfs_read: data block deleted mid-read");

        // Perform the actual read
        memcpy(buffer, block + file->of_offset, to_read);
        // The offset associated with the file handle is incremented accordingly
        file->of_offset += to_read;
    }

    return (ssize_t)to_read;
}

int tfs_unlink(char const *target) {
    inode_t *root = inode_get(ROOT_DIR_INUM);
    int inumber_target = tfs_lookup(target, root);

    // Verifies if the target exists
    if (inumber_target == -1)
        return -1;
    
    inode_t* inode_target = inode_get(inumber_target);

    inode_target->hl_count -= 1;
    // Deletes the target's inode if hard link counter reaches 0
    if (inode_target->hl_count <= 0)
        inode_delete(inumber_target);

    if (clear_dir_entry(root, target + 1) == -1)
        return -1;

    return 0;
}

int tfs_copy_from_external_fs(char const *source_path, char const *dest_path) {
    FILE* fd_read = fopen(source_path, "r");
    if (fd_read == NULL) {
        return -1;
    }

    int fhandle_write = tfs_open(dest_path, TFS_O_CREAT | TFS_O_TRUNC);
    if (fhandle_write == -1) {
        return -1;
    }

    char buffer[state_block_size()];
    memset(buffer, 0, sizeof(buffer));

    // reads from the outside file and writes into the file inside TFS
    size_t bytes_read = fread(buffer, sizeof(char), strlen(buffer) + 1, fd_read);
    while (bytes_read > 0) {
        if (tfs_write(fhandle_write, buffer, strlen(buffer)) == -1) {
            tfs_close(fhandle_write);
            fclose(fd_read);
            return -1;
        }    
        memset(buffer, 0, sizeof(buffer));
        bytes_read = fread(buffer, sizeof(char), strlen(buffer) + 1, fd_read);
    }

    if (tfs_close(fhandle_write) == -1 || fclose(fd_read) != 0)
        return -1;
    
    return 0;
}
