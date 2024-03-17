#include "types.h"
#include "stat.h"
#include "user.h"

#define CMDLENGTH 100
#define PATHLENGTH 100
#define NUMLENGTH 10

int 
main(int argc, char *argv[])
{  
    while(1) {
        char cmd[CMDLENGTH], arg[10][CMDLENGTH] = {};
        int argnum = 0, j = 0;

        gets(cmd, CMDLENGTH);
        for(int i = 0; cmd[i]; i++) {
            if(cmd[i] == ' ' || cmd[i] == '\n' || cmd[i] == '\r') {
                arg[argnum++][j] = '\0';
                j = 0;
                continue;
            }
            arg[argnum][j++] = cmd[i];
        }
        arg[argnum][j] = '\0';

        if(!strcmp(arg[0], "list")) {
            procdump2();
        }
        else if(!strcmp(arg[0], "kill")) {
            if(kill(atoi(arg[1])) == 0)
                printf(1, "kill success.\n");
            else
                printf(1, "kill fail.\n");
                
        }
        else if(!strcmp(arg[0], "execute")) {
            char *execargv[PATHLENGTH];
            execargv[0] = arg[1];
            if(fork() == 0) {
                if(exec2(arg[1], execargv, atoi(arg[2])) == -1)
                    printf(1, "execute fail.\n");
                exit();
            }
        }
        else if(!strcmp(arg[0], "memlim")) {
            if(setmemorylimit(atoi(arg[1]), atoi(arg[2])) == 0)
                printf(1, "memlim success.\n");
            else
                printf(1, "mimlim fail.\n");
        }
        else if(!strcmp(arg[0], "exit")) {
            break;
        }
    }

    exit();
};