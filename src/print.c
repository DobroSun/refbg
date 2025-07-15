
#include "print.h"
#include "temporary_storage.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#define STB_SPRINTF_IMPLEMENTATION
#include "stb_sprintf.h"


literal sprint(char const *fmt, ...)
{
    int out;
    {
        va_list va;
        va_start(va, fmt);

        out = stbsp_vsnprintf(NULL, 0, fmt, va);
        va_end(va);
    }

    literal result = {
        .data  = (const char*) malloc(out + 1),
        .count = out,
    };

    {
        va_list va;
        va_start(va, fmt);

        out = stbsp_vsnprintf((char*) result.data, out + 1, fmt, va);
        va_end(va);

        assert((int) result.count == out);
    }

    return result;
}

literal tprint(char const *fmt, ...)
{
    int out;

    {
        va_list va;
        va_start(va, fmt);

        out = stbsp_vsnprintf(NULL, 0, fmt, va);
        va_end(va);
    }

    literal result = {
        .data = (const char*) temporary_alloc(out + 1, align1),
        .count = out,
    };

    {
        va_list va;
        va_start(va, fmt);

        out = stbsp_vsnprintf((char*) result.data, out + 1, fmt, va);
        va_end(va);

        assert((int) result.count == out);
    }

    return result;
}

static literal tprint_va(char const *fmt, va_list va1, va_list va2)
{
    auto out = stbsp_vsnprintf(NULL, 0, fmt, va1);

    literal result = {};
    result.data    = (const char*) temporary_alloc(out + 1, align1);
    result.count   = out;

    out = stbsp_vsnprintf((char*) result.data, out + 1, fmt, va2);
    assert((int) result.count == out);
    return result;
}

void print(char const *fmt, ...) {
    literal string;
    {
        va_list va1, va2 ;
        va_start(va1, fmt);
        va_copy(va2, va1);

        string = tprint_va(fmt, va1, va2);

        va_end(va1);
        va_end(va2);
    }

    puts(string.data);
}
