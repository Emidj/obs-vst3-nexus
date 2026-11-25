#pragma once
#include <stdlib.h>
#ifdef __cplusplus
extern "C" {
#endif
void* nexus_ebur_malloc(size_t);
void* nexus_ebur_calloc(size_t,size_t);
void* nexus_ebur_realloc(void*,size_t);
void nexus_ebur_free(void*);
#ifdef __cplusplus
}
#endif
#define malloc nexus_ebur_malloc
#define calloc nexus_ebur_calloc
#define realloc nexus_ebur_realloc
#define free nexus_ebur_free
