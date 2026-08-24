#include <stdio.h>
#include <libjbc.h>

static jbc_cred g_orig_cred;
static jbc_cred g_root_cred;
static int g_bypassed = 0;

static int can_write_root(void)
{
    FILE *fp = fopen("/user/.sbtest", "w");
    if (!fp) return 0;
    fclose(fp);
    remove("/user/.sbtest");
    return 1;
}

int sandbox_bypass(void)
{
    if (g_bypassed) return 1;

    if (can_write_root()) {
        g_bypassed = 1;
        return 1;
    }

    if (jbc_get_cred(&g_orig_cred) != 0)
        return 0;

    g_root_cred = g_orig_cred;
    jbc_jailbreak_cred(&g_root_cred);

    if (jbc_set_cred(&g_root_cred) != 0)
        return 0;

    g_bypassed = can_write_root();
    return g_bypassed;
}

void sandbox_restore(void)
{
    if (!g_bypassed) return;
    jbc_set_cred(&g_orig_cred);
    g_bypassed = 0;
}
