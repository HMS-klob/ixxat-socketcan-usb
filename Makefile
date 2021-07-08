mod-name += ix_usb_can

MOD_DIR             := kernel/drivers/net/can/usb/ixxat_usb
KBUILD_DIR          := /lib/modules/$(shell uname -r)/build
SRC_DIR             := $(shell pwd)/$(MOD_DIR)
DEST_DIR            := /lib/modules/$(shell uname -r)/$(MOD_DIR)

#
# the Kernel Makefile is used !
#

.PHONY: all clean install uninstall

all:
	make -C $(KBUILD_DIR) M=$(SRC_DIR) CONFIG_CAN_IXXAT_USB=m modules

clean:
	make -C $(KBUILD_DIR) M=$(SRC_DIR) CONFIG_CAN_IXXAT_USB=m clean

install: all
	install -d "$(DEST_DIR)"
	install "$(SRC_DIR)/$(mod-name).ko" "$(DEST_DIR)"
	depmod -a
	modprobe $(mod-name)

uninstall:
	modprobe -r $(mod-name)
	depmod -a
	rm -f "$(DEST_DIR)/$(mod-name).ko"
