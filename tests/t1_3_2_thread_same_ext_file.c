#include "fs/operations.h"
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>

#define FILE_COUNT 3

char *str_ext_file =
        "BBB! BBB! BBB! BBB! BBB! BBB! BBB! BBB! BBB! BBB! BBB! BBB! BBB! BBB! "
        "BBB! BBB! BBB! BBB! BBB! BBB! BBB! BBB! BBB! BBB! BBB! BBB! BBB! BBB! "
        "BBB! BBB! BBB! BBB! BBB! BBB! BBB! BBB! BBB! BBB! BBB! BBB! BBB! BBB! "
        "BBB! BBB! BBB! BBB! BBB! BBB! BBB! BBB! BBB! BBB! BBB! BBB! BBB! BBB! "
        "BBB! BBB! BBB! BBB! BBB! BBB! BBB! BBB! BBB! BBB! BBB! BBB! BBB! BBB! "
        "BBB! BBB! BBB! BBB! BBB! BBB! BBB! BBB! BBB! BBB! BBB! BBB! BBB! BBB! "
        "BBB! BBB! BBB! BBB! BBB! BBB! BBB! BBB! BBB! BBB! BBB! BBB! BBB! BBB! "
        "BBB! BBB! BBB! BBB! BBB! ";
    char *path_tfs_file = "/f1";
    char *path_ext_file_to_copy = "tests/file_to_copy_over512.txt";
    char buffer[600];

void *copy_contents() {
    int f = tfs_copy_from_external_fs(path_ext_file_to_copy, path_tfs_file);
    assert(f != -1);

    return NULL;
}

void assert_contents_ok() {
    int f = tfs_open(path_tfs_file, 0);
    assert(f != -1);

    ssize_t r = tfs_read(f, buffer, sizeof(buffer) - 1);
    assert(r == strlen(str_ext_file));
    assert(!memcmp(buffer, str_ext_file, strlen(str_ext_file)));

    assert(tfs_close(f) != -1);
}

int main() {
    assert(tfs_init(NULL) != -1);

    pthread_t tid[FILE_COUNT];

    for (int i = 0; i < FILE_COUNT; ++i) {
        assert(pthread_create(&tid[i], NULL, copy_contents, NULL) == 0);
    }

    for (int i = 0; i < FILE_COUNT; ++i) {
        assert(pthread_join(tid[i], NULL) == 0);
    }

    for (int i = 0; i < FILE_COUNT; ++i) {
        assert_contents_ok();
    }

    assert(tfs_destroy() != -1);

    printf("Successful test.\n");

    return 0;
}
