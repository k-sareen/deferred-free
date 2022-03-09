#include <stddef.h>

// 1 MB thread-local buffer size
#define BUFFER_SIZE             1048576

void *ql_malloc(size_t size);
void ql_free(void *ptr);

void *ql_calloc(size_t nmemb, size_t size);
void *ql_realloc(void *ptr, size_t size);

#define QL_ALIAS(fun) __attribute__((alias(#fun), used, visibility("default")))
