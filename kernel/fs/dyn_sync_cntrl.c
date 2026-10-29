/*
 * Dynamic sync control driver
 *
 * Author: Paul Reioux aka Faux123 <reioux@gmail.com>
 * Adapted and optimized for MT6582 by Alex Mihalenok
 *
 * Copyright 2013-2014 Paul Reioux
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/kobject.h>
#include <linux/sysfs.h>
#include <linux/earlysuspend.h>
#include <linux/mutex.h>
#include <linux/notifier.h>
#include <linux/reboot.h>
#include <linux/writeback.h>
#include <linux/fs.h>
#include <linux/dyn_sync_cntrl.h>

#define DYN_FSYNC_VERSION_MAJOR 1
#define DYN_FSYNC_VERSION_MINOR 2

static DEFINE_MUTEX(fsync_mutex);

bool dyn_fsync_suspended __read_mostly = false;
EXPORT_SYMBOL(dyn_fsync_suspended);

bool dyn_fsync_active __read_mostly = true;
EXPORT_SYMBOL(dyn_fsync_active);

static void dyn_fsync_force_flush(void)
{
	/* Flush all dirty inodes, superblocks and block devices */
	sync_filesystems(0);
	sync_filesystems(1);
}

static ssize_t dyn_fsync_active_show(struct kobject *kobj,
		struct kobj_attribute *attr, char *buf)
{
	return sprintf(buf, "%u\n", (dyn_fsync_active ? 1 : 0));
}

static ssize_t dyn_fsync_active_store(struct kobject *kobj,
		struct kobj_attribute *attr, const char *buf, size_t count)
{
	unsigned int val;

	if (sscanf(buf, "%u\n", &val) == 1) {
		if (val == 1) {
			pr_info("dyn_fsync: dynamic fsync enabled\n");
			dyn_fsync_active = true;
		} else if (val == 0) {
			pr_info("dyn_fsync: dynamic fsync disabled\n");
			dyn_fsync_active = false;
			dyn_fsync_force_flush();
		} else {
			pr_info("dyn_fsync: invalid value: %u\n", val);
		}
	}

	return count;
}

static ssize_t dyn_fsync_version_show(struct kobject *kobj,
		struct kobj_attribute *attr, char *buf)
{
	return sprintf(buf, "version: %u.%u\n",
		       DYN_FSYNC_VERSION_MAJOR, DYN_FSYNC_VERSION_MINOR);
}

static ssize_t dyn_fsync_earlysuspend_show(struct kobject *kobj,
		struct kobj_attribute *attr, char *buf)
{
	return sprintf(buf, "early suspend active: %u\n",
		       dyn_fsync_suspended ? 1 : 0);
}

static struct kobj_attribute dyn_fsync_active_attr =
	__ATTR(Dyn_fsync_active, 0644,
	       dyn_fsync_active_show, dyn_fsync_active_store);

static struct kobj_attribute dyn_fsync_version_attr =
	__ATTR(Dyn_fsync_version, 0444,
	       dyn_fsync_version_show, NULL);

static struct kobj_attribute dyn_fsync_earlysuspend_attr =
	__ATTR(Dyn_fsync_earlysuspend, 0444,
	       dyn_fsync_earlysuspend_show, NULL);

static struct attribute *dyn_fsync_attrs[] = {
	&dyn_fsync_active_attr.attr,
	&dyn_fsync_version_attr.attr,
	&dyn_fsync_earlysuspend_attr.attr,
	NULL,
};

static struct attribute_group dyn_fsync_attr_group = {
	.attrs = dyn_fsync_attrs,
};

static struct kobject *dyn_fsync_kobj;

static void dyn_fsync_early_suspend(struct early_suspend *h)
{
	mutex_lock(&fsync_mutex);
	if (dyn_fsync_active) {
		dyn_fsync_suspended = true;
		dyn_fsync_force_flush();
	}
	mutex_unlock(&fsync_mutex);
}

static void dyn_fsync_late_resume(struct early_suspend *h)
{
	mutex_lock(&fsync_mutex);
	dyn_fsync_suspended = false;
	mutex_unlock(&fsync_mutex);
}

static struct early_suspend dyn_fsync_early_suspend_handler = {
	.level   = EARLY_SUSPEND_LEVEL_BLANK_SCREEN,
	.suspend = dyn_fsync_early_suspend,
	.resume  = dyn_fsync_late_resume,
};

static int dyn_fsync_panic_event(struct notifier_block *this,
		unsigned long event, void *ptr)
{
	dyn_fsync_suspended = true;
	dyn_fsync_force_flush();
	return NOTIFY_DONE;
}

static struct notifier_block dyn_fsync_panic_block = {
	.notifier_call = dyn_fsync_panic_event,
	.priority      = INT_MAX,
};

static int dyn_fsync_notify_sys(struct notifier_block *this,
		unsigned long code, void *unused)
{
	if (code == SYS_DOWN || code == SYS_HALT || code == SYS_POWER_OFF) {
		dyn_fsync_suspended = true;
		dyn_fsync_force_flush();
	}
	return NOTIFY_DONE;
}

static struct notifier_block dyn_fsync_notifier = {
	.notifier_call = dyn_fsync_notify_sys,
};

static int __init dyn_fsync_init(void)
{
	int ret;

	register_early_suspend(&dyn_fsync_early_suspend_handler);
	register_reboot_notifier(&dyn_fsync_notifier);
	atomic_notifier_chain_register(&panic_notifier_list,
				       &dyn_fsync_panic_block);

	dyn_fsync_kobj = kobject_create_and_add("dyn_fsync", kernel_kobj);
	if (!dyn_fsync_kobj) {
		pr_err("dyn_fsync: kobject create failed\n");
		return -ENOMEM;
	}

	ret = sysfs_create_group(dyn_fsync_kobj, &dyn_fsync_attr_group);
	if (ret) {
		pr_err("dyn_fsync: sysfs create failed\n");
		kobject_put(dyn_fsync_kobj);
		dyn_fsync_kobj = NULL;
	}

	pr_info("dyn_fsync: initialized (version %u.%u)\n",
		DYN_FSYNC_VERSION_MAJOR, DYN_FSYNC_VERSION_MINOR);
	return 0;
}

static void __exit dyn_fsync_exit(void)
{
	unregister_early_suspend(&dyn_fsync_early_suspend_handler);
	unregister_reboot_notifier(&dyn_fsync_notifier);
	atomic_notifier_chain_unregister(&panic_notifier_list,
					 &dyn_fsync_panic_block);

	if (dyn_fsync_kobj)
		kobject_put(dyn_fsync_kobj);
}

module_init(dyn_fsync_init);
module_exit(dyn_fsync_exit);
MODULE_AUTHOR("Paul Reioux <reioux@gmail.com>");
MODULE_DESCRIPTION("Dynamic fsync control for Android earlysuspend");
MODULE_LICENSE("GPL v2");
