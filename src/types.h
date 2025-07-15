#pragma once

#include <stdint.h>
#include <stddef.h>

typedef struct {
    const char* data;
    size_t count;
} literal;

#ifdef __cplusplus
#define lit(x) literal { (x), sizeof(x)-1 }
#else
#define lit(x) (literal) { (x), sizeof(x)-1 }
#endif

#define fmt(x) (int) ((x).count), (x).data

typedef int8_t s8;
typedef int16_t s16;
typedef int32_t s32;
typedef int64_t s64;
typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef float  f32;
typedef double f64;

typedef int8_t int8;
typedef int16_t int16;
typedef int32_t int32;
typedef int64_t int64;

typedef uint8_t uint8;
typedef uint16_t uint16;
typedef uint32_t uint32;
typedef uint64_t uint64;
typedef float  float32;
typedef double float64;

