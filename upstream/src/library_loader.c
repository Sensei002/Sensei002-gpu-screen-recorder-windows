#include "../include/library_loader.h"
#include "../include/log.h"

#include <dlfcn.h>
#include <stdbool.h>

void* dlsym_print_fail(void *handle, const char *name, bool required) {
    dlerror();
    void *sym = dlsym(handle, name);
    char *err_str = dlerror();

    if(!sym)
        gsr_log(required ? GSR_LOG_LEVEL_ERROR : GSR_LOG_LEVEL_WARNING, "dlsym(handle, \"%s\") failed, error: %s", name, err_str ? err_str : "(null)");

    return sym;
}

/* |dlsyms| should be null terminated */
bool dlsym_load_list(void *handle, const dlsym_assign *dlsyms) {
    bool success = true;
    for(int i = 0; dlsyms[i].func; ++i) {
        *dlsyms[i].func = dlsym_print_fail(handle, dlsyms[i].name, true);
        if(!*dlsyms[i].func)
            success = false;
    }
    return success;
}

/* |dlsyms| should be null terminated */
void dlsym_load_list_optional(void *handle, const dlsym_assign *dlsyms) {
    for(int i = 0; dlsyms[i].func; ++i) {
        *dlsyms[i].func = dlsym_print_fail(handle, dlsyms[i].name, false);
    }
}
