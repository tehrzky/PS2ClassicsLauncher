#include "syscalls.h"
#include <unistd.h>

int ps4_load_prx(const char *path, int *mod_id) {
    return (int)syscall(594, path, 0, mod_id, 0);
}

int ps4_dlsym(int mod_id, const char *symbol, void **addr) {
    return (int)syscall(591, (long)mod_id, symbol, addr);
}