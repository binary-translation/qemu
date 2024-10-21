//
// Created by simon on 18.09.24.
//

#ifndef HOST_MTE_H
#define HOST_MTE_H

#include <stdint.h>

static inline void mte_set_tag(void* tagged_address) {}

static inline void enable_tag_check(void) {}
static inline void disable_tag_check(void) {}
void mte_set_tag_range(uint64_t start, uint64_t end, uint8_t tag);

#endif //HOST_MTE_H
