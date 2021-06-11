#define _GNU_SOURCE   // for RTLD_NEXT
#include <dlfcn.h>    // for dlsym()
#include <stdio.h>    // for printf()
#include <stdlib.h>   // for malloc(), free(), calloc(), realloc(), getenv()
#include <malloc.h>   // for malloc_usable_size()
#include <pthread.h>  // for pthread_key_t, pthread_key_create(), pthread_self()
#include <unistd.h>   // for close()
#include <fcntl.h>    // for open()
#include <sys/mman.h> // for mmap(), munmap()

#include "ql.h"

#define DEBUG   0
#define VERBOSE 0
#define printd(fmt, ...) \
                do { if (DEBUG) fprintf(stderr, fmt, __VA_ARGS__); } while (0)
#define printd_verbose(fmt, ...) \
                do { if (DEBUG && VERBOSE) fprintf(stderr, fmt, __VA_ARGS__); } while (0)

// Default quarantine list size; it can be set from the QL_SIZE environment variable
static unsigned long ql_size = 40960;
static const void *ql_empty = NULL;
static pthread_key_t tls_default_key = (pthread_key_t)(-1);

// Quarantine list and associated variables. Have to initialize it to &ql_empty
// as otherwise there are some issues with allocating the thread local variable
// during execution.
static __thread void **ql = (void *) &ql_empty;
static __thread int ql_offset = 0;
static __thread size_t ql_current_size = 0;

static void (*real_free)(void* ptr) = NULL;

// The pthread destructor set with pthread_key_create() is not called when
// pthreads are not used in the main executable as main() just exits and does
// not actually call pthread_exit(). Hence, we use a hack wherein this function
// is also called in ql_fini() which is the destructor for the entire library.
// This prevents leaking memory when the program has finished execution.
static void ql_collect()
{
    if (ql == (void *) &ql_empty) return;
    printd("ql: run thread cleanup %p\n", &ql);

    for (int i = ql_offset - 1; i >= 0; i--) {
        printd_verbose("ql: free %p %p\n", ql[i], &ql);
        real_free(ql[i]);
    }

    ql_offset = 0;
    ql_current_size = 0;

    munmap(ql, BUFFER_SIZE);
    ql = (void *) &ql_empty;
}

// Constructor for the entire library. We need to obtain a function pointer to
// the real free() (either glibc's free() or the free() of another drop-in
// malloc replacement) so that we can use it internally. This also sets a
// destructor (ql_collect()) to be associated with pthreads exiting as well as
// sets the size of the quarantine list by reading the environment variable
// QL_SIZE.
__attribute__((constructor)) static void ql_init()
{
    printf("initializing libql\n");
    real_free = dlsym(RTLD_NEXT, "free");
    pthread_key_create(&tls_default_key, &ql_collect);

    char *ql_size_str = getenv("QL_SIZE");
    if (ql_size_str != NULL) {
        ql_size = strtoul(ql_size_str, NULL, 10);
        ql_size = ql_size == 0 ? 40960 : ql_size;
    }

    printd("ql: ql_size = %lu\n", ql_size);
}

// Destructor for the entire library. See comment above ql_collect() for more insight.
__attribute__((destructor)) static void ql_fini()
{
    ql_collect();
}

// We don't care about how the allocation takes place as we only care about
// emulating a GC's free behaviour (i.e. freeing a large number of objects in
// one go).
inline void *ql_malloc(size_t size)
{
    return malloc(size);
}

inline void *ql_calloc(size_t nmemb, size_t size)
{
    return calloc(nmemb, size);
}

inline void *ql_realloc(void *ptr, size_t size)
{
    return realloc(ptr, size);
}

// Set up the thread local storage/variables for the calling pthread.
static inline void tls_setup()
{
    printd("ql: run thread setup %p\n", &ql);

    // Use /dev/zero to get already zeroed memory when mmap'd XXX: non-portable
    int fd = open("/dev/zero", O_RDWR);

    ql_offset = 0;
    ql_current_size = 0;
    ql = mmap(NULL, BUFFER_SIZE, PROT_READ | PROT_WRITE, MAP_PRIVATE, fd, 0);
    close(fd);
}

// We place malloc'd memory into a quarantine list in order to artificially add
// the inhale-exhale behaviour of garbage collection to manual memory management.
//
// The idea is that we place malloc'd memory into the quarantine list until we
// have either exhausted the buffer or we have gone over a certain limit in
// terms of volume of memory (i.e. if we have quarantined more than a certain
// amount of bytes). When either of these two conditions are true, we walk the
// quarantine list and free all the memory.
//
// This is much different from a garbage collector as we don't perform a
// transitive closure over the entire heap to figure out what is alive or dead;
// instead we rely on the programmer to insert (hopefully correct) calls to
// free() when objects are no longer required.
void ql_free(void *ptr)
{
    size_t size;

    // initialize the quarantine list XXX: adds extra comparison for every free; try and remove it and place it elsewhere
    printd("ql: called free %p\n", ptr);
    printd_verbose("t%p: %p\n", (void *) pthread_self(), ptr);
    if (ql == (void *) &ql_empty) {
        tls_setup();
        if (tls_default_key != (pthread_key_t)(-1)) {
            pthread_setspecific(tls_default_key, ql);
        }
    }

    ql[ql_offset++] = ptr;
    size = malloc_usable_size(ptr);
    ql_current_size += size;

    printd_verbose("ql: add  %p %p ql_current_size = %ld, size = %ld\n",
            ptr, &ql, ql_current_size, size);

    if (ql_current_size >= ql_size
            || ql_offset >= BUFFER_SIZE) {
        // slow path
        printd_verbose("ql: ql_current_size = %ld, ql_offset = %d\n",
                ql_current_size, ql_offset);
        for (int i = ql_offset - 1; i >= 0; i--) {
            printd_verbose("ql: free %p %p\n", ql[i], &ql);
            real_free(ql[i]);
        }

        ql_offset = 0;
        ql_current_size = 0;
    }
}

// Overriding free() implementations. These have to declared here instead of
// the header file as the compiler complains that you can't use a prototype
// function as an alias for another function.
//
// Primarily lifted from the mimalloc (https://github.com/microsoft/mimalloc)
// source code.
void free(void *ptr) QL_ALIAS(ql_free);
#if (defined(__GNUC__) || defined(__clang__))
    // ------------------------------------------------------
    // Override by defining the mangled C++ names of the operators (as
    // used by GCC and CLang).
    // See <https://itanium-cxx-abi.github.io/cxx-abi/abi.html#mangling>
    // ------------------------------------------------------
    void _ZdlPv(void* p)            QL_ALIAS(ql_free); // delete
    void _ZdaPv(void* p)            QL_ALIAS(ql_free); // delete[]
    __attribute__((used)) void _ZdlPvm(void* p, size_t n) { ql_free(p); }
    __attribute__((used)) void _ZdaPvm(void* p, size_t n) { ql_free(p); }
    __attribute__((used)) void _ZdlPvSt11align_val_t(void* p, size_t al)            { ql_free(p); }
    __attribute__((used)) void _ZdaPvSt11align_val_t(void* p, size_t al)            { ql_free(p); }
    __attribute__((used)) void _ZdlPvmSt11align_val_t(void* p, size_t n, size_t al) { ql_free(p); }
    __attribute__((used)) void _ZdaPvmSt11align_val_t(void* p, size_t n, size_t al) { ql_free(p); }
#endif
