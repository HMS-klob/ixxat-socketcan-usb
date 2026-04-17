# SPDX-License-Identifier: GPL-2.0
#
# Makefile for IXXAT USB CAN interfaces driver
#
# Usage:
#
# targets:
# clean			clean up, base for a forced new make
# install		run as root, installs the drivers
# uninstall		run as root, uninstalls the drivers
#
# makefile uses kernel build system, so standard kernel make options apply
# see Documentation/kbuild/makefiles.txt in kernel source tree
#
# DKMS support:
#
# 	if the makefile finds dkms in the system, it will use it to install/uninstall
# 	the driver. If dkms is not found, the driver will be manually installed.
# 	To force manual installation, set USE_DKMS=no in the make command line.
#
# Generate in-tree driver variant:
#   make intree
# or with specific kernel sources:
#   make intree INTREE_KERNEL=/path/to/linux/kernel/sources
#
DRV				:= ix_usb_can
KERNEL_VERSION	?= $(shell uname -r)
KERNEL_SRC		?= /lib/modules/$(KERNEL_VERSION)/build
MOD_DIR			?= kernel/drivers/net/can/usb/ixxat_usb
SRC_DIR			:= $(shell pwd)/$(MOD_DIR)
INSTALL_LOCATION := /lib/modules/$(KERNEL_VERSION)/$(MOD_DIR)

PWD				:= $(shell pwd)
TARGET			:= $(DRV).ko
obj-m			:= $(DRV).o

DEPMOD			:= $(shell which depmod)
DEPMOD_OPTS		:= -a
MODPROBE		:= $(shell which modprobe)

# Additional kernel make options
MAKE_CMDLINE_OPTS += CONFIG_CAN_IXXAT_USB=m

# DKMS support
# Note: if dkms is not installed then the driver will be manually installed
#       dkms.conf and the dkms speciific makefaile	is always build
#
USE_DKMS		?= yes
DKMS_DRV		:= $(DRV)
DKMS_VER		:= 2.1.3
DKMS_CONF		:= dkms.conf
DKMS_MAKEFILE	:= $(DKMS_DRV).mk
DKMS_TREE		:= /var/lib/dkms
DKMS_BIN		:= $(shell which dkms 2>/dev/null || which dkms)
DKMS_STDERR		:= $(INSTALL_LOCATION)/tmp/dkms-$(DRV)-error.log
DKMS_SRC_DIR	:= /usr/src/$(DKMS_DRV)-$(DKMS_VER)

# DKMS not installed ?
ifeq ($(DKMS_BIN),)
USE_DKMS		= no
endif

# check if driver module runs
DRV_RUNS := $(shell lsmod | grep $(DRV) | cut -d' ' -f1)

.PHONY: all clean modules_install install uninstall

all:
	$(MAKE) -C $(KERNEL_SRC) M=$(SRC_DIR) $(MAKE_CMDLINE_OPTS) modules

clean:
	$(MAKE) -C $(KERNEL_SRC) M=$(SRC_DIR) $(MAKE_CMDLINE_OPTS) clean

modules_install:
	$(MAKE) -C "$(KERNEL_SRC)" M=$(SRC_DIR) $(MAKE_CMDLINE_OPTS) modules_install

install: all dkms_prepare
	@-rmmod $(DRV) 2> /dev/null || true
	@if [ -f $(INSTALL_LOCATION)/$(TARGET) ]; then\
		rm -f $(INSTALL_LOCATION)/$(TARGET);\
		$(DEPMOD) $(DEPMOD_OPTS);\
	fi
ifeq ($(USE_DKMS),yes)
	@echo "- DKMS install $(DRV) to $(DKMS_SRC_DIR)..."
	@(if ! [ -d $(DKMS_SRC_DIR) ]; then \
		mkdir -p $(DKMS_SRC_DIR); \
		cp -r $(SRC_DIR)/* $(DKMS_SRC_DIR); \
		cp $(PWD)/$(DKMS_CONF) $(DKMS_SRC_DIR)/$(DKMS_CONF); \
		$(DKMS_BIN) add $(DKMS_SRC_DIR) >>kminstall.log; \
	fi)
	@$(DKMS_BIN) install -m $(DKMS_DRV) -v $(DKMS_VER) >>kminstall.log
	@$(DKMS_BIN) status  -m $(DKMS_DRV) -v $(DKMS_VER) >>kminstall.log
else
	@echo "- manually installing $(DRV) under $(INSTALL_LOCATION)..."
	@install -d "$(INSTALL_LOCATION)"
	@install "$(SRC_DIR)/$(TARGET)" "$(INSTALL_LOCATION)"
	@echo "- Building dependencies..."
	@$(DEPMOD) $(DEPMOD_OPTS)
	@$(MODPROBE) $(DRV)
endif

uninstall:
	@(if [ -n "$(DRV_RUNS)" ]; then\
        rmmod $(DRV) >>kminstall.log 2>&1;\
    fi)
ifeq ($(USE_DKMS),yes)
	@echo "- DKMS remove $(DRV) from $(DKMS_SRC_DIR)"
	@$(DKMS_BIN) remove -m $(DKMS_DRV) -v $(DKMS_VER) --all >>kminstall.log 2>&1 || true
	@(if [ -d $(DKMS_SRC_DIR) ]; then\
		rm -r $(DKMS_SRC_DIR)/* ;\
		rmdir --ignore-fail-on-non-empty $(DKMS_SRC_DIR);\
	fi)
else
	@echo "- manually uninstalling $(DRV) from $(INSTALL_LOCATION)"
	@if [ -f $(INSTALL_LOCATION)/$(TARGET) ]; then\
		rm -f $(INSTALL_LOCATION)/$(TARGET);\
		$(DEPMOD) $(DEPMOD_OPTS);\
	fi
endif

#-------------------- DKMS  --------------------

# show dkms status
dkms_status:
ifeq ($(USE_DKMS),yes)
	@echo "- getting dkms status for $(DKMS_DRV) $(DKMS_VER) for $(KERNEL_VERSION):"
	@$(DKMS_BIN) status $(DKMS_DRV)/$(DKMS_VER) -k $(KERNEL_VERSION) 2> /dev/null | grep -q -e ": installed$$" && [ $$? -eq 0 ] && echo "  => Ok" || echo "  => NOk"
else
	@echo "- DKMS not installed, no status available."
endif

# prepare dkms files
dkms_prepare: $(DKMS_CONF)

# build dkms config file
$(DKMS_CONF):
	@echo "PACKAGE_NAME=\"$(DKMS_DRV)\"" > $(DKMS_CONF)
	@echo "PACKAGE_VERSION=\"$(DKMS_VER)\"" >> $(DKMS_CONF)
	@echo "CLEAN=\"true\"" >> $(DKMS_CONF)
	@echo "BUILT_MODULE_NAME[0]=\"$(DRV)\"" >> $(DKMS_CONF)
	@echo "BUILT_MODULE_LOCATION[0]=\".\"" >> $(DKMS_CONF)
	@echo "DEST_MODULE_LOCATION[0]=\"/updates\"" >> $(DKMS_CONF)
	@echo "AUTOINSTALL=\"yes\"" >> $(DKMS_CONF)


-include Makefile.intree
