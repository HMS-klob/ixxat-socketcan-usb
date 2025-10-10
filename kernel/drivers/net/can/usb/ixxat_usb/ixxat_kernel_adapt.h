/* SPDX-License-Identifier: GPL-2.0 */
/* CAN driver base for IXXAT USB-to-CAN
 *
 * Copyright (C) 2018-2024 HMS Industrial Networks <socketcan@hms-networks.de>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published
 * by the Free Software Foundation; version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 */
#ifndef IXXAT_LNX_KERNEL_ADAPT_H
#define IXXAT_LNX_KERNEL_ADAPT_H

#include <linux/version.h>

/* IX_SYNCTOHOSTCLOCK_xxx controls how timestamps are sync'ed between host and
 * device:
 *
 * _NONE	do not sync to host clock, device start is timestamp zero
 * _BEFORESTART	sync to host clock before start command is issued
 * _AFTERSTART	sync to host clock after start command returned
 * _ONSTART	sync to host clock on start command, middle between start cmd
 * 		issued and cmd returned
 *
 * The list is given in order of priority, meaning that if (for example)
 * _BEFORESTART and _AFTERSTART are defined, then _BEFORESTART will take
 * precedence.
 *
 * IX_SYNCTOHOSTCLOCK_NONE is the Linux kernel in-tree driver variant default.
 */
#define IX_SYNCTOHOSTCLOCK_NONE

/* Exact statistics means that all messages are sent with active self reception  * (overhead) so that the statistic counters are incremented after the message
 * was really written on the CAN bus. Otherwise the counters are incremented
 * upon acknowledgment of the USB packet containing the frame by the kernel's
 * USB subsystem.
 *
 * CL1 firmwares can't do exact statistics.
 *
 * IX_STATISTICS_EXACT is not defined in the Linux kernel in-tree driver variant
 */
#undef IX_STATISTICS_EXACT

#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 11, 0)
#define can_fd_dlc2len(dlc)			can_dlc2len(get_canfd_dlc(dlc))
#define can_fd_len2dlc(dlc)			can_len2dlc(dlc)
#define can_cc_dlc2len(dlc)			get_can_dlc(dlc)
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 12, 0)
#define can_get_echo_skb(dev, idx, pLen)	can_get_echo_skb(dev, idx)
#define can_put_echo_skb(skb, dev, idx, len)	can_put_echo_skb(skb, dev, idx)
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 13, 0)
#define can_free_echo_skb(dev, idx, pLen)	can_free_echo_skb(dev, idx)
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 19, 0)
#define netif_napi_add_weight(dev, napi, poll, wait)	netif_napi_add(dev, napi, poll, wait)
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 1, 0)
#define can_dev_dropped_skb(netdev, skb)	can_dropped_invalid_skb(netdev, skb)
#endif

#endif
