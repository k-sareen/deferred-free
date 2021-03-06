#include <stddef.h>

// 10 OS pages worth of buffer space (5120 pointers)
#define BUFFER_SIZE             40960
#define NUM_PTRS_IN_BUFFER      BUFFER_SIZE / sizeof(void *)

void *ql_malloc(size_t size);
void ql_free(void *ptr);

void *ql_calloc(size_t nmemb, size_t size);
void *ql_realloc(void *ptr, size_t size);

#define QL_ALIAS(fun) __attribute__((alias(#fun), used, visibility("default")))
