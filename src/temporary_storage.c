#include "temporary_storage.h"

#include <stdlib.h>
#include <string.h>


static temporary_storage_t init_temporary() {
    static const uint32 TEMPORARY_CAPACITY = 64 * 1024; // 64kb.

    temporary_storage_t result = {
        .data     = malloc(TEMPORARY_CAPACITY),
        .capacity = TEMPORARY_CAPACITY,
        .mark     = 0,
    };

    return result;
}


static temporary_storage_t* get_temp() {
    static thread_local temporary_storage_t storage = {};

    if (storage.data == nullptr && storage.capacity == 0) {
        storage = init_temporary();
    }

    return &storage;
}


u32 temporary_read_mark() {
    auto temp = get_temp();
    return temp->mark;
}

void temporary_write_mark(u32 mark) {
    auto temp = get_temp();
    temp->mark = mark;
}

void* temporary_alloc(u32 size, alignment_info_t alignment) {
    auto temp = get_temp();

    auto ptr     = temp->data + temp->mark;
    auto aligned = (uint8*) align((uintptr_t) ptr, alignment);
    memset(aligned, 0, size);

    // @Incomplete: check if we fit, otherwise allocate with malloc??? Log error?
    temp->mark += (aligned - ptr) + size;

    return aligned;
}

void temporary_reset() {
    auto temp = get_temp();
    temp->mark = 0;
}
