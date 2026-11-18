#include "types.h"
#include "stat.h"
#include "user.h"

static char *cmds[]={"cat", "echo", "grep","init", "kill", "ln", "ls", "mkdir", "pause",
     "rm", "sh", "stressfs", "uptime", "usertests", "wc", "zombie", 0};
    
void autocomplete(char *buf){

    int len = strlen(buf);
    int count = 0;
    char *match = 0;

    for (int i=0; cmds[i]!=0; i++){
        if (strncmp(buf, cmds[i], len) == 0) {
            if (count == 0)
                match = cmds[i];  
            else
                match = 0;     

            count++;
            printf(1, "%s ", cmds[i]);
        }
    }

    if (count == 1 && match) {
        strcpy(buf, match);
    }
    else if (count > 1) {
        printf(1, "\n"); 
    }
}
