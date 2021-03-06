/*
 * Copyright (c) 2010, Atheros Communications Inc.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <linux/kernel.h>
#include <linux/version.h>
#include <linux/module.h>
#include <linux/workqueue.h>

#include "adf_os_defer_pvt.h"

void 
__adf_os_defer_func(struct work_struct *work)
{
#if LINUX_VERSION_CODE <= KERNEL_VERSION(2,6,19)
    return;
#else
    __adf_os_work_t  *ctx = container_of(work, __adf_os_work_t, work);
    if (ctx->fn == NULL)
    {
        printk("BugCheck: Callback is not initilized while creating work queue\n");
        return;
    }
    ctx->fn(ctx->arg);
#endif
}

void 
__adf_os_defer_delayed_func(struct work_struct *dwork)
{
#if LINUX_VERSION_CODE <= KERNEL_VERSION(2,6,19)
    return;
#else
    __adf_os_delayed_work_t  *ctx = container_of(dwork, __adf_os_delayed_work_t, dwork.work);
    if (ctx->fn == NULL)
    {
        printk("BugCheck: Callback is not initilized while creating delayed work queue\n");
        return;
    }
    ctx->fn(ctx->arg);
#endif

}
EXPORT_SYMBOL(__adf_os_defer_func);
EXPORT_SYMBOL(__adf_os_defer_delayed_func);

