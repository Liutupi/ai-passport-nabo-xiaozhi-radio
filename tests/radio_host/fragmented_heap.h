#pragma once
#include <stddef.h>
/* These replace malloc/calloc/free in every production translation unit. */
void *fheap_malloc(size_t bytes);
void *fheap_calloc(size_t count, size_t bytes);
void fheap_free(void *pointer);
size_t fheap_free_bytes(void);
size_t fheap_largest(void);
