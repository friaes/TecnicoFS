#include "fs/operations.h"
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

uint8_t const file_contents[] = "ABC";
const char *file_path = "/f";
const char *hard_link_path = "/hl";
const char *soft_link_path = "/sl";

void assert_empty_file(const char *path) {
    int f = tfs_open(path, 0);
    assert(f != -1);

    uint8_t buffer[sizeof(file_contents)];
    assert(tfs_read(f, buffer, sizeof(buffer)) == 0);

    assert(tfs_close(f) != -1);
}

int main() {

    // init TécnicoFS
    assert(tfs_init(NULL) != -1);

    // Create file
    int f = tfs_open(file_path, TFS_O_CREAT);
    assert(f != -1);
    assert(tfs_close(f) != -1);

    // Create soft link on a file
    assert(tfs_sym_link(file_path, soft_link_path) != -1);
    assert_empty_file(soft_link_path);

    // Try to create hard link on a soft link
    assert(tfs_link(soft_link_path, hard_link_path) == -1);

    // Write to soft link
    f = tfs_open(soft_link_path, 0);
    assert(f != -1);

    assert(tfs_write(f, file_contents, sizeof(file_contents)));

    assert(tfs_close(f) != -1);

    // Create hard link on a file
    assert(tfs_link(file_path, hard_link_path) != -1);

    // Unlink file
    assert(tfs_unlink(file_path) != -1);

    // Soft link unusable
    assert(tfs_open(soft_link_path, 0) == -1);

    // Create new file with the same name
    f = tfs_open(hard_link_path, TFS_O_CREAT);
    assert(f != -1);

    // Check if file still contains the correct data
    uint8_t buffer[sizeof(file_contents)];
    assert(tfs_read(f, buffer, sizeof(buffer)) == sizeof(buffer));
    assert(memcmp(buffer, file_contents, sizeof(buffer)) == 0);

    assert(tfs_destroy() != -1);

    printf("Successful test.\n");

    return 0;
}
