#include "memory_arena.h"
#include "types.h"

#include <stdlib.h>
#include <string.h>

#if 0
typedef struct memory_arena_t {
    uint8* data;
    uint32 capacity;
    uint32 mark;
} memory_arena_t;

void arena_init(memory_arena_t* arena, u32 size);

void* arena_alloc(memory_arena_t* arena, u32 size, u32 alignment);
void arena_reset(memory_arena_t* arena);
#endif



void arena_init(memory_arena_t* arena, u32 size) {

    *arena = (memory_arena_t) {
        .data     = malloc(size),
        .capacity = size,
        .mark     = 0,
    };
}

void arena_free(memory_arena_t* arena) {
    free(arena->data);
    *arena = (memory_arena_t) {};
}

void* arena_alloc(memory_arena_t* arena, u32 size, alignment_info_t alignment) {

    auto ptr     = arena->data + arena->mark;
    auto aligned = (uint8*) align((uintptr_t) ptr, alignment);
    memset(aligned, 0, size);

    // @Incomplete: check if we fit, otherwise allocate with malloc??? Log error?
    arena->mark += (aligned - ptr) + size;

    return aligned;
}

void arena_reset(memory_arena_t* arena) {
    arena->mark = 0;
}

