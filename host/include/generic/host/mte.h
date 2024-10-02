//
// Created by simon on 18.09.24.
//

#ifndef HOST_MTE_H
#define HOST_MTE_H

#include <stdbool.h>

static inline void mte_set_tag(void* tagged_address) {}

static inline void enable_tag_check(bool b) {}
static inline void disable_tag_check(bool b) {}

#endif //HOST_MTE_H
