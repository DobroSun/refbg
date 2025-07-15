#include "libdecor.h"
#include <dlfcn.h>

#ifdef HAS_LIBDECOR

#define LIBDECOR_SYM(ret, name, ...) ret (* DYN_##name)(__VA_ARGS__);
#include "libdecor_sym.h"
#undef LIBDECOR_SYM


static void* lib = NULL;
void load_libdecor() {
    lib = dlopen("libdecor-0.so", RTLD_LAZY);
    if (lib) {

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
//
// @Ignore: ISO C forbids conversion of object pointer to function pointer type [-Werror=pedantic]
//

#define LIBDECOR_SYM(ret, name, ...) DYN_##name = (ret (*)(__VA_ARGS__)) dlsym(lib, #name);
#include "libdecor_sym.h"
#undef LIBDECOR_SYM

#pragma GCC diagnostic pop

    }

}

bool using_libdecor() {
    return lib != NULL;
}

void unload_libdecor() {
    if (lib) {
        dlclose(lib);
    }
}

#endif // HAS_LIBDECOR
