#pragma once

#include "types.h"
#include "align.h"

typedef struct memory_arena_t {
    uint8* data;
    uint32 capacity;
    uint32 mark;
} memory_arena_t;

void arena_init(memory_arena_t* arena, u32 size);

void* arena_alloc(memory_arena_t* arena, u32 size, alignment_info_t alignment);
void arena_reset(memory_arena_t* arena);

