#pragma once
#include "types.h"

typedef struct alignment_info_t {
    u32 alignment;
} alignment_info_t;

uintptr_t align(uintptr_t ptr, alignment_info_t info);

extern const alignment_info_t align16;
extern const alignment_info_t align8;
extern const alignment_info_t align4;
extern const alignment_info_t align2;
extern const alignment_info_t align1;
