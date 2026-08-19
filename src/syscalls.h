#ifndef SYSCALLS_H
#define SYSCALLS_H

int ps4_load_prx(const char *path, int *mod_id);
int ps4_dlsym(int mod_id, const char *symbol, void **addr);

#endif