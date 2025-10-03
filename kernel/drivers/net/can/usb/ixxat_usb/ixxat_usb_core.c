// SPDX-License-Identifier: GPL-2.0
/* CAN driver for IXXAT USB-to-CAN
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

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/netdevice.h>
#include <linux/net_tstamp.h>
#include <linux/can/dev.h>
#include <linux/kthread.h>
#include <linux/usb.h>
#include <linux/version.h>

#include "ixxat_usb_core.h"

#include "ixxat_kernel_adapt.h"

#ifdef DEBUG
#define IXXAT_DEBUG
#endif

MODULE_AUTHOR("HMS Technology Center GmbH <socketcan@hms-networks.de>");
MODULE_DESCRIPTION("SocketCAN driver for HMS Ixxat USB-to-CAN V2, USB-to-CAN-FD family adapters");
MODULE_LICENSE("GPL v2");

/* Prefix for debug output - makes for easier grepping */
#define IX_DRIVER_TAG "ix_usb_can: "

/* IX_SYNCTOHOSTCLOCK controls how timestamps are sync'ed between host and
 * device:
 *
 * 0	do not sync to host clock, device start is timestamp zero
 * 1	sync to host clock before start command is issued
 * 2	sync to host clock after start command returned
 * 3	sync to host clock on start command, middle between start command
 * 	issued and command returned
 *
 * SGr Note: in the mainline version, one method should be selected once for all
 */
#define IX_SYNCTOHOST_NONE		0
#define IX_SYNCTOHOST_BEFORESTART	1
#define IX_SYNCTOHOST_AFTERSTART	2
#define IX_SYNCTOHOST_ONSTART		3

/* default sync to host clock setting, see above */
#define IX_SYNCTOHOSTCLOCK		IX_SYNCTOHOST_NONE

#define IX_STATISTICS_EXACT		0

/* minimum firmware version that supports V2 communication layer */
#define IX_MIN_MAJORFWVERSION_SUPP_V2	0x01
#define IX_MIN_MINORFWVERSION_SUPP_V2	0x07
#define IX_MIN_BUILDFWVERSION_SUPP_V2	0x00

#define IX_FW_VER(a, b, c)	(((a) << 16) + ((b) << 8) + (c))
#define IX_FW_CL2		IX_FW_VER(IX_MIN_MAJORFWVERSION_SUPP_V2, \
					  IX_MIN_MINORFWVERSION_SUPP_V2, \
					  IX_MIN_MINORFWVERSION_SUPP_V2)

#if defined(CONFIG_TRACING) && defined(DEBUG)
#define ix_trace_printk(...)		trace_printk(__VA_ARGS__)
#elif defined(IXXAT_DEBUG)
#define ix_trace_printk(...)		pr_info(__VA_ARGS__)
#else
#define ix_trace_printk(...)
#endif
/* struct ixxat_driver_info IXXAT USB device static information
 * @name	commercial name
 * @adapter	IXXAT USB adapter family
 * @is_legacy	Legacy USB device
 */
struct ixxat_driver_info {
	const char *name;
	const struct ixxat_usb_adapter *adapter;
	bool is_legacy;
};

/* IXXAT_USB_VENDOR_ID_LEGACY products information */
static const struct ixxat_driver_info legacy_usb2can_compact = {
	.name = "IXXAT USB Compact",
	.adapter = &usb2can_cl1,
	.is_legacy = true,
};
static const struct ixxat_driver_info legacy_usb2can_embedded = {
	.name = "IXXAT USB Embedded",
	.adapter = &usb2can_cl1,
	.is_legacy = true,
};
static const struct ixxat_driver_info legacy_usb2can_pro = {
	.name = "IXXAT USB Professional",
	.adapter = &usb2can_cl1,
	.is_legacy = true,
};
static const struct ixxat_driver_info legacy_usb2can_auto = {
	.name = "IXXAT USB Automotive",
	.adapter = &usb2can_cl1,
	.is_legacy = true,
};
static const struct ixxat_driver_info legacy_usb2can_plugin = {
	.name = "IXXAT USB Plugin",
	.adapter = &usb2can_cl1,
	.is_legacy = true,
};
static const struct ixxat_driver_info legacy_usb2can_fd_compact = {
	.name = "IXXAT USB Compact FD",
	.adapter = &usb2can_fd,
};
static const struct ixxat_driver_info legacy_usb2can_fd_pro = {
	.name = "IXXAT USB Professional FD",
	.adapter = &usb2can_fd,
};
static const struct ixxat_driver_info legacy_usb2can_fd_auto = {
	.name = "IXXAT USB Automotive FD",
	.adapter = &usb2can_fd,
};
static const struct ixxat_driver_info legacy_usb2can_fd_pcie_mini = {
	.name = "IXXAT USB PCIE Mini FD",
	.adapter = &usb2can_fd,
};
static const struct ixxat_driver_info legacy_usb2car = {
	.name = "IXXAT USB-to-Car",
	.adapter = &usb2can_fd,
};
static const struct ixxat_driver_info legacy_can_idm101 = {
	.name = "IXXAT IDM 101",
	.adapter = &can_fd_idm,
};
static const struct ixxat_driver_info legacy_can_idm200 = {
	.name = "IXXAT IDM 200",
	.adapter = &can_fd_idm,
};

/* IXXAT_USB_VENDOR_ID products information */
static const struct ixxat_driver_info usb2can_fd_pro = {
	.name = "Ixxat USB-to-CAN/FD Pro",
	.adapter = &usb2can_fd,
};
static const struct ixxat_driver_info usb2can_fd_std = {
	.name = "Ixxat USB-to-CAN/FD Standard",
	.adapter = &usb2can_fd,
};
static const struct ixxat_driver_info usb2can_fd_std_card = {
	.name = "Ixxat USB-to-CAN/FD Standard Card",
	.adapter = &usb2can_fd,
};
static const struct ixxat_driver_info usb2can_fd_pro_module = {
	.name = "Ixxat USB-to-CAN/FD Pro Module",
	.adapter = &usb2can_fd,
};
static const struct ixxat_driver_info usb2can_fd_standard_module = {
	.name = "Ixxat USB-to-CAN/FD Standard Module",
	.adapter = &usb2can_fd,
};

/* Table of devices that work with this driver */
static const struct usb_device_id ixxat_usb_table[] = {
	{ USB_DEVICE(IXXAT_USB_VENDOR_ID_LEGACY, USB2CAN_COMPACT_PRODUCT_ID),
	  .driver_info = (kernel_ulong_t)&legacy_usb2can_compact,
	},
	{ USB_DEVICE(IXXAT_USB_VENDOR_ID_LEGACY, USB2CAN_EMBEDDED_PRODUCT_ID),
	  .driver_info = (kernel_ulong_t)&legacy_usb2can_embedded,
	},
	{ USB_DEVICE(IXXAT_USB_VENDOR_ID_LEGACY,
		     USB2CAN_PROFESSIONAL_PRODUCT_ID),
	  .driver_info = (kernel_ulong_t)&legacy_usb2can_pro,
	},
	{ USB_DEVICE(IXXAT_USB_VENDOR_ID_LEGACY, USB2CAN_AUTOMOTIVE_PRODUCT_ID),
	  .driver_info = (kernel_ulong_t)&legacy_usb2can_auto,
	},
	{ USB_DEVICE(IXXAT_USB_VENDOR_ID_LEGACY, USB2CAN_PLUGIN_PRODUCT_ID),
	  .driver_info = (kernel_ulong_t)&legacy_usb2can_plugin,
	},
	{ USB_DEVICE(IXXAT_USB_VENDOR_ID_LEGACY, USB2CAN_FD_COMPACT_PRODUCT_ID),
	  .driver_info = (kernel_ulong_t)&legacy_usb2can_fd_compact,
	},
	{ USB_DEVICE(IXXAT_USB_VENDOR_ID_LEGACY,
		     USB2CAN_FD_PROFESSIONAL_PRODUCT_ID),
	  .driver_info = (kernel_ulong_t)&legacy_usb2can_fd_pro,
	},
	{ USB_DEVICE(IXXAT_USB_VENDOR_ID_LEGACY,
		     USB2CAN_FD_AUTOMOTIVE_PRODUCT_ID),
	  .driver_info = (kernel_ulong_t)&legacy_usb2can_fd_auto,
	},
	{ USB_DEVICE(IXXAT_USB_VENDOR_ID_LEGACY,
		     USB2CAN_FD_PCIE_MINI_PRODUCT_ID),
	  .driver_info = (kernel_ulong_t)&legacy_usb2can_fd_pcie_mini,
	},
	{ USB_DEVICE(IXXAT_USB_VENDOR_ID_LEGACY, USB2CAR_PRODUCT_ID),
	  .driver_info = (kernel_ulong_t)&legacy_usb2car,
	},
	{ USB_DEVICE(IXXAT_USB_VENDOR_ID_LEGACY, CAN_IDM101_PRODUCT_ID),
	  .driver_info = (kernel_ulong_t)&legacy_can_idm101,
	},
	{ USB_DEVICE(IXXAT_USB_VENDOR_ID_LEGACY, CAN_IDM200_PRODUCT_ID),
	  .driver_info = (kernel_ulong_t)&legacy_can_idm200,
	},
	{ USB_DEVICE(IXXAT_USB_VENDOR_ID, USB2CAN_FD_PRO_PRODUCT_ID),
	  .driver_info = (kernel_ulong_t)&usb2can_fd_pro,
	},
	{ USB_DEVICE(IXXAT_USB_VENDOR_ID, USB2CAN_FD_STANDARD_PRODUCT_ID),
	  .driver_info = (kernel_ulong_t)&usb2can_fd_std,
	},
	{ USB_DEVICE(IXXAT_USB_VENDOR_ID, USB2CAN_FD_STANDARD_CARD_PRODUCT_ID),
	  .driver_info = (kernel_ulong_t)&usb2can_fd_std_card,
	},
	{ USB_DEVICE(IXXAT_USB_VENDOR_ID, USB2CAN_FD_PRO_MODULE_PRODUCT_ID),
	  .driver_info = (kernel_ulong_t)&usb2can_fd_pro_module,
	},
	{ USB_DEVICE(IXXAT_USB_VENDOR_ID,
		     USB2CAN_FD_STANDARD_MODULE_PRODUCT_ID),
	  .driver_info = (kernel_ulong_t)&usb2can_fd_standard_module,
	},
	{ } /* Terminating entry */
};

MODULE_DEVICE_TABLE(usb, ixxat_usb_table);

#ifdef IXXAT_DEBUG
static void showdevcaps(struct ixxat_dev_caps *dev_caps)
{
	int i, bus_ctrl_count = le16_to_cpu(dev_caps->bus_ctrl_count);

	ix_trace_printk(KBUILD_MODNAME ": CtrlCount = %d\n", bus_ctrl_count);
	for (i = 0; i < bus_ctrl_count; i++)
		ix_trace_printk(KBUILD_MODNAME ": Type = %d\n",
				dev_caps->bus_ctrl_types[i]);
}

static void showdump(u8 *pbdata, u16 length)
{
	char dump[100] = "Dump: ";
	int i, l = strlen(dump);
	int len = (length > 25) ? 25 : length;

	/* SGr: uses snprintf() to control the overflow of the local variable
	 * and simplify the use of the strcat/sprintf combination
	 */
	for (i = 0; i < len; i++)
		l += snprintf(dump + l, sizeof(dump) - l, "%02x ", pbdata[i]);

	dump[l-1] = '\n';

	ix_trace_printk(KBUILD_MODNAME ": %s", dump);
}
#endif

/* ixxat_usb_dev_name - return the name of the IXXAT USB device
 * @param id: pointer to the USB device ID structure
 *
 * This function returns the name of the IXXAT USB device.
 * It is used to identify the device in logs and user interfaces.
 *
 * Returns the name of the device as a string.
 */
static const char *ixxat_usb_dev_name(const struct usb_device_id *id)
{
	const struct ixxat_driver_info *drv_info =
		(const struct ixxat_driver_info *)id->driver_info;

	return drv_info->name;
}

/* ixxat_usb_is_legacy_usb2can - check if device is a legacy USB2CAN device
 * @id: USB device id
 *
 * Returns true if the device is a legacy USB2CAN device.
 */
static bool ixxat_usb_is_legacy_usb2can(const struct usb_device_id *id)
{
	const struct ixxat_driver_info *drv_info =
		(const struct ixxat_driver_info *)id->driver_info;

	return drv_info->is_legacy;
}

/* ixxat_usb_has_cl2_firmware - check if device has CL2 firmware
 * @id: USB device id
 * @fwinfo: Firmware info of the device (may be NULL)
 *
 * Returns != 0 if the device has CL2 firmware, 0 otherwise.
 */
static int ixxat_usb_has_cl2_firmware(const struct usb_device_id *id,
				      struct ixxat_fw_info2 *fwinfo)
{
	if (ixxat_usb_is_legacy_usb2can(id) && fwinfo) {
		int major = le16_to_cpu(fwinfo->major_version);
		int minor = le16_to_cpu(fwinfo->minor_version);
		int build = le16_to_cpu(fwinfo->build_version);

		return IX_FW_VER(major, minor, build) >= IX_FW_CL2;
	}

	return 0;
}

/* ixxat_usb_get_adapter - get the IXXAT USB adapter based on the USB device ID
 * @id: pointer to the USB device ID structure
 * @dev_fwinfo: pointer to the firmware info structure (optional)
 *
 * This function retrieves the appropriate IXXAT USB adapter
 * based on the USB device ID and the firmware information.
 *
 * Returns a pointer to the IXXAT USB adapter structure
 */
static const struct ixxat_usb_adapter *
	ixxat_usb_get_adapter(const struct usb_device_id *id,
			      struct ixxat_fw_info2 *dev_fwinfo)
{
	const struct ixxat_driver_info *drv_info =
		(const struct ixxat_driver_info *)id->driver_info;

	if ((drv_info->adapter == &usb2can_cl1) &&
	    (ixxat_usb_has_cl2_firmware(id, dev_fwinfo)))
		return &usb2can_v2;

	return drv_info->adapter;
}

/* ixxat_usb_needs_firmware_update - check if firmware update is needed
 * @id: USB device id
 * @fwinfo: Firmware info of the device
 *
 * Returns != 0 if firmware update is recommended, 0 otherwise.
 */
static int ixxat_usb_needs_firmware_update(const struct usb_device_id *id,
					   struct ixxat_fw_info2 *fwinfo)
{
	/* firmware update is recomended for devices with cl1 firmware */
	return (ixxat_usb_is_legacy_usb2can(id)) ?
		!ixxat_usb_has_cl2_firmware(id, fwinfo) : 0;
}

/* ixxat_usb_get_tx_context - get a free URB context for transmission
 * @dev: pointer to the IXXAT USB CAN device
 *
 * Returns a pointer to a free URB context or NULL if no context is available.
 */
static struct ixxat_tx_urb_context *
	ixxat_usb_get_tx_context(struct ixxat_usb_candevice *dev)
{
	struct ixxat_tx_urb_context *context = NULL;
	unsigned long flags;
	int i;

	spin_lock_irqsave(&dev->dev_lock, flags);

	for (i = 0; i < IXXAT_USB_MAX_TX_URBS; i++) {
		/* is urb allocated and free */
		if ((dev->tx_contexts[i].urb) &&
		    (dev->tx_contexts[i].urb_index == IXXAT_USB_FREE_ENTRY)) {
			context = &dev->tx_contexts[i];
			context->urb_index = i;
			break;
		}
	}

	spin_unlock_irqrestore(&dev->dev_lock, flags);

	return context;
}

/* ixxat_usb_rel_tx_context - release a URB context
 * @dev: pointer to the IXXAT USB CAN device
 * @context: pointer to the URB context to release
 * This function releases the URB context by setting its index to
 * IXXAT_USB_FREE_ENTRY
 */
static void ixxat_usb_rel_tx_context(struct ixxat_usb_candevice *dev,
				     struct ixxat_tx_urb_context *context)
{
	if (context) {
		unsigned long flags;

		spin_lock_irqsave(&dev->dev_lock, flags);

		context->urb_index = IXXAT_USB_FREE_ENTRY;

		spin_unlock_irqrestore(&dev->dev_lock, flags);
	}
}

/* ixxat_usb_msg_get_next_idx - get next free message index
 * @dev: pointer to the IXXAT USB CAN device
 *
 * Returns the next free message index or IXXAT_USB_E_FAILED
 * if no free index is available.
 */
static u32 ixxat_usb_msg_get_next_idx(struct ixxat_usb_candevice *dev)
{
	u32 msg_idx, msg_cnt;
	unsigned long flags;

	spin_lock_irqsave(&dev->dev_lock, flags);

	/* Note: dev::msg_max = 32 */
	msg_idx = (dev->msg_lastindex + 1) % dev->msg_max;

	for (msg_cnt = 0; msg_cnt < dev->msg_max; msg_cnt++) {
		u64 msg_mask = 1ULL << msg_idx;

		if (!(dev->msgs & msg_mask)) {
			dev->msgs |= msg_mask;
			dev->msg_lastindex = msg_idx;
			break;
		}

		msg_idx = (msg_idx + 1) % dev->msg_max;
	}

	spin_unlock_irqrestore(&dev->dev_lock, flags);

	return (msg_cnt < dev->msg_max) ? msg_idx : IXXAT_USB_E_FAILED;
}

/* ixxat_usb_msg_free_idx - free a message index
 * @dev: pointer to the IXXAT USB CAN device
 * @msg_idx: message index to free, if 0xFFFFFFFF all messages are freed
 */
static void ixxat_usb_msg_free_idx(struct ixxat_usb_candevice *dev, u32 msg_idx)
{
	unsigned long flags;

	spin_lock_irqsave(&dev->dev_lock, flags);

	if (msg_idx == 0xFFFFFFFF) {
		dev->msgs = 0;
		dev->msg_lastindex = 0;

	} else if (msg_idx < dev->msg_max) {
		dev->msgs &= ~(1ULL << msg_idx);
	}

	spin_unlock_irqrestore(&dev->dev_lock, flags);
}

/* ixxat_usb_setup_cmd - setup a command request and response
 * @req: pointer to the request structure to setup
 * @res: pointer to the response structure to setup
 */
void ixxat_usb_setup_cmd(struct ixxat_usb_dal_req *req,
			 struct ixxat_usb_dal_res *res)
{
	req->size = cpu_to_le32(sizeof(*req));
	req->port = cpu_to_le16(0xffff);
	req->socket = cpu_to_le16(0xffff);
	req->code = cpu_to_le32(0);

	res->res_size = cpu_to_le32(sizeof(*res));
	res->ret_size = cpu_to_le32(0);
	res->code = cpu_to_le32(0xffffffff);
}

/* ixxat_usb_send_cmd - send a command to the IXXAT USB device
 * @dev: pointer to the USB device
 * @port: port number to send the command to
 * @req: pointer to the request structure
 * @req_size: size of the request structure
 * @res: pointer to the response structure
 * @res_size: size of the response structure
 *
 * This function sends a command to the IXXAT USB device and waits for the
 * response. It retries the command up to IXXAT_USB_MAX_COM_REQ times if the
 * command fails to be sent or the response is not received.
 *
 * Returns >= 0 on success, negative error code on failure.
 * If the response size is wrong it returns -EBADMSG.
 */
int ixxat_usb_send_cmd(struct usb_device *dev, const u16 port, void *req,
		       const u16 req_size, void *res, const u16 res_size)
{
	struct ixxat_usb_dal_res *dal_res = res;
	int i, ret = 0, pos = 0;

	/* Send the command */
	for (i = 0; i < IXXAT_USB_MAX_COM_REQ; i++) {
		ret = usb_control_msg(dev, usb_sndctrlpipe(dev, 0), 0xff,
				      USB_TYPE_VENDOR | USB_DIR_OUT,
				      port, 0, req, req_size,
				      msecs_to_jiffies(IXXAT_USB_MSG_TIMEOUT));
		if (ret >= 0)
			break;

		if (ret != -ETIMEDOUT) {
			dev_err(&dev->dev, KBUILD_MODNAME
				": Failed to send TX command (err %d)\n", ret);
			return ret;
		}
	}

	/* Wait for the response */
	for (i = 0; i < IXXAT_USB_MAX_COM_REQ; i++) {
		ret = usb_control_msg(dev, usb_rcvctrlpipe(dev, 0), 0xff,
				USB_TYPE_VENDOR | USB_DIR_IN,
				port, 0, res + pos, res_size - pos,
				msecs_to_jiffies(IXXAT_USB_MSG_TIMEOUT));

		if (ret == -ETIMEDOUT)
			continue;

		if (ret < 0) {
			dev_err(&dev->dev, KBUILD_MODNAME
				": Failed to get TX response (err %d)\n", ret);
			return ret;
		}

		pos += ret;
		if (pos >= res_size)
			break;

		msleep(IXXAT_USB_MSG_CYCLE);
	}

	/* firmware responses may be smaller than requested response size
	 * but should be not smaller than the response header size
	 */
	if (pos < sizeof(struct ixxat_usb_dal_res)) {
		dev_err(&dev->dev, KBUILD_MODNAME
			": Invalid cmd rsp size %u (%u expected)\n",
			pos, res_size);
		return -EBADMSG;
	}

	return le32_to_cpu(dal_res->code);
}

#if IX_CONFIG_USE_HW_TIMESTAMPS
/* multiply by 100.000.000 to get 1ns resolution */
const u64 TICK_FACTOR = 1000000000ULL;

/* ixxat_usb_ts_set_cancaps - set timestamp multiplier/divider
 * from controller timestamp clock settings
 * @timeref: pointer to the time reference structure to set
 * @ts_clock_divisor: timestamp clock divisor
 * @ts_clock_freq: timestamp clock frequency
 * This function calculates the tick multiplier and divider based on the
 * timestamp clock divisor and frequency.
 */
static void ixxat_usb_ts_set_cancaps(struct ixxat_time_ref *timeref,
				     u32 ts_clock_divisor,
				     u32 ts_clock_freq)
{
	/* calculate tick multiplier and divider
	 * divide by clock frequency -> resolution [1s]
	 * multiply by TICK_FACTOR -> resolution [1ns]
	 */
	timeref->tick_multiplier = (u64)ts_clock_divisor * TICK_FACTOR;
	timeref->tick_divider = ts_clock_freq;

	/* remove not significant zero bits from multiplier and divider */
	while (!(timeref->tick_multiplier & 0x1) &&
		!(timeref->tick_divider & 0x1)) {

		timeref->tick_multiplier >>= 1;
		timeref->tick_divider >>= 1;
	}

	/* check if multiplier is divisible by divider without remainder */
	/* TODO: check 32-bit compilation against / */
	if (!(timeref->tick_multiplier % timeref->tick_divider)) {
		timeref->tick_multiplier /= timeref->tick_divider;
		timeref->tick_divider = 1;
	}
}
#endif

/* ixxat_usb_ts_set_start - set controller start timestamp
 * @dev: pointer to the IXXAT USB CAN device
 * @t_A: timestamp A (before start command)
 * @t_B: timestamp B (after start command)
 * @ts_dev_start: device start timestamp
 *
 * This function sets the controller start timestamp based on the provided
 * timestamps and device start timestamp. It also updates the time reference
 * structure in the shared data of the device.
 */
static void ixxat_usb_ts_set_start(struct ixxat_usb_candevice *dev,
				   ktime_t t_A, ktime_t t_B, u32 ts_dev_start)
{
	struct ixxat_usb_device_data *devdata = dev->shareddata;
	unsigned long flags;
	dev_info(&dev->udev->dev,
		 "%s A: %lld B: %lld devtick: %u\n",
		 __func__, t_A, t_B, ts_dev_start);

	spin_lock_irqsave(&devdata->access_lock, flags);

	if (!devdata->timeref_valid) {

		devdata->timeref_valid = true;

		devdata->kt_host_start = 0;
#if IX_SYNCTOHOSTCLOCK == IX_SYNCTOHOST_NONE
		devdata->kt_host_start = 0;

#elif IX_SYNCTOHOSTCLOCK == IX_SYNCTOHOST_BEFORESTART
		devdata->kt_host_start = t_A;

#elif IIX_SYNCTOHOSTCLOCK == X_SYNCTOHOST_AFTERSTART
		devdata->kt_host_start = t_B;

#elif IX_SYNCTOHOSTCLOCK == IX_SYNCTOHOST_ONSTART
		devdata->kt_host_start = t_A +
					 ktime_divns(ktime_sub(t_B, t_A), 2);
#else
#error "Invalid IX_SYNCTOHOSTCLOCK setting"
#endif
		devdata->ts_dev_start = ts_dev_start;

		dev_info(&dev->udev->dev,
			 "set kt_host_start: %lld devtick: %u\n",
			 devdata->kt_host_start, ts_dev_start);
	}

	spin_unlock_irqrestore(&devdata->access_lock, flags);
}

/* ixxat_usb_get_dev_caps - get device capabilities
 * @dev: pointer to the USB device
 * @devdata: pointer to the USB device private data area
 * @dev_caps: pointer to the device capabilities structure to fill
 * This function retrieves the device capabilities from the IXXAT USB device.
 *
 * Returns 0 on success, negative error code on failure.
 */
static int ixxat_usb_get_dev_caps(struct usb_device *dev,
				  struct ixxat_usb_device_data *devdata,
				  struct ixxat_dev_caps *dev_caps)
{
	struct ixxat_usb_caps_cmd *cmd = &devdata->cmd.caps;
	const u32 cmd_size = sizeof(*cmd);
	const u32 req_size = sizeof(cmd->req);
	const u32 rcv_size = cmd_size - req_size;
	const u32 snd_size = req_size + sizeof(cmd->res);
	u16 num_ctrl;
	int i, err;

	ixxat_usb_setup_cmd(&cmd->req, &cmd->res);
	cmd->req.code = cpu_to_le32(IXXAT_USB_BRD_CMD_GET_DEVCAPS);
	cmd->res.res_size = cpu_to_le32(rcv_size);

	err = ixxat_usb_send_cmd(dev, le16_to_cpu(cmd->req.port), cmd, snd_size,
				 &cmd->res, rcv_size);
	if (err)
		return err;

	dev_caps->bus_ctrl_count = cmd->caps.bus_ctrl_count;
	num_ctrl = le16_to_cpu(cmd->caps.bus_ctrl_count);
	if (num_ctrl > ARRAY_SIZE(dev_caps->bus_ctrl_types)) {
		dev_err(&dev->dev, KBUILD_MODNAME
			": invalid ctrlr count %u in rsp (> %lu)\n",
			num_ctrl, ARRAY_SIZE(dev_caps->bus_ctrl_types));
		err = -EINVAL;
		goto fail;
	}

	for (i = 0; i < num_ctrl; i++)
		dev_caps->bus_ctrl_types[i] = cmd->caps.bus_ctrl_types[i];

fail:
	return err;
}

/* ixxat_usb_get_dev_info - get device information
 * @dev: pointer to the USB device
 * @devdata: pointer to the USB device private data area
 * This function retrieves the device information from the IXXAT USB device.
 *
 * Returns 0 on success, negative error code on failure.
 */
static int ixxat_usb_get_dev_info(struct usb_device *dev,
				  struct ixxat_usb_device_data *devdata)
{
	struct ixxat_usb_info_cmd *cmd = &devdata->cmd.info;
	const u32 cmd_size = sizeof(*cmd);
	const u32 req_size = sizeof(cmd->req);
	const u32 rcv_size = cmd_size - req_size;
	const u32 snd_size = req_size + sizeof(cmd->res);
	int err;

	ixxat_usb_setup_cmd(&cmd->req, &cmd->res);
	cmd->req.code = cpu_to_le32(IXXAT_USB_BRD_CMD_GET_DEVINFO);
	cmd->res.res_size = cpu_to_le32(rcv_size);

	err = ixxat_usb_send_cmd(dev, le16_to_cpu(cmd->req.port), cmd, snd_size,
				 &cmd->res, rcv_size);
	if (!err) {
		struct ixxat_dev_info *dev_info = &devdata->dev_info;

		memcpy(dev_info->device_id, &cmd->info.device_id,
		       sizeof(cmd->info.device_id));
		memcpy(dev_info->device_name, &cmd->info.device_name,
		       sizeof(cmd->info.device_name));

		dev_info->device_fpga_version = cmd->info.device_fpga_version;
		dev_info->device_version = cmd->info.device_version;
	}

	return err;
}

/* ixxat_usb_get_fw_info - get firmware information (V1)
 * @dev: pointer to the USB device
 * @devdata: pointer to the USB device private data area
 * This function retrieves the firmware information from the IXXAT USB device.
 *
 * Returns 0 on success, negative error code on failure.
 */
static int ixxat_usb_get_fw_info(struct usb_device *dev,
				 struct ixxat_usb_device_data *devdata)
{
	struct ixxat_usb_fwinfo_cmd *cmd = &devdata->cmd.fwinfo;
	const u32 cmd_size = sizeof(*cmd);
	const u32 req_size = sizeof(cmd->req);
	const u32 rcv_size = cmd_size - req_size;
	const u32 snd_size = req_size + sizeof(cmd->res);
	int err;

	ixxat_usb_setup_cmd(&cmd->req, &cmd->res);
	cmd->req.code = cpu_to_le32(IXXAT_USB_BRD_CMD_GET_FWINFO);
	cmd->res.res_size = cpu_to_le32(rcv_size);

	err = ixxat_usb_send_cmd(dev, le16_to_cpu(cmd->req.port), cmd, snd_size,
				 &cmd->res, rcv_size);
	if (!err) {
		struct ixxat_fw_info2 *fw_info = &devdata->fw_info;

		fw_info->firmware_type = cmd->info.firmware_type;
		fw_info->major_version = cmd->info.major_version;
		fw_info->minor_version = cmd->info.minor_version;
		fw_info->build_version = cmd->info.build_version;
		fw_info->revision = 0;
	}

	return err;
}

/* ixxat_usb_get_fw_info2 - get firmware information (V2)
 * @dev: pointer to the USB device
 * @devdata: pointer to the USB device private data area
 * This function retrieves the firmware information from the IXXAT USB device.
 *
 * Returns 0 on success, negative error code on failure.
 */
static int ixxat_usb_get_fw_info2(struct usb_device *dev,
				  struct ixxat_usb_device_data *devdata)
{
	struct ixxat_usb_fwinfo2_cmd *cmd = &devdata->cmd.fwinfo2;
	const u32 cmd_size = sizeof(*cmd);
	const u32 req_size = sizeof(cmd->req);
	const u32 rcv_size = cmd_size - req_size;
	const u32 snd_size = req_size + sizeof(cmd->res);
	int err;

	ixxat_usb_setup_cmd(&cmd->req, &cmd->res);
	cmd->req.code = cpu_to_le32(IXXAT_USB_BRD_CMD_GET_FWINFO2);
	cmd->res.res_size = cpu_to_le32(rcv_size);

	err = ixxat_usb_send_cmd(dev, le16_to_cpu(cmd->req.port), cmd, snd_size,
				 &cmd->res, rcv_size);
	if (!err) {
		struct ixxat_fw_info2 *fw_info = &devdata->fw_info;

		fw_info->firmware_type = cmd->info.firmware_type;
		fw_info->major_version = cmd->info.major_version;
		fw_info->minor_version = cmd->info.minor_version;
		fw_info->build_version = cmd->info.build_version;
		fw_info->revision = cmd->info.revision;
	}

	return err;
}

/* ixxat_usb_start_ctrl - start the controller
 * @dev: pointer to the IXXAT USB CAN device
 * This function sends a start command to the IXXAT USB CAN device
 *
 * Returns 0 on success, negative error code on failure.
 */
static int ixxat_usb_start_ctrl(struct ixxat_usb_candevice *dev)
{
	struct ixxat_usb_start_cmd *cmd = &dev->shareddata->cmd.start;
	const u16 port = dev->ctrl_index;
	const u32 cmd_size = sizeof(*cmd);
	const u32 req_size = sizeof(cmd->req);
	const u32 rcv_size = cmd_size - req_size;
	const u32 snd_size = req_size + sizeof(cmd->res);
	ktime_t kt_host_A, kt_host_B;
	u32 start_offset = 0;
	int err;

	ixxat_usb_setup_cmd(&cmd->req, &cmd->res);
	cmd->req.code = cpu_to_le32(IXXAT_USB_CAN_CMD_START);
	cmd->req.port = cpu_to_le16(port);
	cmd->res.res_size = cpu_to_le32(rcv_size);
	cmd->time = 0;

	kt_host_A = ktime_get_real_ns();

	err = ixxat_usb_send_cmd(dev->udev, port, cmd, snd_size, &cmd->res,
				 rcv_size);

	kt_host_B = ktime_get_real_ns();

	if (!err)
		start_offset = le32_to_cpu(cmd->time);

	ixxat_usb_ts_set_start(dev, kt_host_A, kt_host_B, start_offset);
#if IX_CONFIG_USE_HW_TIMESTAMPS
	dev->time_ref.ts_overrun_ticks = 0;
#endif

	return err;
}

/* ixxat_usb_stop_ctrl - stop the controller
 * @dev: pointer to the IXXAT USB CAN device
 * This function sends a stop command to the IXXAT USB CAN device.
 *
 * Returns 0 on success, negative error code on failure.
 */
static int ixxat_usb_stop_ctrl(struct ixxat_usb_candevice *dev)
{
	struct ixxat_usb_stop_cmd *cmd = &dev->shareddata->cmd.stop;
	const u16 port = dev->ctrl_index;
	const u32 rcv_size = sizeof(cmd->res);
	const u32 snd_size = sizeof(*cmd);

	ixxat_usb_setup_cmd(&cmd->req, &cmd->res);
	cmd->req.size = cpu_to_le32(snd_size - rcv_size);
	cmd->req.code = cpu_to_le32(IXXAT_USB_CAN_CMD_STOP);
	cmd->req.port = cpu_to_le16(port);
	cmd->action = cpu_to_le32(IXXAT_USB_STOP_ACTION_CLEARALL);

	return ixxat_usb_send_cmd(dev->udev, port, cmd, snd_size, &cmd->res,
				  rcv_size);
}

/* ixxat_usb_power_ctrl - control the power mode of the device
 * @dev: pointer to the USB device
 * @devdata: pointer to the USB device private data area
 * @mode: power mode to set (e.g. IXXAT_USB_POWER_WAKEUP)
 *
 * This function sends a command to the IXXAT USB device to control its power
 * mode.
 *
 * Returns 0 on success, negative error code on failure.
 */
static int ixxat_usb_power_ctrl(struct usb_device *dev,
				struct ixxat_usb_device_data *devdata, u8 mode)
{
	struct ixxat_usb_power_cmd *cmd = &devdata->cmd.power;
	const u32 rcv_size = sizeof(cmd->res);
	const u32 snd_size = sizeof(*cmd);

	ixxat_usb_setup_cmd(&cmd->req, &cmd->res);
	cmd->req.size = cpu_to_le32(snd_size - rcv_size);
	cmd->req.code = cpu_to_le32(IXXAT_USB_BRD_CMD_POWER);
	cmd->mode = mode;

	return ixxat_usb_send_cmd(dev, le16_to_cpu(cmd->req.port),
				  cmd, snd_size,
				  &cmd->res, rcv_size);
}

/* ixxat_usb_reset_ctrl - reset the controller
 * @dev: pointer to the IXXAT USB CAN device
 * This function sends a reset command to the IXXAT USB CAN device.
 *
 * Returns 0 on success, negative error code on failure.
 */
static int ixxat_usb_reset_ctrl(struct ixxat_usb_candevice *dev)
{
	struct ixxat_usb_dal_cmd *cmd = &dev->shareddata->cmd.dal;
	const u16 port = dev->ctrl_index;
	const u32 snd_size = sizeof(*cmd);
	const u32 rcv_size = sizeof(cmd->res);

	ixxat_usb_setup_cmd(&cmd->req, &cmd->res);
	cmd->req.code = cpu_to_le32(IXXAT_USB_CAN_CMD_RESET);
	cmd->req.port = cpu_to_le16(port);

	return ixxat_usb_send_cmd(dev->udev, port, cmd, snd_size, &cmd->res,
				  rcv_size);
}

/* ixxat_usb_free_usb_communication - free USB communication resources
 * @dev: pointer to the IXXAT USB CAN device
 * This function stops the network queue, kills all anchored URBs,
 * frees the message index store, and releases all echo skbs.
 * It also resets the URB contexts to the free entry state.
 */
static void ixxat_usb_free_usb_communication(struct ixxat_usb_candevice *dev)
{
	struct net_device *netdev = dev->netdev;
	u32 skb_idx, urb_idx;

	netif_stop_queue(netdev);
	usb_kill_anchored_urbs(&dev->rx_anchor);
	usb_kill_anchored_urbs(&dev->tx_anchor);
	atomic_set(&dev->active_tx_urbs, 0);

	/* reset msg idx store */
	ixxat_usb_msg_free_idx(dev, 0xFFFFFFFF);

	for (skb_idx = 0; skb_idx < dev->can.echo_skb_max; skb_idx++)
		can_free_echo_skb(netdev, skb_idx, NULL);

	for (urb_idx = 0; urb_idx < IXXAT_USB_MAX_TX_URBS; urb_idx++)
		if (dev->tx_contexts[urb_idx].urb_index != IXXAT_USB_FREE_ENTRY)
			dev->tx_contexts[urb_idx].urb_index =
				IXXAT_USB_FREE_ENTRY;

	/* Annotation:
	 * The Urbs are released within the system with (usb_free_urb)
	 * dependant on the reference count
	 * With the Urbs the assigned buffers are also deleted.
	 * This is caused by urb->transfer_flags |= URB_FREE_BUFFER;
	 */
}

/* ixxat_usb_restart - restart (stop/start) the controller
 * @dev: pointer to the IXXAT USB CAN device
 */
static int ixxat_usb_restart(struct ixxat_usb_candevice *dev)
{
	struct net_device *netdev = dev->netdev;
	int err = ixxat_usb_stop_ctrl(dev);
	if (err)
		goto fail;

	err = ixxat_usb_start_ctrl(dev);
	if (err)
		goto fail;

	dev->can.state = CAN_STATE_ERROR_ACTIVE;
	netif_wake_queue(netdev);

fail:
	return err;
}

/* ixxat_usb_set_mode - set the CAN mode of the device
 * @netdev: pointer to the network device
 * @mode: desired CAN mode (e.g. CAN_MODE_START)
 *
 * This function sets the CAN mode of the device. Currently, only CAN_MODE_START
 * is supported, which restarts the controller.
 *
 * Returns 0 on success, negative error code on failure.
 */
static int ixxat_usb_set_mode(struct net_device *netdev, enum can_mode mode)
{
	int err;
	struct ixxat_usb_candevice *dev = netdev_priv(netdev);

	switch (mode) {
	case CAN_MODE_START:
		err = ixxat_usb_restart(dev);
		break;
	default:
		err = -EOPNOTSUPP;
		break;
	}

	return err;
}

/* ixxat_usb_get_berr_counter - get the bus error counter
 * @netdev: pointer to the network device
 * @bec: pointer to the bus error counter structure to fill
 * This function retrieves the bus error counter from the IXXAT USB CAN device.
 * It fills the provided can_berr_counter structure with the current bus error counters.
 *
 * Returns 0 on success, negative error code on failure.
 */
static int ixxat_usb_get_berr_counter(const struct net_device *netdev,
				      struct can_berr_counter *bec)
{
	struct ixxat_usb_candevice *dev = netdev_priv(netdev);

	*bec = dev->bec;
	return 0;
}

/* ixxat_convert - convert IXXAT CAN message to canfd_frame
 * @adapter: pointer to the IXXAT USB adapter
 * @cf: pointer to the canfd_frame to fill
 * @rx: pointer to the IXXAT CAN message to convert
 * @datalen: data length of the CAN message
 *
 * This function converts an IXXAT CAN message to a canfd_frame structure.
 * It fills the can_id, len, flags, and data fields of the canfd_frame.
 */
static void ixxat_convert(const struct ixxat_usb_adapter *adapter,
			  struct canfd_frame *cf,
			  struct ixxat_can_msg *rx,
			  u8 datalen)
{
	const u32 ixx_flags = le32_to_cpu(rx->base.flags);
	u8 flags = 0;

	if (ixx_flags & IXXAT_USB_FDMSG_FLAGS_EDL) {
		if (ixx_flags & IXXAT_USB_FDMSG_FLAGS_FDR)
			flags |= CANFD_BRS;

		if (ixx_flags & IXXAT_USB_FDMSG_FLAGS_ESI)
			flags |= CANFD_ESI;
	}

	cf->can_id = le32_to_cpu(rx->base.msg_id);
	cf->len = datalen;
	cf->flags |= flags;

	if (ixx_flags & IXXAT_USB_MSG_FLAGS_EXT)
		cf->can_id |= CAN_EFF_FLAG;

	if (ixx_flags & IXXAT_USB_MSG_FLAGS_RTR)
		cf->can_id |= CAN_RTR_FLAG;

	else if (adapter == &usb2can_cl1)
			memcpy(cf->data, rx->cl1.data, datalen);
		else
			memcpy(cf->data, rx->cl2.data, datalen);
}

#if LINUX_VERSION_CODE <= KERNEL_VERSION(5, 8, 0)

/* define 64bit mul_div function which exists only on
 * kernel 5.9 and higher
 */
/*
 * Will generate an #DE when the result doesn't fit u64, could fix with an
 * __ex_table[] entry when it becomes an issue.
 */
static inline u64 mul_u64_u64_div_u64(u64 a, u64 mul, u64 div)
{
	u64 q;

	asm ("mulq %2; divq %3" : "=a" (q)
				: "a" (a), "rm" (mul), "rm" (div)
				: "rdx");

	return q;
}
#define mul_u64_u64_div_u64 mul_u64_u64_div_u64
#endif

#if IX_CONFIG_USE_HW_TIMESTAMPS
/* ixxat_usb_netif_rx - receive a CAN message and pass it to the network stack
 * @timeref: pointer to the time reference structure
 * @skb: pointer to the socket buffer containing the CAN message
 * @ts_tick: timestamp tick value from the CAN message
 *
 * This function calculates the timestamp in nanoseconds from the tick value,
 * sets it in the skb_shared_hwtstamps structure, and passes the skb to the
 * network stack using netif_rx.
 *
 * Returns 0 on success.
 */
static int ixxat_usb_netif_rx(struct ixxat_time_ref *timeref,
			      struct sk_buff *skb, __le32 ts_tick)
{
	u64 ts_ns;
	struct skb_shared_hwtstamps *hwts = skb_hwtstamps(skb);

	/* calculate tick offset */
	u64 ticks = timeref->ts_overrun_ticks;

	ticks |= le32_to_cpu(ts_tick);

#if (IX_SYNCTOHOST_NONE == IX_SYNCTOHOSTCLOCK)
#else
	ticks -= timeref->ts_dev_start;
#endif

	/* convert tick to [1ns] resolution */
	ts_ns = mul_u64_u64_div_u64(ticks, timeref->tick_multiplier,
				    timeref->tick_divider);

#if (IX_SYNCTOHOST_NONE == IX_SYNCTOHOSTCLOCK)
	hwts->hwtstamp = ns_to_ktime(ts_ns);
#else
	hwts->hwtstamp = timeref->kt_host_start + ns_to_ktime(ts_ns);
#endif

	return netif_rx(skb);
}
#else
static int ixxat_usb_netif_rx(struct ixxat_time_ref *timeref,
			      struct sk_buff *skb, __le32 ts_tick)
{
	// skb->tstamp = ktime_get();
	return netif_rx(skb);
}
#endif

/* ixxat_usb_handle_canmsg - handle a received CAN message
 * @dev: pointer to the IXXAT USB CAN device
 * @rx: pointer to the received IXXAT CAN message
 * This function processes a received CAN message from the IXXAT USB device.
 * It checks the message size, extracts the CAN ID, data length, and data,
 * and passes the message to the network stack.
 */
static int ixxat_usb_handle_canmsg(struct ixxat_usb_candevice *dev,
				   struct ixxat_can_msg *rx)
{
	const u32 ixx_flags = le32_to_cpu(rx->base.flags);
	const u8 dlc = IXXAT_USB_DECODE_DLC(ixx_flags);
	const u8 datalen = (ixx_flags & IXXAT_USB_FDMSG_FLAGS_EDL) ?
				can_fd_dlc2len(dlc) : can_cc_dlc2len(dlc);
	u8 min_size = sizeof(rx->base) + datalen;
	struct net_device *netdev = dev->netdev;

	if (dev->adapter == &usb2can_cl1)
		min_size += sizeof(rx->cl1) - sizeof(rx->cl1.data);
	else
		min_size += sizeof(rx->cl2) - sizeof(rx->cl2.data);

	if (rx->base.size < (min_size - 1)) {
		netdev_err(netdev, "Error: CAN Invalid data message size\n");
		return -EBADMSG;
	}

	if (ixx_flags & IXXAT_USB_MSG_FLAGS_OVR) {
		netdev->stats.rx_over_errors++;
		netdev->stats.rx_errors++;
		ix_trace_printk("CAN Message overflow\n");
	}

	if (ixx_flags & IXXAT_USB_MSG_FLAGS_SRR) {
		if (dev->adapter == &usb2can_cl1) {
			/* do nothing because the tx_packets are already handled
			 * in the write callback !
			 */
		} else {
			u32 msg_idx = le32_to_cpu(rx->cl2.client_id);

			netdev->stats.tx_bytes += datalen;
			netdev->stats.tx_packets++;

			if (msg_idx >= IXXAT_USB_MSG_IDX_OFFSET) {
				int len;

				msg_idx -= IXXAT_USB_MSG_IDX_OFFSET;

				len = can_get_echo_skb(netdev, msg_idx, NULL);
				ixxat_usb_msg_free_idx(dev, msg_idx);
			}
		}

		netif_wake_queue(netdev);
	} else {
		struct sk_buff *skb;
		struct canfd_frame *cf;

		if (ixx_flags & IXXAT_USB_FDMSG_FLAGS_EDL)
			skb = alloc_canfd_skb(netdev, &cf);
		else
			skb = alloc_can_skb(netdev, (struct can_frame **)&cf);

		if (!skb)
			return -ENOMEM;

		ixxat_convert(dev->adapter, cf, rx, datalen);

		netdev->stats.rx_packets++;
		netdev->stats.rx_bytes += cf->len;

		ixxat_usb_netif_rx(&dev->time_ref, skb, rx->base.time);
	}

	return 0;
}

/* ixxat_usb_handle_status - handle a received status message
 * @dev: pointer to the IXXAT USB CAN device
 * @rx: pointer to the received IXXAT CAN message
 * This function processes a received status message from the IXXAT USB device.
 */
static int ixxat_usb_handle_status(struct ixxat_usb_candevice *dev,
				   struct ixxat_can_msg *rx)
{
	struct net_device *netdev = dev->netdev;
	struct can_frame *can_frame;
	struct sk_buff *skb;
	enum can_state new_state = CAN_STATE_ERROR_ACTIVE;
	u32 raw_status;
	u8 min_size = sizeof(rx->base) + sizeof(raw_status);

	if (dev->adapter == &usb2can_cl1)
		min_size += sizeof(rx->cl1) - sizeof(rx->cl1.data);
	else
		min_size += sizeof(rx->cl2) - sizeof(rx->cl2.data);

	if (rx->base.size < (min_size - 1)) {
		netdev_err(netdev, "Error: CAN Invalid status message size\n");
		return -EBADMSG;
	}

	if (dev->adapter == &usb2can_cl1)
		raw_status = le32_to_cpup((__le32 *)rx->cl1.data);
	else
		raw_status = le32_to_cpup((__le32 *)rx->cl2.data);

	if (raw_status != IXXAT_USB_CAN_STATUS_OK) {
		if (raw_status & IXXAT_USB_CAN_STATUS_BUSOFF) {
			dev->can.can_stats.bus_off++;
			new_state = CAN_STATE_BUS_OFF;
			can_bus_off(netdev);
		} else {
			if (raw_status & IXXAT_USB_CAN_STATUS_ERRLIM) {
				dev->can.can_stats.error_warning++;
				new_state = CAN_STATE_ERROR_WARNING;
			}

			if (raw_status & IXXAT_USB_CAN_STATUS_ERR_PAS) {
				dev->can.can_stats.error_passive++;
				new_state = CAN_STATE_ERROR_PASSIVE;
			}

			if (raw_status & IXXAT_USB_CAN_STATUS_OVERRUN)
				new_state = CAN_STATE_MAX;
		}
	}

	if (new_state == CAN_STATE_ERROR_ACTIVE) {
		dev->bec.txerr = 0;
		dev->bec.rxerr = 0;
	}

	if (new_state != CAN_STATE_MAX)
		dev->can.state = new_state;

	skb = alloc_can_err_skb(netdev, &can_frame);
	if (!skb)
		return -ENOMEM;

	switch (new_state) {
	case CAN_STATE_ERROR_ACTIVE:
		can_frame->can_id |= CAN_ERR_CRTL;
		can_frame->data[1] |= CAN_ERR_CRTL_ACTIVE;
		break;
	case CAN_STATE_ERROR_WARNING:
		can_frame->can_id |= CAN_ERR_CRTL;
		can_frame->data[1] |= CAN_ERR_CRTL_TX_WARNING;
		can_frame->data[1] |= CAN_ERR_CRTL_RX_WARNING;
		break;
	case CAN_STATE_ERROR_PASSIVE:
		can_frame->can_id |= CAN_ERR_CRTL;
		can_frame->data[1] |= CAN_ERR_CRTL_TX_PASSIVE;
		can_frame->data[1] |= CAN_ERR_CRTL_RX_PASSIVE;
		break;
	case CAN_STATE_BUS_OFF:
		can_frame->can_id |= CAN_ERR_BUSOFF;
		break;
	case CAN_STATE_MAX:
		can_frame->can_id |= CAN_ERR_CRTL;
		can_frame->data[1] |= CAN_ERR_CRTL_RX_OVERFLOW;
		break;
	default:
		netdev_err(netdev, "Error: CAN Unhandled status %d\n",
			   new_state);
		break;
	}

	netdev->stats.rx_packets++;
	netdev->stats.rx_bytes += can_frame->can_dlc;

	ixxat_usb_netif_rx(&dev->time_ref, skb, rx->base.time);

	return 0;
}

/* ixxat_usb_handle_error - handle a received error message
 * @dev: pointer to the IXXAT USB CAN device
 * @rx: pointer to the received IXXAT CAN message
 * This function processes a received error message from the IXXAT USB device.
 * It extracts the error code and updates the bus error counters.
 *
 * Returns 0 on success, negative error code on failure (e.g. -EBADMSG).
 */
static int ixxat_usb_handle_error(struct ixxat_usb_candevice *dev,
				  struct ixxat_can_msg *rx)
{
	struct net_device *netdev = dev->netdev;
	struct can_frame *can_frame;
	struct sk_buff *skb;
	u8 raw_error;
	u8 min_size = sizeof(rx->base) + IXXAT_USB_CAN_ERROR_LEN;

	if (dev->adapter == &usb2can_cl1)
		min_size += sizeof(rx->cl1) - sizeof(rx->cl1.data);
	else
		min_size += sizeof(rx->cl2) - sizeof(rx->cl2.data);

	if (rx->base.size < (min_size - 1)) {
		netdev_err(netdev, "Error: CAN Invalid error message size\n");
		return -EBADMSG;
	}

	if (dev->can.state == CAN_STATE_BUS_OFF)
		return 0;

	if (dev->adapter == &usb2can_cl1) {
		raw_error = rx->cl1.data[IXXAT_USB_CAN_ERROR_CODE];
		dev->bec.rxerr = rx->cl1.data[IXXAT_USB_CAN_ERROR_COUNTER_RX];
		dev->bec.txerr = rx->cl1.data[IXXAT_USB_CAN_ERROR_COUNTER_TX];
	} else {
		raw_error = rx->cl2.data[IXXAT_USB_CAN_ERROR_CODE];
		dev->bec.rxerr = rx->cl2.data[IXXAT_USB_CAN_ERROR_COUNTER_RX];
		dev->bec.txerr = rx->cl2.data[IXXAT_USB_CAN_ERROR_COUNTER_TX];
	}

	if (raw_error == IXXAT_USB_CAN_ERROR_ACK)
		netdev->stats.tx_errors++;
	else
		netdev->stats.rx_errors++;

	skb = alloc_can_err_skb(netdev, &can_frame);
	if (!skb)
		return -ENOMEM;

	switch (raw_error) {
	case IXXAT_USB_CAN_ERROR_ACK:
		can_frame->can_id |= CAN_ERR_ACK;
		break;
	case IXXAT_USB_CAN_ERROR_BIT:
		can_frame->can_id |= CAN_ERR_PROT;
		can_frame->data[2] |= CAN_ERR_PROT_BIT;
		break;
	case IXXAT_USB_CAN_ERROR_CRC:
		can_frame->can_id |= CAN_ERR_PROT;
		can_frame->data[3] |= CAN_ERR_PROT_LOC_CRC_SEQ;
		break;
	case IXXAT_USB_CAN_ERROR_FORM:
		can_frame->can_id |= CAN_ERR_PROT;
		can_frame->data[2] |= CAN_ERR_PROT_FORM;
		break;
	case IXXAT_USB_CAN_ERROR_STUFF:
		can_frame->can_id |= CAN_ERR_PROT;
		can_frame->data[2] |= CAN_ERR_PROT_STUFF;
		break;
	default:
		can_frame->can_id |= CAN_ERR_PROT;
		can_frame->data[2] |= CAN_ERR_PROT_UNSPEC;
		break;
	}

	netdev->stats.rx_packets++;
	netdev->stats.rx_bytes += can_frame->can_dlc;

	ixxat_usb_netif_rx(&dev->time_ref, skb, rx->base.time);

	return 0;
}

/* ixxat_usb_decode_buf - decode a received urb
 * @urb: pointer to the USB request block containing the received data
 *
 * This function decodes the received urb and processes the CAN messages.
 * It handles different message types such as CAN data, status, error, and time
 * overrun.
 *
 * Returns 0 on success, negative error code on failure.
 */
static int ixxat_usb_decode_buf(struct urb *urb)
{
	struct ixxat_usb_candevice *dev = urb->context;
	struct net_device *netdev = dev->netdev;
	int err = 0;
	u8 *data = urb->transfer_buffer;
	u32 len = urb->actual_length, pos, size;

	for (pos = 0; pos < len; pos += size) {
		struct ixxat_can_msg can_msg;
		u32 type;

		/* Since struct ixxat_can_msg is packed and starts with a byte,
		 * we have no choice but to copy the whole into a local
		 * variable to avoid bus violation.
		 */
		size = data[pos] + 1;
		if ((size > sizeof(can_msg)) ||
		    (size < sizeof(struct ixxat_can_msg_base))) {
			err = -EBADMSG;
			netdev_err(netdev, "USB invalid msg size %u\n", size);
			break;
		}

		memcpy(&can_msg, data + pos, size);
		if (!can_msg.base.size) {
			err = -ENOTSUPP;
			netdev_err(netdev, "Error %d: USB Unsupported msg\n",
				   err);
			break;
		}

		size = can_msg.base.size + 1;
		if (size < sizeof(can_msg.base) || (pos + size) > len) {
			err = -EBADMSG;
			netdev_err(netdev,
				   "Error %d: USB Invalid message size\n",
				   err);
			break;
		}

		type = le32_to_cpu(can_msg.base.flags);
		type &= IXXAT_USB_MSG_FLAGS_TYPE;

		switch (type) {
		case IXXAT_USB_CAN_DATA:
			err = ixxat_usb_handle_canmsg(dev, &can_msg);
			if (err)
				goto fail;
			break;

		case IXXAT_USB_CAN_STATUS:
			err = ixxat_usb_handle_status(dev, &can_msg);
			if (err)
				goto fail;
			break;

		case IXXAT_USB_CAN_ERROR:
			err = ixxat_usb_handle_error(dev, &can_msg);
			if (err)
				goto fail;
			break;

		case IXXAT_USB_CAN_TIMEOVR:
#if IX_CONFIG_USE_HW_TIMESTAMPS
			{
				u64 time = le32_to_cpu(can_msg.base.msg_id);
				dev->time_ref.ts_overrun_ticks += (time << 32);
			}
#endif
			break;

		case IXXAT_USB_CAN_INFO:
		case IXXAT_USB_CAN_WAKEUP:
		case IXXAT_USB_CAN_TIMERST:
			break;

		default:
			netdev_err(netdev,
				   "CAN Unhandled rec type 0x%02x (%d): ignored\n",
				   type, type);
			break;
		}
	}

fail:
	if (err)
		netdev_err(netdev, "Error %d: Buffer decoding failed\n", err);

	return err;
}

/* ixxat_usb_encode_msg - encode a CAN message into USB format
 * @dev: pointer to the IXXAT USB CAN device
 * @skb: pointer to the socket buffer containing the CAN message
 * @obuf: output buffer to fill with the encoded message
 * @self_rcv: flag indicating if the message is for self-reception
 * @umsg_idx: message index for self-reception
 *
 * This function encodes a CAN message from the socket buffer into
 * the IXXAT USB CAN message format.
 */
static int ixxat_usb_encode_msg(struct ixxat_usb_candevice *dev,
				struct sk_buff *skb, u8 *obuf, u8 self_rcv,
				u32 umsg_idx)
{
	int size;
	struct canfd_frame *cf = (struct canfd_frame *)skb->data;
	struct ixxat_can_msg can_msg;
	struct ixxat_can_msg_base *msg_base = &can_msg.base;
	u32 flags = 0, msg_id;

	if (cf->can_id & CAN_EFF_FLAG) {
		flags |= IXXAT_USB_MSG_FLAGS_EXT;
		msg_id = cf->can_id & CAN_EFF_MASK;
	} else {
		msg_id = cf->can_id & CAN_SFF_MASK;
	}

	if (cf->can_id & CAN_RTR_FLAG) {
		flags |= IXXAT_USB_MSG_FLAGS_RTR;

	} else if (can_is_canfd_skb(skb)) {
		flags |= IXXAT_USB_FDMSG_FLAGS_EDL;

		if (cf->flags & CANFD_BRS)
			flags |= IXXAT_USB_FDMSG_FLAGS_FDR;

		flags |= IXXAT_USB_ENCODE_DLC(can_fd_len2dlc(cf->len));
	} else {
		flags |= IXXAT_USB_ENCODE_DLC(cf->len);
	}

	msg_base->size = sizeof(*msg_base) + cf->len - 1;

	if (dev->adapter == &usb2can_cl1) {
		msg_base->size += sizeof(can_msg.cl1);
		msg_base->size -= sizeof(can_msg.cl1.data);
		memcpy(can_msg.cl1.data, cf->data, cf->len);

		if (self_rcv)
			flags |= IXXAT_USB_MSG_FLAGS_SRR;

	} else {
		msg_base->size += sizeof(can_msg.cl2);
		msg_base->size -= sizeof(can_msg.cl2.data);
		memcpy(can_msg.cl2.data, cf->data, cf->len);

		if (self_rcv) {
			flags |= IXXAT_USB_MSG_FLAGS_SRR;
			can_msg.cl2.client_id = cpu_to_le32(umsg_idx);
		} else {
			can_msg.cl2.client_id = 0;
		}
	}

	msg_base->flags = cpu_to_le32(flags);
	msg_base->msg_id = cpu_to_le32(msg_id);

	size = msg_base->size + 1;
	memcpy(obuf, &can_msg, size);
	return size;
}

/* ixxat_evaluate_usb_status - evaluate the status of a USB request block
 * @netdev: pointer to the network device
 * @urb: pointer to the USB request block
 * @ep_msg: endpoint message type
 * This function evaluates the status of a USB request block (urb)
 * and returns an error code based on the status.
 */
static int ixxat_evaluate_usb_status(struct net_device *netdev,
				     struct urb *urb, u8 ep_msg)
{
	/* 0: success, -1: error -> return, -2: error -> retry */
	int err = 0;

	if (!netif_device_present(netdev))
		return -1;

	switch (urb->status) {
	case 0:
		/* success */
		break;
	case -EPROTO:
	case -EILSEQ:
	case -ENOENT:
	case -ECONNRESET:
	case -ESHUTDOWN:
		err = -1;
		break;
	default:
		err = -2;
		break;
	}

#ifdef IXXAT_DEBUG
	switch (urb->status) {
	case 0: /* success */
		break;
	case -EPROTO:
		netdev_err(netdev, "EP: %x, Protocol error /(%d)\n", ep_msg, urb->status);
		break;
	case -EILSEQ:
		netdev_err(netdev, "EP: %x, Illegal byte sequence /(%d)\n", ep_msg, urb->status);
		break;
	case -ENOENT:
		netdev_err(netdev, "EP: %x, No such file or directory /(%d)\n", ep_msg, urb->status);
		break;
	case -ECONNRESET:
		netdev_err(netdev, "EP: %x, Connection reset by peer /(%d)\n", ep_msg, urb->status);
		break;
	case -ESHUTDOWN:
		netdev_err(netdev, "EP: %x, Cannot send after transport endpoint shutdown /(%d)\n", ep_msg, urb->status);
		break;
	default:
		netdev_err(netdev, "EP: %x, Urb Status /(%d)\n", ep_msg, urb->status);
		break;
	}
#endif

	return err;
}

/* ixxat_usb_read_bulk_callback - callback for USB bulk read URB
 * @urb: pointer to the USB request block containing the received data
 * This function is called when a USB bulk read URB completes.
 */
static void ixxat_usb_read_bulk_callback(struct urb *urb)
{
	struct ixxat_usb_candevice *dev = urb->context;
	const struct ixxat_usb_adapter *adapter = dev->adapter;
	struct net_device *netdev = dev->netdev;
	struct usb_device *udev = dev->udev;

	int err = ixxat_evaluate_usb_status(netdev, urb, dev->ep_msg_in);
	if (err)
		return;

	if ((urb->actual_length > 0) &&
	    (dev->state & IXXAT_USB_STATE_STARTED)) {
		err = ixxat_usb_decode_buf(urb);
		if (err)
			return;
	}

	/* resubmit_urb: */
	/* ix_trace_printk("callback: fill_bulk_urb %x\n", dev->ep_msg_in); */
	usb_fill_bulk_urb(urb, udev,
			  usb_rcvbulkpipe(udev, dev->ep_msg_in),
			  urb->transfer_buffer, adapter->buffer_size_rx,
			  ixxat_usb_read_bulk_callback, dev);

	usb_anchor_urb(urb, &dev->rx_anchor);
	err = usb_submit_urb(urb, GFP_ATOMIC);
	if (err) {
		usb_unanchor_urb(urb);

		if (err == -ENODEV)
			netif_device_detach(netdev);
		else
			netdev_err(netdev,
				"Error %d: Failed to resubmit read bulk urb\n", err);
	}
}

/* ixxat_usb_write_bulk_callback - callback for USB bulk write URB
 * @urb: pointer to the USB request block containing the sent data
 * This function is called when a USB bulk write URB completes.
 */
static void ixxat_usb_write_bulk_callback(struct urb *urb)
{
	struct ixxat_tx_urb_context *context = urb->context;
	struct ixxat_usb_candevice *dev;
	struct net_device *netdev;
	u32 msg_idx;
	int err;

	if (!context)
		return;

	dev = context->dev;
	netdev = dev->netdev;

	err = ixxat_evaluate_usb_status(netdev, urb, dev->ep_msg_out);
	switch (err) {
	case -1:
		return;

	default:
	case 0:
		/* find correct msg_idx with the CAN Id and Len */
		msg_idx = context->msg_index;

		if (msg_idx < IXXAT_USB_MAX_MSGS) {
			int echo_bytes;

			echo_bytes = can_get_echo_skb(netdev, msg_idx, NULL);
			if (echo_bytes) {
				netdev->stats.tx_bytes += echo_bytes;
				netdev->stats.tx_packets++;
			} else {
				/* if no loopback is active */
				netdev->stats.tx_bytes +=
					context->msg_packet_len;
				netdev->stats.tx_packets +=
					context->msg_packet_no;
			}

			ixxat_usb_msg_free_idx(dev, msg_idx);
		}

		fallthrough;
	case -2:
		context->msg_index = IXXAT_USB_MAX_MSGS;

		ixxat_usb_rel_tx_context(dev, context);
		atomic_dec(&dev->active_tx_urbs);
		netif_wake_queue(netdev);
	}
}

#define IX_LOOP_DIS		0x00	/* disable self reception */
#define IX_LOOP_SELF_RX		0x01	/* enable self reception */
#define IX_LOOPBACK		0x02	/* pass on message to application */

/* ixxat_fix_loop_mode - determine the loop mode for message transmission
 * @loopback: boolean indicating if loopback is set with setsockopt
 * @global_loopback: boolean indicating if global loopback is set
 * @old_dev: boolean indicating if the device is an old version (without client
 * ID)
 *
 * This function determines the loop mode for message transmission based on the
 * loopback settings and the device version. It returns the appropriate loop
 * mode.
 */
static u8 ixxat_fix_loop_mode(bool loopback, bool global_loopback, bool old_dev)
{
	/* decision if this message should be loopbacked !! */
	u8 loop_mode = IX_LOOP_DIS;

	/* exact statistics means that all messages are sent with active
	 * self reception (overhead) so that the statistic counter are
	 * incremented after the message was really written on the can bus,
	 * otherwise the counter is incremented after the WriteURB returns.
	 */
	/* SGr Note: this kind of choice makes nosense in vanilla context */
	const bool statistics_exact = IX_STATISTICS_EXACT;

	/* is loopback set with ip link .. loopback on */
	if (global_loopback) {
		/* is loopback set with setsockopt?
		 * can be changed between message transmission
		 */
		if (loopback)
			loop_mode = IX_LOOP_SELF_RX | IX_LOOPBACK;
	}

	if ((loop_mode & IX_LOOP_SELF_RX) != IX_LOOP_SELF_RX)
		if (statistics_exact)
			loop_mode = IX_LOOP_SELF_RX;

	/* the old firmware doesn't support a clientid
	 * -> so there is no exact loopback or statistic possible
	 */
	if (old_dev)
		loop_mode &= ~IX_LOOP_SELF_RX;

	return loop_mode;
}

/* ixxat_usb_start_xmit - start transmission of a CAN message
 * @skb: pointer to the socket buffer containing the CAN message
 * @netdev: pointer to the network device
 * This function prepares a CAN message for transmission over USB.
 */
static netdev_tx_t ixxat_usb_start_xmit(struct sk_buff *skb,
					struct net_device *netdev)
{
	int err;
	int size;
	struct ixxat_usb_candevice *dev = netdev_priv(netdev);
	struct ixxat_tx_urb_context *context;
	struct net_device_stats *stats = &netdev->stats;
	struct urb *urb;
	u8 *obuf;
	u32 msg_idx;
	u8 loop_mode;
	bool is_loopback;
	bool self_recv;

	if (can_dev_dropped_skb(netdev, skb))
		return NETDEV_TX_OK;

	/* find free URB */
	context = ixxat_usb_get_tx_context(dev);
	if (!context) {
		netif_stop_queue(netdev);
		return NETDEV_TX_BUSY;
	}

	/* get free msg number (ClientId) */
	msg_idx = ixxat_usb_msg_get_next_idx(dev);
	if (msg_idx == IXXAT_USB_E_FAILED) {
		ixxat_usb_rel_tx_context(dev, context);
		netif_stop_queue(netdev);
		return NETDEV_TX_BUSY;
	}

	/* reset to be sure that no old value is stored ! */
	context->msg_index = IXXAT_USB_MAX_MSGS;

	/* prepare the Urb */
	urb = context->urb;
	obuf = urb->transfer_buffer;

	/* check loopback */
	loop_mode = ixxat_fix_loop_mode((skb->pkt_type == PACKET_LOOPBACK),
					dev->loopback,
					dev->adapter == &usb2can_cl1);

	is_loopback = ((loop_mode & IX_LOOPBACK) == IX_LOOPBACK);

	self_recv = ((loop_mode & IX_LOOP_SELF_RX) == IX_LOOP_SELF_RX);
	if (!self_recv) {

		/* handle the reception in the USB callback */
		if (!is_loopback) {
			struct can_frame *cf = (struct can_frame *)skb->data;

			context->msg_packet_len = cf->can_dlc;
			context->msg_packet_no = 1;
		}

		/* store the msg_idx in the Urb */
		context->msg_index = msg_idx;
	}

	size = ixxat_usb_encode_msg(dev, skb, obuf, self_recv,
				    msg_idx + IXXAT_USB_MSG_IDX_OFFSET);
	if (is_loopback)
		can_put_echo_skb(skb, netdev, msg_idx, 0);
	else
		dev_kfree_skb(skb);

#ifdef IXXAT_DEBUG
	showdump(obuf, size);
#endif
	urb->transfer_buffer_length = size;
	usb_anchor_urb(urb, &dev->tx_anchor);

	atomic_inc(&dev->active_tx_urbs);

	err = usb_submit_urb(urb, GFP_ATOMIC);
	if (err) {
		/* submit failed. Should only free if it's exist */
		can_free_echo_skb(netdev, msg_idx, NULL);
		ixxat_usb_msg_free_idx(dev, msg_idx);
		ixxat_usb_rel_tx_context(dev, context);

		usb_unanchor_urb(urb);
		atomic_dec(&dev->active_tx_urbs);

		if (err == -ENODEV) {
			netif_device_detach(netdev);
		} else {
			stats->tx_dropped++;
			netdev_err(netdev, "Error %d: Submitting tx-urb failed\n", err);
		}
	}

	return err;
}

/* ixxat_usb_setup_rx_urbs - setup the receive URBs for the IXXAT USB device
 * @dev: pointer to the IXXAT USB CAN device
 *
 * This function allocates and initializes the receive URBs for the IXXAT USB
 * device. It sets up the URBs to receive CAN messages from the USB endpoint.
 *
 * Returns 0 on success, negative error code on failure.
 */
static int ixxat_usb_setup_rx_urbs(struct ixxat_usb_candevice *dev)
{
	int i;
	int err = 0;
	const struct ixxat_usb_adapter *adapter = dev->adapter;
	struct net_device *netdev = dev->netdev;
	struct usb_device *udev = dev->udev;

	for (i = 0; i < IXXAT_USB_MAX_RX_URBS; i++) {
		u8 *buf;

		struct urb *urb = usb_alloc_urb(0, GFP_KERNEL);
		if (!urb) {
			err = -ENOMEM;
			netdev_err(netdev, "Error %d: No memory for URBs\n",
				   err);
			break;
		}

		ix_trace_printk("setup: kmalloc %x\n", adapter->buffer_size_rx);
		buf = kmalloc(adapter->buffer_size_rx, GFP_KERNEL);
		if (!buf) {
			usb_free_urb(urb);
			err = -ENOMEM;
			netdev_err(netdev,
				   "Error %d: No memory for USB-buffer\n", err);
			break;
		}

		dev->rx_buf[i] = buf;

		ix_trace_printk("setup: fill_bulk_urb %i\n", dev->ep_msg_in);
		usb_fill_bulk_urb(urb, udev,
				  usb_rcvbulkpipe(udev, dev->ep_msg_in), buf,
				  adapter->buffer_size_rx,
				  ixxat_usb_read_bulk_callback, dev);

		urb->transfer_flags |= URB_FREE_BUFFER;
		usb_anchor_urb(urb, &dev->rx_anchor);

		err = usb_submit_urb(urb, GFP_KERNEL);
		if (err) {
			usb_unanchor_urb(urb);

			dev->rx_buf[i] = NULL;
			kfree(buf);

			usb_free_urb(urb);

			if (err == -ENODEV)
				netif_device_detach(netdev);

			break;
		}

		usb_free_urb(urb);
	}

	if (!i)
		netdev_err(netdev, "Error: Couldn't setup any rx-URBs\n");

	return err;
}

/* ixxat_usb_setup_tx_urbs - setup the transmit URBs for the IXXAT USB device
 * @dev: pointer to the IXXAT USB CAN device
 *
 * This function allocates and initializes the transmit URBs for the IXXAT USB
 * device. It sets up the URBs to send CAN messages to the USB endpoint.
 */
static int ixxat_usb_setup_tx_urbs(struct ixxat_usb_candevice *dev)
{
	int urb_idx;
	int ret = 0;
	const struct ixxat_usb_adapter *adapter = dev->adapter;
	struct net_device *netdev = dev->netdev;
	struct usb_device *udev = dev->udev;

	for (urb_idx = 0; urb_idx < IXXAT_USB_MAX_TX_URBS; urb_idx++) {
		struct ixxat_tx_urb_context *context;
		u8 *buf;

		struct urb *urb = usb_alloc_urb(0, GFP_KERNEL);
		if (!urb) {
			ret = -ENOMEM;
			netdev_err(netdev, "Error %d: No memory for URBs\n",
				   ret);
			break;
		}

		buf = kmalloc(adapter->buffer_size_tx, GFP_KERNEL);
		if (!buf) {
			usb_free_urb(urb);
			ret = -ENOMEM;
			netdev_err(netdev,
				   "Error %d: No memory for USB-buffer\n", ret);
			break;
		}

		context = dev->tx_contexts + urb_idx;

		context->dev = dev;
		context->urb = urb;
		context->urb_index = IXXAT_USB_FREE_ENTRY;
		usb_fill_bulk_urb(urb, udev,
				  usb_sndbulkpipe(udev, dev->ep_msg_out), buf,
				  adapter->buffer_size_tx,
				  ixxat_usb_write_bulk_callback, context);

		urb->transfer_flags |= URB_FREE_BUFFER;
	}

	if (!urb_idx) {
		netdev_err(netdev, "Error: Couldn't setup any tx-URBs\n");
		usb_kill_anchored_urbs(&dev->rx_anchor);
	}

	return ret;
}

/* sysfs attributes
 * These attributes provide information about the IXXAT USB device,
 * such as serial number, firmware version, hardware name, hardware version,
 * and FPGA version. They are used to expose device information through sysfs.
 */
static ssize_t serial_show(struct device *pdev, struct device_attribute *attr,
			   char *buf)
{
	struct net_device *netdev = to_net_dev(pdev);
	struct ixxat_usb_candevice *dev = netdev_priv(netdev);
	struct ixxat_usb_device_data *devdata = dev->shareddata;

	return (devdata) ?
		snprintf(buf, PAGE_SIZE, "%.*s\n",
			(int)sizeof(devdata->dev_info.device_id),
			devdata->dev_info.device_id) :
		0;
}
static DEVICE_ATTR_RO(serial);

static ssize_t firmware_version_show(struct device *pdev,
				     struct device_attribute *attr, char *buf)
{
	struct net_device *netdev = to_net_dev(pdev);
	struct ixxat_usb_candevice *dev = netdev_priv(netdev);
	struct ixxat_usb_device_data *devdata = dev->shareddata;

	return (devdata) ?
		snprintf(buf, PAGE_SIZE, "%d.%d.%d.%d\n",
			 le16_to_cpu(devdata->fw_info.major_version),
			 le16_to_cpu(devdata->fw_info.minor_version),
			 le16_to_cpu(devdata->fw_info.build_version),
			 le16_to_cpu(devdata->fw_info.revision)) :
		0;
}
static DEVICE_ATTR_RO(firmware_version);

static ssize_t hardware_show(struct device *pdev,
			     struct device_attribute *attr,
			     char *buf)
{
	struct net_device *netdev = to_net_dev(pdev);
	struct ixxat_usb_candevice *dev = netdev_priv(netdev);
	struct ixxat_usb_device_data *devdata = dev->shareddata;

	return (devdata) ?
		snprintf(buf, PAGE_SIZE, "%.*s\n",
			 (int)sizeof(devdata->dev_info.device_name),
			 devdata->dev_info.device_name) :
		0;
}
static DEVICE_ATTR_RO(hardware);

static ssize_t hardware_version_show(struct device *pdev,
				     struct device_attribute *attr,
				     char *buf)
{
	struct net_device *netdev = to_net_dev(pdev);
	struct ixxat_usb_candevice *dev = netdev_priv(netdev);
	struct ixxat_usb_device_data *devdata = dev->shareddata;

	return (devdata) ?
		snprintf(buf, PAGE_SIZE, "0x%04X\n",
			 devdata->dev_info.device_version) :
		0;
}
static DEVICE_ATTR_RO(hardware_version);

static ssize_t fpga_version_show(struct device *pdev,
				 struct device_attribute *attr,
				 char *buf)
{
	struct net_device *netdev = to_net_dev(pdev);
	struct ixxat_usb_candevice *dev = netdev_priv(netdev);
	struct ixxat_usb_device_data *devdata = dev->shareddata;

	return (devdata) ?
		snprintf(buf, PAGE_SIZE, "0x%08X\n",
			 devdata->dev_info.device_fpga_version) :
		0;
}
static DEVICE_ATTR_RO(fpga_version);

static struct attribute *ixxat_pdev_attrs[] = {
	&dev_attr_serial.attr,
	&dev_attr_firmware_version.attr,
	&dev_attr_hardware.attr,
	&dev_attr_hardware_version.attr,
	&dev_attr_fpga_version.attr,
	NULL,
};

static const struct attribute_group ixxat_pdev_group = {
	.name = NULL,
	.attrs = ixxat_pdev_attrs,
};

/* ixxat_usb_disconnect - disconnect the IXXAT USB device
 * @intf: pointer to the USB interface
 * This function is called when the USB device is disconnected.
 */
static void ixxat_usb_disconnect(struct usb_interface *intf)
{
	struct ixxat_usb_device_data *devdata;

	struct ixxat_usb_candevice *dev = usb_get_intfdata(intf);
	if (!dev)
		return;

	devdata = dev->shareddata;

	/* unregister the given device and all previous devices */
	do {
		struct ixxat_usb_candevice *prev_dev = dev->prev_dev;
		struct net_device *netdev = dev->netdev;
		char name[IFNAMSIZ];

		strscpy(name, netdev->name, IFNAMSIZ);

		/* sysfs_remove_group(&dev->netdev->dev.kobj, &ixxat_pdev_group); */

		unregister_candev(netdev);

		free_candev(netdev);
		dev_info(&intf->dev, "%s removed\n", name);

		dev = prev_dev;
	} while (dev);

	/* free the shared data */
	kfree(devdata);

	usb_set_intfdata(intf, NULL);
}

/* ixxat_usb_start - start the IXXAT USB CAN device
 * @dev: pointer to the IXXAT USB CAN device
 * This function initializes the IXXAT USB CAN device by setting up
 * the receive and transmit URBs, resetting the controller,
 * and starting the controller if necessary.
 *
 * Returns 0 on success, negative error code on failure.
 */
static int ixxat_usb_start(struct ixxat_usb_candevice *dev)
{
	int err;
	int i;
	const struct ixxat_usb_adapter *adapter = dev->adapter;

	err = ixxat_usb_setup_rx_urbs(dev);
	if (err)
		return err;

	err = ixxat_usb_setup_tx_urbs(dev);
	if (err)
		return err;

	/* Try to reset the controller, in case it is already initialized
	 * from a previous unclean shutdown
	 */
	ixxat_usb_reset_ctrl(dev);

	if (adapter->init_ctrl) {
		err = adapter->init_ctrl(dev);
		if (err)
			goto fail;
	}

	if (!(dev->state & IXXAT_USB_STATE_STARTED)) {
		err = ixxat_usb_start_ctrl(dev);
		if (err)
			goto fail;
	}

	dev->bec.txerr = 0;
	dev->bec.rxerr = 0;

	dev->state |= IXXAT_USB_STATE_STARTED;
	dev->can.state = CAN_STATE_ERROR_ACTIVE;

	return 0;

fail:
	if (err == -ENODEV)
		netif_device_detach(dev->netdev);

	netdev_err(dev->netdev, "Error %d: Couldn't submit control\n", err);

	for (i = 0; i < IXXAT_USB_MAX_TX_URBS; i++) {
		usb_free_urb(dev->tx_contexts[i].urb);
		dev->tx_contexts[i].urb = NULL;
	}

	return err;
}

/* ixxat_usb_open - open the IXXAT USB CAN device
 * @netdev: pointer to the network device
 * This function is called when the network device is opened.
 *
 * Returns 0 on success, negative error code on failure.
 */
static int ixxat_usb_open(struct net_device *netdev)
{
	struct ixxat_usb_candevice *dev = netdev_priv(netdev);
	int err;

	/* common open */
	err = open_candev(netdev);
	if (err)
		goto fail;

	/* finally start device */
	err = ixxat_usb_start(dev);
	if (err) {
		netdev_err(netdev, "Error %d: Couldn't start device.\n", err);
		close_candev(netdev);
		goto fail;
	}

	netif_start_queue(netdev);

fail:
	return err;
}

/* ixxat_usb_stop - stop the IXXAT USB CAN device
 * @netdev: pointer to the network device
 *
 * This function stops the IXXAT USB CAN device by freeing the USB
 * communication, stopping the controller, and closing the network device.
 *
 * Returns 0 on success, negative error code on failure.
 */
static int ixxat_usb_stop(struct net_device *netdev)
{
	int err = 0;
	struct ixxat_usb_candevice *dev = netdev_priv(netdev);

	ixxat_usb_free_usb_communication(dev);

	if (dev->state & IXXAT_USB_STATE_STARTED) {
		err = ixxat_usb_stop_ctrl(dev);
		if (err) {
			/* netdev_warn(netdev, "Error %d: Cannot stop device\n",err); */
			ix_trace_printk("Error %d: Cannot stop device\n", err);
		}
	}

	dev->state &= ~IXXAT_USB_STATE_STARTED;
	close_candev(netdev);
	dev->can.state = CAN_STATE_STOPPED;

	return err;
}

static const struct net_device_ops ixxat_usb_netdev_ops = {
	.ndo_open = ixxat_usb_open,
	.ndo_stop = ixxat_usb_stop,
#if IX_CONFIG_USE_HW_TIMESTAMPS && (LINUX_VERSION_CODE >= KERNEL_VERSION(6, 0, 0))
	/* .ndo_eth_ioctl does not exist on kernel prior to 6.0 */
	.ndo_eth_ioctl = can_eth_ioctl_hwts,
#endif
	.ndo_start_xmit = ixxat_usb_start_xmit,
	.ndo_change_mtu = can_change_mtu,
};

#if IX_CONFIG_USE_HW_TIMESTAMPS
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 0, 0)

static const struct ethtool_ops ixxat_ethtool_ops = {
	.get_ts_info = can_ethtool_op_get_ts_info_hwts
};

#else

/* generic implementation of ethtool_ops::get_ts_info for CAN devices
 * supporting hardware timestamps
 */
int ixxat_ethtool_op_get_ts_info_hwts(struct net_device *dev,
				      struct ethtool_ts_info *info)
{
	info->so_timestamping = SOF_TIMESTAMPING_TX_SOFTWARE |
				SOF_TIMESTAMPING_RX_SOFTWARE |
				SOF_TIMESTAMPING_SOFTWARE |
				SOF_TIMESTAMPING_TX_HARDWARE |
				SOF_TIMESTAMPING_RX_HARDWARE |
				SOF_TIMESTAMPING_RAW_HARDWARE;
	info->phc_index = -1;
	info->tx_types = BIT(HWTSTAMP_TX_ON);
	info->rx_filters = BIT(HWTSTAMP_FILTER_ALL);

	return 0;
}

static const struct ethtool_ops ixxat_ethtool_ops = {
	.get_ts_info = ixxat_ethtool_op_get_ts_info_hwts
};

#endif
#endif

/* ixxat_usb_create_ctrl - create a CAN controller for the IXXAT USB device
 * @intf: pointer to the USB interface
 * @adapter: pointer to the IXXAT USB adapter structure
 * @ctrl_index: index of the controller to create
 * @devdata: pointer to the IXXAT USB device data structure
 *
 * This function allocates and initializes a CAN controller for the IXXAT USB
 * device.
 */
static int ixxat_usb_create_ctrl(struct usb_interface *intf,
				 const struct ixxat_usb_adapter *adapter,
				 u16 ctrl_index,
				 struct ixxat_usb_device_data *devdata)
{
	struct usb_device *udev = interface_to_usbdev(intf);
	struct ixxat_usb_candevice *dev;
	struct net_device *netdev;
	int err;
	int i;
#if IX_CONFIG_USE_HW_TIMESTAMPS
	u32 ts_clock_divisor;
	u32 ts_clock_freq;
	struct ixxat_cancaps2 caps;
#endif

	/* number of echo_skb */
	netdev = alloc_candev(sizeof(*dev), IXXAT_USB_MAX_MSGS);
	if (!netdev) {
		dev_err(&intf->dev, "Cannot allocate candev\n");
		return -ENOMEM;
	}

	dev = netdev_priv(netdev);

	/* Must be identical to the can.echo_skb_max set.
	 * This is necessary to correctly handle the loopback option.
	 */
	dev->msg_max = IXXAT_USB_MAX_MSGS;

	dev->shareddata = devdata;

	dev->udev = udev;
	dev->netdev = netdev;
	dev->adapter = adapter;
	dev->loopback = false;
	dev->ctrl_index = ctrl_index;
	dev->state = IXXAT_USB_STATE_CONNECTED;

	spin_lock_init(&dev->dev_lock);

	i = ctrl_index + adapter->ep_offs;
	dev->ep_msg_in = adapter->ep_msg_in[i];
	dev->ep_msg_out = adapter->ep_msg_out[i];

	dev->can.clock.freq = adapter->clock;
	dev->can.bittiming_const = adapter->bt;
	dev->can.data_bittiming_const = adapter->btd;
	dev->can.ctrlmode_supported = adapter->modes;

	/* map function */
	dev->can.do_set_mode = ixxat_usb_set_mode;
	dev->can.do_get_berr_counter = ixxat_usb_get_berr_counter;

	/* configure communication */
	init_usb_anchor(&dev->rx_anchor);
	init_usb_anchor(&dev->tx_anchor);

	atomic_set(&dev->active_tx_urbs, 0);

	for (i = 0; i < IXXAT_USB_MAX_TX_URBS; i++)
		dev->tx_contexts[i].urb_index = IXXAT_USB_FREE_ENTRY;

	ixxat_usb_msg_free_idx(dev, 0xFFFFFFFF);

	/* configure netdev */
	netdev->netdev_ops = &ixxat_usb_netdev_ops;
#if IX_CONFIG_USE_HW_TIMESTAMPS
	netdev->ethtool_ops = &ixxat_ethtool_ops;
#endif
	netdev->flags |= IFF_ECHO;

#if IX_CONFIG_USE_HW_TIMESTAMPS
	dev->adapter->get_ctrl_caps(dev, &caps);

	ts_clock_divisor = le32_to_cpu(caps.ts_clock_divisor);
	ts_clock_freq  = le32_to_cpu(caps.ts_clock_freq);

	ixxat_usb_ts_set_cancaps(&dev->time_ref, ts_clock_divisor,
				 ts_clock_freq);
#endif

	/* link this device into the existing list */
	dev->prev_dev = usb_get_intfdata(intf);
	usb_set_intfdata(intf, dev);

	SET_NETDEV_DEV(netdev, &intf->dev);
	err = register_candev(netdev);
	if (err) {
		dev_err(&intf->dev, "Error %d: Failed to register can device\n",
			err);
		goto free_candev;
	}

#if IX_CONFIG_USE_HW_TIMESTAMPS
	netdev_info(netdev, "timestamp clock resolution  : %u / %u\n",
		    ts_clock_freq, ts_clock_divisor);
	netdev_info(netdev, "timestamp multiplier/divisor: %llu / %llu\n",
		    dev->time_ref.tick_multiplier, dev->time_ref.tick_divider);
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 17, 0)
	/* do not set device address because
	 * that triggers a bug in ModemManager (at least in ModemManager 1.10.0
	 * on debian kernel 4.19.0-22-amd64 #1 SMP Debian 4.19.260-1
	 * (2022-09-29))
	 *
	 * netdev->addr_len = sizeof(devdata->dev_info.device_id);
	 * netdev->dev_addr = devdata->dev_info.device_id;
	 */
#else
	netdev->addr_len = sizeof(devdata->dev_info.device_id);
	dev_addr_mod(netdev, 0, devdata->dev_info.device_id,
		     sizeof(devdata->dev_info.device_id));
#endif

	netdev->dev_id = ctrl_index;
	netdev->dev_port = ctrl_index;

	err = sysfs_create_group(&netdev->dev.kobj, &ixxat_pdev_group);
	if (err < 0) {
		netdev_err(netdev, "Error: %d: create sysfs failed\n", err);
		goto free_candev;
	}

	netdev_info(netdev, "%.*s: Connected channel %u (device %.*s)\n",
		    (int)sizeof(devdata->dev_info.device_name),
		    devdata->dev_info.device_name, ctrl_index,
		    (int)sizeof(devdata->dev_info.device_id),
		    devdata->dev_info.device_id);

	return err;

free_candev:
	sysfs_remove_group(&netdev->dev.kobj, &ixxat_pdev_group);
	usb_set_intfdata(intf, dev->prev_dev);
	free_candev(netdev);
	return err;
}

/* ixxat_usb_check_channel - check if the USB interface matches the known
 * endpoints
 * @adapter: pointer to the IXXAT USB adapter structure
 * @host_intf: pointer to the USB host interface descriptor
 *
 * This function checks if the endpoints of the USB interface match the known
 * endpoints for the IXXAT USB adapter. It returns NETDEV_TX_OK if all endpoints
 * match, otherwise it returns an error code.
 *
 * Returns NETDEV_TX_OK on success, or an error code on failure.
 */
static int ixxat_usb_check_channel(const struct ixxat_usb_adapter *adapter,
				   const struct usb_host_interface *host_intf)
{
	int match = 0;
	u16 i;

	for (i = 0; i < host_intf->desc.bNumEndpoints; i++) {
		const u8 epaddr = host_intf->endpoint[i].desc.bEndpointAddress;
		u8 j;

		/* Check if usb-endpoint address matches known usb-endpoints */
		match = 0;
		for (j = 0; j < IXXAT_USB_MAX_CHANNEL; j++) {
			u8 ep_msg_in = adapter->ep_msg_in[j];
			u8 ep_msg_out = adapter->ep_msg_out[j];

			if (epaddr == ep_msg_in || epaddr == ep_msg_out) {
				match = 1;
				break;
			}
		}

		if (!match)
			break;
	}

	return match ? NETDEV_TX_OK : -ENODEV;
}

/* ixxat_usb_probe - probe the IXXAT USB device
 * @intf: pointer to the USB interface
 * @id: pointer to the USB device ID structure
 * This function is called when the USB device is connected.
 * It initializes the IXXAT USB device, retrieves firmware information,
 * checks the device capabilities, and creates the CAN controller devices.
 *
 * Returns 0 on success, negative error code on failure.
 */
static int ixxat_usb_probe(struct usb_interface *intf,
			   const struct usb_device_id *id)
{
	struct usb_device *udev = interface_to_usbdev(intf);
	const struct ixxat_usb_adapter *adapter;
	struct ixxat_usb_device_data *devdata;
	struct ixxat_dev_caps dev_caps = {0};
	u16 ctrlidx;
	int err;

	devdata = kzalloc(sizeof(*devdata), GFP_KERNEL);
	if (!devdata) {
		err = -ENOMEM;
		goto done;
	}

	/* init device struct */
	spin_lock_init(&devdata->access_lock);

#ifdef IXXAT_OOT_VERSION
	pr_info(IX_DRIVER_TAG "KERNELVERSION: 0x%x (%i)",
		LINUX_VERSION_CODE, LINUX_VERSION_CODE);
#endif

	err = ixxat_usb_get_fw_info(udev, devdata);
	if (err) {
		dev_err(&udev->dev, "Failed to get FW info (err %d)\n", err);
	} else {
		u32 fw_type = le32_to_cpu(devdata->fw_info.firmware_type);
		if (fw_type != IXXAT_USB_DEV_FWTYPE_BAL) {
			dev_err(&udev->dev,
				"Firmware type %u unknown; %u expected\n",
				fw_type, IXXAT_USB_DEV_FWTYPE_BAL);
			err = -EFAULT;

		/* Otherwise, check if FW supports get_fw_info2 command */
		} else if (ixxat_usb_has_cl2_firmware(id, &devdata->fw_info)) {
			err = ixxat_usb_get_fw_info2(udev, devdata);
			if (err)
				dev_err(&udev->dev,
					"Failed to get FW info2 (err %d)\n",
					err);
		}
	}

	if (err) {
		dev_err(&udev->dev, "A firmware update may be required\n");
		adapter = ixxat_usb_get_adapter(id, NULL);
	} else {
		adapter = ixxat_usb_get_adapter(id, &devdata->fw_info);
	}

	if (adapter) {
		dev_info(&udev->dev, "%s\n", ixxat_usb_dev_name(id));

		err = ixxat_usb_check_channel(adapter, intf->altsetting);

		if (err == NETDEV_TX_OK) {
			err = ixxat_usb_power_ctrl(udev, devdata, IXXAT_USB_POWER_WAKEUP);
			if (err != NETDEV_TX_OK)
				dev_err(&udev->dev, "Error %d: Failed to exec IXXAT_USB_BRD_CMD_POWER command.\n", err);
			msleep(IXXAT_USB_POWER_WAKEUP_TIME);
		}

		if (err == NETDEV_TX_OK) {
			err = ixxat_usb_get_dev_info(udev, devdata);
			if (err) {
				dev_err(&udev->dev,
					"Error %d: Failed to get device information\n", err);
			}
		}

		if (err == NETDEV_TX_OK) {
			pr_info(IX_DRIVER_TAG "Device type     : %.*s\n", (int)(sizeof(devdata->dev_info.device_name)), devdata->dev_info.device_name);
			pr_info(IX_DRIVER_TAG "Device id       : %.*s\n", (int)(sizeof(devdata->dev_info.device_id)), devdata->dev_info.device_id);
			pr_info(IX_DRIVER_TAG "Device version  : 0x%08X\n", devdata->dev_info.device_version);
			pr_info(IX_DRIVER_TAG "FPGA version    : 0x%08X\n", devdata->dev_info.device_fpga_version);
			pr_info(IX_DRIVER_TAG "Firmware version: %d.%d.%d.%d (type: %d)"
				, le16_to_cpu(devdata->fw_info.major_version)
				, le16_to_cpu(devdata->fw_info.minor_version)
				, le16_to_cpu(devdata->fw_info.build_version)
				, le16_to_cpu(devdata->fw_info.revision)
				, le32_to_cpu(devdata->fw_info.firmware_type));

			if (ixxat_usb_needs_firmware_update(id, &devdata->fw_info))
				pr_warn(IX_DRIVER_TAG "                  Firmware update recommended.\n");
		}

		if (err == NETDEV_TX_OK) {
			err = ixxat_usb_get_dev_caps(udev, devdata, &dev_caps);

			if (err) {
				dev_err(&intf->dev,
					"Error %d: Failed to get device capabilities\n", err);
			}
		}

		if (err == NETDEV_TX_OK) {
			err = -ENODEV;
#ifdef IXXAT_DEBUG
			showdevcaps(dev_caps);
#endif

			for (ctrlidx = 0; ctrlidx < le16_to_cpu(dev_caps.bus_ctrl_count); ctrlidx++) {
				u16 dev_bustype = le16_to_cpu(dev_caps.bus_ctrl_types[ctrlidx]);
				u8 bustype = IXXAT_USB_BUS_TYPE(dev_bustype);

				if (bustype == IXXAT_USB_BUS_CAN)
					err = ixxat_usb_create_ctrl(intf, adapter, ctrlidx, devdata);

				if (err) {
					/* deregister already created devices */
					ixxat_usb_disconnect(intf);
					break;
				}
			}
		}
	} else {
		dev_err(&intf->dev, "%s: Unknown device id %d\n",
			KBUILD_MODNAME, id->idProduct);
		err = -ENODEV;
	}

done:
	return err;
}

static struct usb_driver ixxat_usb_driver = {
	.name = KBUILD_MODNAME,
	.probe = ixxat_usb_probe,
	.disconnect = ixxat_usb_disconnect,
	.id_table = ixxat_usb_table,
};

module_usb_driver(ixxat_usb_driver);
