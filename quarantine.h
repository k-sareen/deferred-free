#include <stddef.h>

// 10 OS pages worth of buffer space (5120 pointers)
#define BUFFER_SIZE     40960
// 10 OS pages worth of memory needs to be allocated before it bulk frees TODO: make configurable
#define QL_SIZE         40960

void *ql_malloc(size_t size);
void ql_free(void *ptr);

void *ql_calloc(size_t nmemb, size_t size);
void *ql_realloc(void *ptr, size_t size);

#define QL_ALIAS(fun) __attribute__((alias(#fun), used, visibility("default")))
