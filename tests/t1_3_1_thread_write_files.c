#include "fs/operations.h"
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>

#define FILE_COUNT 3

uint8_t const file_contents[] = "ABC";

char *tfs_files[] = {"/f1", "/f2", "/f3"};

void *write_contents(void *input) {

    int file_id = *((int *)input);
    char *path = tfs_files[file_id];

    int f = tfs_open(path, TFS_O_CREAT);
    assert(f != -1);

    assert(tfs_write(f, file_contents, sizeof(file_contents)) ==
           sizeof(file_contents));

    assert(tfs_close(f) != -1);

    
    return NULL;
}

void assert_contents_ok(char *tfs_file) {
    int f = tfs_open(tfs_file, 0);
    assert(f != -1);

    uint8_t buffer[sizeof(file_contents)];
    assert(tfs_read(f, buffer, sizeof(buffer)) == sizeof(buffer));
    assert(memcmp(buffer, file_contents, sizeof(buffer)) == 0);

    assert(tfs_close(f) != -1);
}

int main() {
    assert(tfs_init(NULL) != -1);

    pthread_t tid[FILE_COUNT];
    int file_id[FILE_COUNT];

    for (int i = 0; i < FILE_COUNT; ++i) {
        file_id[i] = i;
        assert(pthread_create(&tid[i], NULL, write_contents,
                              (void *)(&file_id[i])) == 0);
    }

    for (int i = 0; i < FILE_COUNT; ++i) {
        assert(pthread_join(tid[i], NULL) == 0);
    }

    for (int i = 0; i < FILE_COUNT; ++i) {
        assert_contents_ok(tfs_files[i]);
    }

    assert(tfs_destroy() != -1);

    printf("Successful test.\n");

    return 0;
}
