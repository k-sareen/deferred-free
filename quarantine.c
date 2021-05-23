#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <malloc.h>
#include <pthread.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>

#include "quarantine.h"

struct ql_tls_t {
    void **ql;
    int ql_offset;
    size_t ql_current_size;
};

static const struct ql_tls_t ql_empty_tls = {
    NULL,
    0,
    0
};

// static pthread_once_t init_once = PTHREAD_ONCE_INIT;
static pthread_once_t key_once = PTHREAD_ONCE_INIT;
static pthread_key_t tls_default_key = (pthread_key_t)(-1);
static __thread struct ql_tls_t *tls_default = (struct ql_tls_t *) &ql_empty_tls;

static void *(*real_malloc)(size_t size) = NULL;
static void (*real_free)(void* ptr) = NULL;
static void *(*real_calloc)(size_t nmemb, size_t size) = NULL;
static void *(*real_realloc)(void *ptr, size_t size) = NULL;

// TODO: destructor not called when pthreads are not used in the main executable
// as main() just exits and does not actually call pthread_exit. Hence this is
// leaky when used for single-threaded workloads or workloads which generate
// other processes
static void thread_done(void *ptr) {
    printf("ql: run thread cleanup\n");
    struct ql_tls_t *tls = (struct ql_tls_t *) ptr;

    for (int i = tls->ql_offset - 1; i >= 0; i--) {
        // printf("ql: free %p %p\n", tls->ql[i], &tls);
        real_free(tls->ql[i]);
    }

    tls->ql_offset = 0;
    tls->ql_current_size = 0;

    munmap(tls->ql, BUFFER_SIZE);
    real_free(tls);
    tls = (struct ql_tls_t *) &ql_empty_tls;
}

static void key_create() {
    printf("ql: run key create\n");
    pthread_key_create(&tls_default_key, &thread_done);
}

__attribute__((constructor)) static void ql_init()
{
    // printf("ql: init constructor\n");
    pthread_once(&key_once, key_create);

    real_malloc = dlsym(RTLD_NEXT, "malloc");
    real_free = dlsym(RTLD_NEXT, "free");
    real_calloc = dlsym(RTLD_NEXT, "calloc");
    real_realloc = dlsym(RTLD_NEXT, "realloc");
    // printf("ql: %p %p %p %p\n", real_malloc, real_free, real_calloc, real_realloc);
}

// We don't care about how the allocation takes place as we only care about
// emulating a GC's free behaviour (i.e. freeing a large number of objects in
// one go).
inline void *ql_malloc(size_t size)
{
    return real_malloc(size);
}

inline void *ql_calloc(size_t nmemb, size_t size)
{
    return real_calloc(nmemb, size);
}

inline void *ql_realloc(void *ptr, size_t size)
{
    return real_realloc(ptr, size);
}

static inline struct ql_tls_t *tls_setup() {
    printf("ql: run thread setup\n");
    // Use /dev/zero to get already zeroed memory when mmap'd XXX: non-portable
    int fd = open("/dev/zero", O_RDWR);

    struct ql_tls_t *tls_tmp = malloc(sizeof(struct ql_tls_t));
    tls_tmp->ql_offset = 0;
    tls_tmp->ql_current_size = 0;
    tls_tmp->ql = mmap(NULL, BUFFER_SIZE, PROT_READ | PROT_WRITE, MAP_PRIVATE, fd, 0);
    close(fd);

    return tls_tmp;
}

void ql_free(void *ptr)
{
    size_t size;
    struct ql_tls_t *tls = tls_default;

    // initialize the quarantine list TODO: adds extra comparison for every free; try and remove it and place it elsewhere
    // printf("t%p: %p\n", pthread_self(), ptr);
    if (tls->ql == NULL) {
        tls = tls_setup();
        tls_default = tls;

        if (tls_default_key != (pthread_key_t)(-1)) {
            pthread_setspecific(tls_default_key, tls);
        }
    }

    tls->ql[tls->ql_offset++] = ptr;
    size = malloc_usable_size(ptr);
    tls->ql_current_size += size;

    // printf("ql: add  %p %p ql_current_size = %ld, size = %ld\n",
    //         ptr, &tls, tls->ql_current_size, size);

    if (tls->ql_current_size >= QL_SIZE
            || tls->ql_offset >= BUFFER_SIZE) {
        // printf("ql: ql_current_size = %ld, ql_offset = %d\n",
        //         tls->ql_current_size, tls->ql_offset);
        for (int i = tls->ql_offset - 1; i >= 0; i--) {
            // printf("ql: free %p %p\n", tls->ql[i], &tls);
            real_free(tls->ql[i]);
        }

        tls->ql_offset = 0;
        tls->ql_current_size = 0;
    }
}

void free(void *ptr) QL_ALIAS(ql_free);
