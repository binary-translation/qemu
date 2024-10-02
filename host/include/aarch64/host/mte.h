//
// Created by simon on 18.09.24.
//

#ifndef ARM_MTE_H
#define ARM_MTE_H

#include "host/cpuinfo.h"
#include <stdbool.h>
#include <arm_acle.h>

#ifdef __ARM_FEATURE_MEMORY_TAGGING
# define HAVE_MTE  true
#else
# define HAVE_MTE  likely(cpuinfo & CPUINFO_MTE)
#endif
#if !defined(__ARM_FEATURE_MEMORY_TAGGING)
# define ATTR_MTE  __attribute__((target("+memtag")))
#else
# define ATTR_MTE
#endif

static inline void ATTR_MTE
mte_set_tag(void* tagged_address) {
    __arm_mte_set_tag(tagged_address);
}

static inline void ATTR_MTE
enable_tag_check(void) {
    if (cpuinfo & CPUINFO_MTE)
    {
        __arm_wsr("TCO", 0);
    }
}
static inline void ATTR_MTE
disable_tag_check(void) {
    if (cpuinfo & CPUINFO_MTE)
    {
        __arm_wsr("TCO", 1);
    }
}

#endif //ARM_MTE_H
