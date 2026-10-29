/*
 * Dynamic sync control
 *
 * While the screen is on, userspace fsync()/fdatasync()/sync_file_range()
 * become no-ops and dirty data is written back by the regular periodic
 * writeback instead. When the screen turns off (early suspend), or the
 * system is rebooting/powering off, everything is flushed and real fsync
 * semantics are restored until the screen is turned on again.
 *
 * This removes the vast majority of small synchronous writes (and the
 * matching jbd2 journal commits + eMMC cache flushes) that Android
 * generates, which noticeably reduces eMMC wear and I/O latency.
 *
 * Based on the idea of "Dynamic fsync" by Paul Reioux (faux123).
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#ifndef _LINUX_DYN_SYNC_CNTRL_H
#define _LINUX_DYN_SYNC_CNTRL_H

#include <linux/types.h>
#include <linux/compiler.h>

#ifdef CONFIG_DYNAMIC_FSYNC
extern bool dyn_fsync_active;
extern bool dyn_fsync_suspended;

/*
 * Returns true when a userspace-requested sync may be skipped.
 * Kernel-internal users of vfs_fsync()/vfs_fsync_range() are never
 * affected, only the syscall entry points check this.
 */
static inline bool dyn_fsync_bypass(void)
{
	return likely(ACCESS_ONCE(dyn_fsync_active) &&
		      !ACCESS_ONCE(dyn_fsync_suspended));
}
#else
static inline bool dyn_fsync_bypass(void)
{
	return false;
}
#endif

#endif /* _LINUX_DYN_SYNC_CNTRL_H */
