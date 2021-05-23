#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "quarantine.h"

#define NUM_THREADS     5

void *print_hello(void *threadid)
{
    int i;
    long tid;
    char *str;
    tid = (long)threadid;

    for (i = 0; i < 10000; i++) {
        str = malloc(sizeof(char) * (5 + i));
        sprintf(str, "h%ld%d", tid, i);
        printf("tid %ld: %s\n", tid, str);
        free(str);
    }

    return NULL;
}

int main(int argc, char *argv[])
{
    int err;
    long t;
    pthread_t threads[NUM_THREADS];

    for (t=0; t < NUM_THREADS; t++) {
        err = pthread_create(&threads[t], NULL, print_hello, (void *)t);
        if (err) {
            printf("ERROR; return code from pthread_create() is %d\n", err);
            exit(-1);
        }
    }

    for (t=0; t < NUM_THREADS; t++) {
        pthread_join(threads[t], NULL);
    }

    return 0;
}

