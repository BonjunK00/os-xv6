#include "types.h"
#include "stat.h"
#include "user.h"
#include "fcntl.h"

#define SIZE 1024

void
writetest(char* filename)
{
    int fd;
    char file[SIZE];

    // Initialize the file with 'a'
    for (int i = 0; i < SIZE; i++) 
        file[i] = 'a';

    // Create the file.
    fd = open(filename, O_CREATE | O_RDWR);
    if (fd >= 0) {
        printf(1, "Create Success\n");
    }
    else {
        printf(1, "Create Failed\n");
        exit();
    }

    // Write test.
    printf(1, "Write Proceeding\n");
    for (int i = 0; i < 1024; i++) {
        printf(1, "[%d / 1024]\n", i);
        for (int j = 0; j < 16; j++) {
            if (write(fd, file, SIZE) != SIZE) {
                printf(1, "Write Failed\n");
                exit();
            }
        }
    }
    printf(1, "Write Success\n");
    sync();
    close(fd);
}

void
readtest(char* filename)
{
    int fd;
    char file[SIZE];

    // Open the file with read-only mode.
    fd = open(filename, O_RDONLY);
    if (fd >= 0) {
        printf(1, "Open Success\n");
    }
    else {
        printf(1, "Open Failed\n");
        exit();
    }

    // Read test.
    printf(1, "Read Proceeding\n");
    for (int i = 0; i < 1024; i++) {
        printf(1, "[%d / 1024]\n", i);
        for (int j = 0; j < 16; j++) {
            if (read(fd, file, SIZE) != SIZE || file[0] != 'a') {
                printf(1, "Read Failed\n");
                exit();
            }
        }
    }

    printf(1, "Read Success\n");
    close(fd);
}

void
removetest(char* filename)
{
    if (unlink(filename) < 0) 
        printf(1, "Remove Fail");
    else
        printf(1, "Remove Success");
}

int
main(void)
{
    char filename[5] = "test";

    writetest(filename);
    readtest(filename);
    removetest(filename);

    exit();
}