/*
 *  exit support for qemu
 *
 *  Copyright (c) 2018 Alex Bennée <alex.bennee@linaro.org>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, see <http://www.gnu.org/licenses/>.
 */
#include "qemu/osdep.h"
#include "accel/tcg/perf.h"
#include "gdbstub/syscalls.h"
#include "qemu.h"
#include "user-internals.h"
#include "qemu/plugin.h"

void signal_exit(void);

#ifdef CONFIG_GCOV
extern void __gcov_dump(void);
#endif

static inline void log_unlock_guard (FILE** file)
{
    qemu_log_unlock(*file);
}

void preexit_cleanup(CPUArchState *env, int code)
{
#ifdef CONFIG_GCOV
        __gcov_dump();
#endif
        gdb_exit(code);
        qemu_plugin_user_exit();
        perf_exit();
        signal_exit();

        CPUState* cpu;
        CPU_FOREACH(cpu)
        {
            add_exclusive_accesses(cpu->neg.exclusive_accesses);
            add_shared_accesses(cpu->neg.shared_accesses);
            if (qemu_loglevel_mask(CPU_LOG_ACCESSES))
            {
                FILE* f __attribute__((cleanup(log_unlock_guard))) = qemu_log_trylock();
                fprintf(f, "Thread %lu exited with shared/exclusive accesses "TARGET_FMT_lu"/"TARGET_FMT_lu"\n",
                        cpu->neg.thread_tag_id, cpu->neg.shared_accesses, cpu->neg.exclusive_accesses);
            }
        }

        if (qemu_loglevel_mask(CPU_LOG_ACCESSES))
        {
            FILE *f __attribute__((cleanup(log_unlock_guard))) = qemu_log_trylock();
            fprintf(
                f, "Program exited with shared/exclusive accesses "TARGET_FMT_lu"/"TARGET_FMT_lu
                " (%.2f%% shared accesses)\n",
                get_shared_accesses(), get_exclusive_accesses(),
                100.0 * (double)get_shared_accesses() / (double)(get_shared_accesses() + get_exclusive_accesses()));

        }
}
