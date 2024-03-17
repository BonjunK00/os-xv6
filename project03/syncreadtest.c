#include "types.h"
#include "stat.h"
#include "user.h"
#include "fcntl.h"

#define SIZE 1024

void
readtest(char* filename)
{
    int fd;
    char file[SIZE];
    struct stat st;

    // Open the file with read-only mode.
    fd = open(filename, O_RDONLY);
    if (fd >= 0) {
        printf(1, "Open Success\n");
    }
    else {
        printf(1, "Open Failed\n");
        return;
    }

    // Print the file size.
    if(fstat(fd, &st) < 0){
        close(fd);
        exit();
    }
    printf(1, "File size: %d\n", st.size);
 
    // Read test.
    for (int i = 0; i < 128; i++) {
        if (read(fd, file, SIZE) != SIZE) {
            printf(1, "Read Failed\n");
            return;
        }
        for(int j = 0; j < SIZE; j++) {
            if(file[j] != 'a') {
                printf(1, "Read Failed\n");
                return;
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
        printf(1, "Remove Fail\n");
    else
        printf(1, "Remove Success\n");
}

int
main(void)
{
    char syncfile[5] = "sync", asyncfile[6] = "async";

    printf(1, "[Read a file written with sync()]\n");
    readtest(syncfile);
    printf(1, "[Read a file written without sync()]\n");
    readtest(asyncfile);

    removetest(syncfile);
    removetest(asyncfile);
    
    exit();
}