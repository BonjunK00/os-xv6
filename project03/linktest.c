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
    if (write(fd, file, SIZE) != SIZE) {
        printf(1, "Write Failed\n");
        exit();
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
        return;
    }

    // Read test.
    if (read(fd, file, SIZE) != SIZE || file[0] != 'a') {
        printf(1, "Read Failed\n");
        exit();
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

void
slinktest(char* oldfile, char* newfile)
{
    if(slink(oldfile, newfile) < 0)
        printf(1, "Link Fail\n");
    else
        printf(1, "Link Success\n");
}

int
main(void)
{
    char oldfile[4] = "old", newfile[4] = "new";

    printf(1, "[Create old file]\n");
    writetest(oldfile);

    printf(1, "[Make symbolic link file]\n");
    slinktest(oldfile, newfile);
    readtest(newfile);

    printf(1, "[Remove old file]\n");
    removetest(oldfile);

    printf(1, "[Read symbolic link file again]\n");
    readtest(newfile);

    printf(1, "[Remove new file]\n");
    removetest(newfile);

    exit();
}