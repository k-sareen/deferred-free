#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>

#include "ql.h"

void print_hello(pid_t pid)
{
    int i;
    char *id, *str;
    if (pid) {
        id = "parent";
    } else {
        id = "child";
    }

    for (i = 0; i < 10000; i++) {
        str = malloc(sizeof(char) * (3 + i));
        sprintf(str, "h%d", pid + i);
        printf("%s: %s\n", id, str);
        free(str);
    }
}

int main(int argc, char *argv[])
{
    pid_t pid;

    for (int i = 0; i < 3; i++) {
        pid = fork();
        print_hello(pid);
    }
    return 0;
}

