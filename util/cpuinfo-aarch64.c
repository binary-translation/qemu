/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Host specific cpu identification for AArch64.
 */

#include "qemu/osdep.h"
#include "host/cpuinfo.h"

#ifdef CONFIG_LINUX
# ifdef CONFIG_GETAUXVAL
#  include <sys/auxv.h>
# else
#  include <asm/hwcap.h>
#  include "elf.h"
# endif
# ifndef HWCAP2_BTI
#  define HWCAP2_BTI 0  /* added in glibc 2.32 */
# endif
#endif
#ifdef CONFIG_DARWIN
# include <sys/sysctl.h>
#endif

unsigned cpuinfo;

#ifdef CONFIG_DARWIN
static bool sysctl_for_bool(const char *name)
{
    int val = 0;
    size_t len = sizeof(val);

    if (sysctlbyname(name, &val, &len, NULL, 0) == 0) {
        return val != 0;
    }

    /*
     * We might in the future ask for properties not present in older kernels,
     * but we're only asking about static properties, all of which should be
     * 'int'.  So we shouldn't see ENOMEM (val too small), or any of the other
     * more exotic errors.
     */
    assert(errno == ENOENT);
    return false;
}
#endif

/* Called both as constructor and (possibly) via other constructors. */
unsigned __attribute__((constructor)) cpuinfo_init(void)
{
    unsigned info = cpuinfo;

    if (info) {
        return info;
    }

    info = CPUINFO_ALWAYS;

#ifdef CONFIG_LINUX
    unsigned long hwcap = qemu_getauxval(AT_HWCAP);
    info |= (hwcap & HWCAP_ATOMICS ? CPUINFO_LSE : 0);
    info |= (hwcap & HWCAP_USCAT ? CPUINFO_LSE2 : 0);
    info |= (hwcap & HWCAP_AES ? CPUINFO_AES : 0);
    info |= (hwcap & HWCAP_PMULL ? CPUINFO_PMULL : 0);

    unsigned long hwcap2 = qemu_getauxval(AT_HWCAP2);
    info |= (hwcap2 & HWCAP2_BTI ? CPUINFO_BTI : 0);
    info |= (hwcap2 & HWCAP2_MTE ? CPUINFO_MTE : 0);
#endif
#ifdef CONFIG_DARWIN
    info |= sysctl_for_bool("hw.optional.arm.FEAT_LSE") * CPUINFO_LSE;
    info |= sysctl_for_bool("hw.optional.arm.FEAT_LSE2") * CPUINFO_LSE2;
    info |= sysctl_for_bool("hw.optional.arm.FEAT_AES") * CPUINFO_AES;
    info |= sysctl_for_bool("hw.optional.arm.FEAT_PMULL") * CPUINFO_PMULL;
    info |= sysctl_for_bool("hw.optional.arm.FEAT_BTI") * CPUINFO_BTI;
#endif

    cpuinfo = info;
    return info;
}
