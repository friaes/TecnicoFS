#include "fs/operations.h"
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>

#define MAX_OPEN_FILES 16

const char *file_path = "/f1";
uint8_t const str[] = "ABC";
uint8_t const file_contents[] = "ABCABCABCABCABCABCABCABCABCABCABCABCABCABCABCABC";
uint8_t buffer[sizeof(file_contents)]; 
// the size of the buffer corresponds to the string being written 16x in the file 

void *write_str() {
    int f = tfs_open(file_path, TFS_O_CREAT);
    assert(f != -1);

    assert(tfs_read(f, &buffer, sizeof(buffer)) != -1);
    assert(tfs_write(f, str, sizeof(str)-1) == sizeof(str)-1);

    assert(tfs_close(f) != -1);

    return NULL;
}

void assert_contents_ok() {
    int f = tfs_open(file_path, 0);
    assert(f != -1);
    
    assert(tfs_read(f, buffer, sizeof(buffer)) == sizeof(buffer)-1);
    assert(memcmp(buffer, file_contents, sizeof(buffer)) == 0);

    assert(tfs_close(f) != -1);
}

int main() {
    // init TécnicoFS
    assert(tfs_init(NULL) != -1);

    pthread_t tid[MAX_OPEN_FILES];

    for (int i = 0; i < MAX_OPEN_FILES; ++i) {
        assert(pthread_create(&tid[i], NULL, write_str, NULL) == 0);
    }

    for (int i = 0; i < MAX_OPEN_FILES; ++i) {
        assert(pthread_join(tid[i], NULL) == 0);
    }

    assert_contents_ok();

    // destroy TécnicoFS
    assert(tfs_destroy() != -1);

    printf("Successful test.\n");

    return 0;
}
