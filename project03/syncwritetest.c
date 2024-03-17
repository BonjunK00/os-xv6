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
    struct stat st;

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
    for (int i = 0; i < 128; i++) {
        if (write(fd, file, SIZE) != SIZE) {
            printf(1, "Write Failed\n");
            exit();
        }
    }
    printf(1, "Write Success\n");

    // Print the file size.
    if(fstat(fd, &st) < 0){
        close(fd);
        exit();
    }
    printf(1, "File size: %d\n", st.size);

    close(fd);
}

int
main(void)
{
    char syncfile[5] = "sync", asyncfile[6] = "async";

    printf(1, "[Create a file with sync()]\n");
    writetest(syncfile);
    printf(1, "Sync: %d blocks are written\n", sync());

    printf(1, "[Create a file without sync()]\n");
    writetest(asyncfile);

    exit();
}