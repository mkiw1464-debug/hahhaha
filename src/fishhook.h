// fishhook — Facebook
// https://github.com/facebook/fishhook
// Bundled here for self-contained build

#pragma once
#include <stddef.h>
#include <stdint.h>

struct rebinding {
    const char* name;
    void*       replacement;
    void**      replaced;
};

extern int rebind_symbols(struct rebinding bindings[], size_t bindings_count);
extern int rebind_symbols_image(void* header, intptr_t slide,
                                 struct rebinding bindings[],
                                 size_t bindings_count);
