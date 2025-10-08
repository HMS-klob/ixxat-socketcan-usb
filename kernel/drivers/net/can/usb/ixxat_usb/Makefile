# SPDX-License-Identifier: GPL-2.0
#ifdef IXXAT_OOT_VERSION
# needed since 6.15
ccflags-y := $(EXTRA_CFLAGS)
#endif
obj-$(CONFIG_CAN_IXXAT_USB) += ix_usb_can.o
ix_usb_can-y = ixxat_usb_cl1.o ixxat_usb_cl2.o ixxat_usb_core.o
