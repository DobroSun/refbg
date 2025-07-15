#pragma once

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

literal sprint(char const *fmt, ...);
literal tprint(char const *fmt, ...);
void     print(const char* fmt, ...);


#ifdef __cplusplus
}
#endif
