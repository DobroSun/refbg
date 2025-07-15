#include "align.h"

uintptr_t align(uintptr_t ptr, alignment_info_t info) {
    u32 alignment = info.alignment;
    if (alignment == 0 || (alignment & (alignment - 1)) != 0) { // If not power of two
        return ptr;
    }

    return (ptr + (alignment - 1)) & ~(alignment - 1);
}

const alignment_info_t align16 = { 16 };
const alignment_info_t align8 = { 8 };
const alignment_info_t align4 = { 4 };
const alignment_info_t align2 = { 2 };
const alignment_info_t align1 = { 1 };

