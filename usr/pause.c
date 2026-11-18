#include "types.h"
#include "stat.h"
#include "user.h"

int main(int argc, char *argv[])
{

    if (argc != 2)
    {
        printf(1, "usage: pause <seconds>\n");    
        exit();
    }
    
    int sec = atoi(argv[1]);

    sleep(sec*10);
    
    exit();
}