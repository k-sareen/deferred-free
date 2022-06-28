#define _GNU_SOURCE    // for RTLD_NEXT
#include <dlfcn.h>     // for dlsym()
#include <stdio.h>     // for printf(), fprintf()
#include <stdlib.h>    // for malloc(), free(), calloc(), realloc(), getenv()
#include <malloc.h>    // for malloc_usable_size()
#include <pthread.h>   // for pthread_key_t, pthread_key_create(), pthread_self()
#include <stdbool.h>   // for bool type
#include <sys/mman.h>  // for mmap(), munmap()
#include <stdatomic.h> // for atomic types and operations

#include "ql.h"

#define DEBUG   0
#define VERBOSE 0
#define UPDATE_N_FREES  0
#define printd0(fmt) \
                do { if (DEBUG) fprintf(stdout, fmt); } while (0)
#define printd(fmt, ...) \
                do { if (DEBUG) fprintf(stdout, fmt, __VA_ARGS__); } while (0)
#define printd_v(fmt, ...) \
                do { if (DEBUG && VERBOSE) fprintf(stderr, fmt, __VA_ARGS__); } while (0)

// Default quarantine list size; it can be set from the QL_SIZE environment variable
long ql_size = 4096;
int log_ql_size = 12;
static const void *ql_empty = NULL;
static pthread_key_t tls_default_key = (pthread_key_t)(-1);

// Quarantine list and associated variables. Have to initialize it to &ql_empty
// as otherwise there are some issues with allocating the thread local variable
// during execution.
static __thread void **ql = (void *) &ql_empty;
static __thread int ql_offset = 0;
static __thread size_t ql_current_size = 0;

atomic_size_t ql_global_size = 0;
static __thread size_t collection_count = 0;

#if UPDATE_N_FREES == 1
static __thread int num_frees = 0;
static __thread size_t current_global_size = 0;
static const int MAX_NUM_FREES = 128;
#endif

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

    for (int i = 0; i < ql_offset; i++) {
        printd_v("ql: free %p %p\n", ql[i], &ql);
        if (real_free == NULL) return;
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
    printd0("initializing libql\n");
    real_free = dlsym(RTLD_NEXT, "free");
    pthread_key_create(&tls_default_key, &ql_collect);

    char *ql_size_str = getenv("QL_SIZE");
    if (ql_size_str != NULL) {
        ql_size = strtoul(ql_size_str, NULL, 10);
        ql_size = ql_size == 0 ? 1 : ql_size;
    }

    if (ql_size != 1) {
        // See: https://stackoverflow.com/questions/3272424/compute-fast-log-base-2-ceiling/51351885#51351885
        log_ql_size = 63 - __builtin_clzl(ql_size - 1) + 1;
    } else {
        log_ql_size = 0;
    }

    printd("ql: ql_size = %lu log_ql_size = %d\n", ql_size, log_ql_size);
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

    ql_offset = 0;
    ql_current_size = 0;
    ql = mmap(NULL, BUFFER_SIZE, PROT_READ | PROT_WRITE,
            MAP_PRIVATE | MAP_ANONYMOUS, 0 /* fd */, 0 /* offset */);
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
    if (ptr == NULL) return;

    size_t size;
#if UPDATE_N_FREES == 1
    num_frees++;
#else
    size_t tmp;
    size_t current_global_size = 0;
#endif

    // Initialize the quarantine list XXX: adds extra comparison for every free; try and remove it and place it elsewhere
    if (ql == (void *) &ql_empty) {
        tls_setup();
        if (tls_default_key != (pthread_key_t)(-1)) {
            pthread_setspecific(tls_default_key, ql);
        }
    }

    printd("ql %p: %p\n", &ql, ptr);

    ql[ql_offset++] = ptr;
    size = malloc_usable_size(ptr);

#if UPDATE_N_FREES == 1
    ql_current_size += size;
    if (num_frees >= MAX_NUM_FREES) {
        current_global_size = atomic_fetch_add(&ql_global_size, ql_current_size);
        ql_current_size = 0;
        num_frees = 0;
    }
#else
    tmp = atomic_fetch_add(&ql_global_size, size);
    current_global_size = tmp + size;
#endif

    size_t cc = (current_global_size >> log_ql_size);
    bool collection_required = cc > collection_count;

    printd_v("ql: add  %p %p ql_current_size = %ld, size = %ld\n",
            ptr, &ql, ql_current_size, size);

    // Check if we have quarantined more than the user defined volume. If we
    // have, then walk the list and free all memory
    if (collection_required) {
#if DEBUG == 1
        size = 0;
#endif
        printd("ql %p: current_global_size = %ld (%ld), collection_count = %ld, ql_size = %ld\n",
                &ql, current_global_size, current_global_size / ql_size,
                collection_count, ql_size);

        // slow path
        for (int i = 0; i < ql_offset; i++) {
            printd_v("ql: free %p %p\n", ql[i], &ql);
#if DEBUG == 1
            size += malloc_usable_size(ql[i]);
#endif
            if (real_free == NULL) return;
            real_free(ql[i]);
        }

        printd("ql %p: collected %ld bytes and %d objects\n", &ql, size, ql_offset);

        ql_offset = 0;
#if UPDATE_N_FREES == 1
        ql_current_size = 0;
#endif
        collection_count = cc;
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
