#pragma once

#include "types.h"
#include "align.h"

typedef struct temporary_storage_t {
    uint8* data;
    uint32 capacity;
    uint32 mark;
} temporary_storage_t;


#ifdef __cplusplus
extern "C" {
#endif

u32 temporary_read_mark();
void temporary_write_mark(u32);

void* temporary_alloc(u32 size, alignment_info_t alignment);
void temporary_reset();

#ifdef __cplusplus
}
#endif
