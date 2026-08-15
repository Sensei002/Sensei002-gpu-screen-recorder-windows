#ifndef GSR_LIBRARY_LOADER_H
#define GSR_LIBRARY_LOADER_H

#include <stdbool.h>

typedef struct {
    void **func;
    const char *name;
} dlsym_assign;

void* dlsym_print_fail(void *handle, const char *name, bool required);
/* |dlsyms| should be null terminated */
bool dlsym_load_list(void *handle, const dlsym_assign *dlsyms);
/* |dlsyms| should be null terminated */
void dlsym_load_list_optional(void *handle, const dlsym_assign *dlsyms);

#endif /* GSR_LIBRARY_LOADER_H */
