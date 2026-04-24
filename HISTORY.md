# ix_usb_can

## History

### 2.1.7	(2026-04-24)

- add/cleanup documentation
- add command timeout parameter to ixxat_usb_send_cmd/ixxat_usb_send_cmd_internal
- Fixed the order of USB commands to align with Windows driver
  practices and timing.
  This change eliminates unnecessary msleep() calls and error handling,
  resulting in faster driver startup.
- Remove useless msleep() but keep only one, when cmd sent is CMD_POWER
  TODO: this could be improved by also testing WAKEUP/SLEEP switch
- Power-up the device before doing anything
- In case hardware timestamps is defined with IX_STATISTICS_EXACT, this patch
  allows driver to set a Tx hardware timestamp into the echo skb
- In case IX_STATISTICS_EXACT is undef, then the driver is not capable of
  giving Tx hardware timestamps.
  This patch defines an IXXAT specific method to export this to ethtool.
- Fix compilation against linux v7.0

### 2.1.6	(2026-03-30)

- during tests sometimes E_PIPE errors occured, now the driver does retry
  sending usb control messages on errors and msleeps between tries.

### 2.1.5	(2026-03-26)

- fix access locking for common command channel/buffer

### 2.1.4	(2026-03-13)

- rewrite function to remove fallthrough (fix build on kernel 4.19/debian 10)
- remove unused code

### 2.1.3	(2025-12-15)

- fixes submitted by Stephane Grosjean (stephane.grosjean@hms-networks.com):
   * Include support of Linux 6.19
   * can_put_echo_skb() releases skb, therefore it should be used once skb is no more referenced
   * No need to stop network tx path on error (network core does it by itself)
   * Replace unnecessary spin_lock_irqsave() by a simpler spin_lock()
   * Change error into warning message
   * Rework message format string according to variant kind
   * Add number of attempts in err msg before returning the error
   * Add tests at the end of loops to handle cases where the maximum number of
     attempts is exceeded
   * Move (back) netdev_info() usage (once the netdev device is named/initialized)
   * Remove IXXAT_USB_STATE_STARTED "state" bit only when it is actually set
   * BUGFIX(2/2): all Rx urb must be anchored in case an error occurs in ixxat_usb_start()
     Move usb_kill_anchored_urbs(&dev->rx_anchor) outside of
     ixxat_usb_setup_tx_urbs() (which indeed was not a good place to be) and put
     it at the end of ixxat_usb_start() (just like pcan_usb_core.c does) so that
     all submitted rx urbs will really be freed in case of "fail"
   * BUGFIX: Now allows the driver to operate in degraded mode
     when it has not been able to allocate all the URBs it requested.
     Updated comments accordingly.
   * Uniformizes variable names and their declarations
   * Remove unnecessary braces in condition
   * Remove unused constant definitions from in-tree variants
   * Move constants definitions from core header to core C file and add comments
     These constants only deal with core C file but not with common definitions
     we assume to find in the core header file.
   * Fix 32-bit compilation error by using do_div()
     HW timestamps usage is now possible for 32 bit kernels
   * Fix compilation for kernel < 6.0 (regarding Tx/Rx error counters usage)
   * Fix 32-bit compilation warning
   * Remove global TICK_FACTOR and use a #defined symbol instead
   * Move kernel message regarding timestamp clock resolution outside of the
     vanilla variant (too much verbose) but inside the
     IX_CONFIG_USE_HW_TIMESTAMPS block to make reading easier
   * Rearrange code to make it simpler to read
   * Move comment so that t won't be part in vanilla version
   * Reorder lines to make reading easier
   * Fix forgotten #if/#ifdef replacement
   * Remove useless comment/defines
   * Use #ifdef instead of #if IX_CONFIG_USE_HW_TIMESTAMPS
   * Rewrite "make help" and add "make intree-local" inline help
   * Rewrite the way unifdef file is build.
     This allows user to create an in-tree version that matches a given kernel
     tree. If no Linux kernel is given, then a vanilla variant is made
   * Move definition of UNIFDEF
   * Introduce BUG_ON() on NULL context in read bulk callback too and remove
     some local variables that are not really needed
   * Rename variables names using Linux coding style
   * BUGFIX: remove comment around sysfs_remove_group()
   * Rewrite error message format
   * CAN protocol errors are CAN bus errors
   * Don't update network stats in case of error/status skb
   * ixxat_usb_handle_status(): provide now rx/tx error counters in CAN state
     change notifications
   * Use unlikely(!skb) instead of !skb
   * Keep comment for oot variant only
   * Remove unnecessary test
   * Rewrite xxat_usb_start_ctrl() to facilitate in-tree export
   * Remove unnecessary init
   * Remove unnecessary IXXAT_USB_E_FAILED
     Replace it and change the corresponding failure test
   * Move all what deal with ix_trace_printk() to OOT variant only
   * Surround calls to ix_trace_printk() with #ifdef IXXAT_DEBUG
     Mainly useful for unifdef
   * Replace some calls to ix_trace_printk() wit netdev_warn()
   * Use direct C constant in printk
   * Remove unnecessary IX_DRIVER_TAG
   * Do call netif_wake_queue() only when echo skb has bee nreleased
   * add missing netif_wake_queue() once echo skb has been released
   * Archive: keep original ixxat_fix_loop_mode() until new way is ok
   * Correct alignment
   * WIP: Rewrite echo management and ctrl loopback according to IX_STATISTICS_EXACT
     loopback mode works with non CL1 FW in non exact statistics mode (like the
     in-tree variant).
     Tx path seems blocked when in STATISTICS_EXACT and interface in loopback
     mode.
   * CL2: Remove unnecessary initialization to 0
   * CL2 FW: CTRLMODE_FD_NON_ISO bit SHOULD be handled only if CTRLMODE_FD is
   * CL2 (non CL1) FW: add CTRLMODE_3_SAMPLES in supported mode list
   * Since CL1 firmware handles hw loopback (but not client id) then set
     again CTRLMODE_LOOPBACK in exported ctrlmode_supported
   * Add possibility of compiling the intree against with the running kernel
     WARNING: compilation can fail!
   * Move ix_trace_xxx() (comment) into DEBUG conditional block so that it is
     not part of the in-tree variant
   * BUGFIX Rx path: handle err == -2 cases
     err = -2 means error + resubmit... But ixxat_usb_read_bulk_callback()
     returned "if (err)"... Fix this and return only in case err == -1
   * Reserve verbose messages about timestamping for the DEBUG mode only
   * Remove unused field from the in-tree variant
   * First version of the in-tree variant that runs with (wrongly named)
     "loopback" mode fixed
   * If msg_idx == IXXAT_USB_MAX_MSGS, then no need to set it again
   * msg_idx can't be >= IXXAT_USB_MAX_MSGS except when exact stistics are
     requested (which can't be in the in-tree variant).
   * can_echo_get_skb() must be called, even in case of errors
   * Replace unnecessary ixxat_usb_candevice::msg_max with IXXAT_USB_MAX_MSGS
   * Update trans_start only in case of success
   * Prefer BUG_ON() in case of NULL context
   * Rx path: arrange block of code handling SRR to isolate it
   * Move IXXAT_USB_E_FAILED from .h into the only single .c file it is using it
   * Remove unnecessary initializations to 0
   * Since controller _init() function tests CAN_CTRLMODE_3_SAMPLES bit, then
     it SHOULD be exported in ctrlmode_supported too!
   * Since CL1 firmware doesn't support hardware loopback, remove it from
     socket-can ctrlmode_supported attribute.
     However, keep it in the oot variant of the driver (for compatibility,
     waiting for decision to keep it...)
   * Add symbols to define/undefine when exporting in-tree variant
   * Change MODULE_AUTHOR email address from hms-networks.de to hms-networks.com
   * New way of handling IX_STATISTICS_EXACT
   * Fix alignment
   * Add clock sync default/in-tree selection mode to unifdef file
   * Rename IXXAT_INTREE_VARIANT into IX_INTREE_VARIANT
   * Add "variant" terminology instead of "version"
   * Remove useless macro
   * Rename unifdef test rule name; append clean_intree to .PHONY targets list
   * Reverses the compilation logic conditioned on the compiled variant
   * Move ccflags-y definition to the Makefile used to build the driver module
   * Corrects non-compliance with 80 characters line width rule
   * BUXFIX: Fix the way LIN/CAN ports are enumerated
   * Makefile: define IXXAT_OOT_VERSION in EXTRA_CFLAGS and support of 6.15
   * Since kfree(devdata) has been added to release devdata in case of error,
     normal path (without error) MUST explicitly return 0 WITHOUT freeing
     devdata.
   * Standardization of messages produced in the kernel:
     Use netdev_xxx(netdev) and dev_xxx(intf->dev) whenever possible:
   * Include support up to kernel 6.17.0+
   * Move ixxat_kernel_adapt.h include in ixxat_us_b_core.h and remove version.h
     include (which makes no sense in a in-tree version)
   * Complete previous patches
   * Use pre-allocated cmd buffer instead of allocating/freeing a temporary one
   * ixxat_usb_needs_firmware_update() is used only in OOT version
   * Put #include files in lexicographic order
   * Correct lines longer than 80 characters
   * Fix missing & in front of pointer
   * Keep verbose msg for OOT version only
   * Fix missing brace in condition
   * Since ixxat_usb_disconnect() does kfree(devdata), return immediately
   * fix forgotten kfree(devdata)
   * An in-tree driver does not need to display the kernel version
   * Remove next_dev field from device object
   * Fix multi-lines comment syntax
   * Corrects line breaks exceeding 80 characters; remove an empty line
   * Move variables initialization to where they are declared
   * Change for(;;) loop into while() loop
   * Break on error and save (2) levels of indentation
   * Remove unnecessary whitespaces and rename Windows style var names
   * Return on error so that err is NETDEV_TX_OK which saves levels of indent
   * Arrange the code to replace goto with fallthrough
   * Linux Coding Style: Fix forbidden usage of TAB in declaration
   * Rename all UrbIdx into urb_idx
   * Linux coding style: rename all MsgIdx variables into msg_idx
   * Combine two conditions into one and return next to error
   * Remove redundant condition over urb->status and save one level of indent
   * Replace #ifdef DEBUG by #ifdef IXXAT_DEBUG
   * Move entire braces block into #if/#endif block so avoid empty braces block
     in case unifdef is used over IX_CONFIG_USE_HW_TIMESTAMPS
   * Since the value returned by fixxat_usb_decode_buf() is only compared to 0,
     let's return err and delete ret, which has become useless.
   * Since struct ixxat_can_msg is packed and starts with a byte, can't cast
     can_msg pointer on a unaligned memory address.
   * Factorize statements and change the while() loop to a for(;;) loop
   * Fix Windows style coding variable name and move their declaration to the
     block they're used only.
   * Fix blank lines and a violation of the 80-character rule
     Add a message explaining the error returned
     Add Linux module compilation and intermediate files to .gitignore

### 2.1.2	(2025-09-30)

- add #define IX_CONFIG_USE_HW_TIMESTAMP to be able to switch on/off
  hardware time stamping to support older (maybe 32bit) kernels (e.g. <= 4.9)

### 2.1.1	(2025-09-29)

- rename "USB-to-CAN/FD Standard Brick" to "USB-to-CAN/FD Standard Card"
- add support for device id 08DB:0014 ("USB-to-CAN/FD Standard Module")
- fixes submitted by Stephane Grosjean (stephane.grosjean@hms-networks.com):
   * Remove all local mem allocation and use pre-alloc area to build commands
     To avoid memory fragmentation, command buffer is allocated at probe time,
     then used by each command handler function. This needs to pass the
     "struct ixxat_usb_device_data *devdata" new argument to all of these
     functions (instead of dev_info pointer, sometimes...)
   * ixxat_usb_encode_msg(): simplify code and remoe useless initialization
     - Test RTR flag before everything then CANFD and BRS flag
     - msg_id nor can_msg don't need to be init
   * sysfs files: use snprintf(), simplify code and fix 80 columns rule
   * Remove useless test on devdata and fix 80 columns rule
     ixxat_usb_ts_set_start() is called only once and devdata in a context
     where devdata is not NULL
   * Change "== 0" conditions and use "!" instead and fix 80 columns rule
   * Fix typo in comment
   * Remove useless msleep()
     - usb_control_msg() waits for IXXAT_USB_MSG_TIMEOUT, therefore, no need to
       wait again in case of error
     - loop on retrying to send the request only in case of timeout: no need to
       loop in any other (error) case
     - same for waiting the response
     - simplify error messages
   * Fix Windows style identifiers names
     'Mask' variable is no more used
   * Remove Windows style and optimize loop
     - Local var SHOULD BE declared in the block they're used
     - Use for() loop over msg_cnt instead of while()
     - Set msg_lastindex value when an empty entry has been found, just before
       leaving the loop: this avoids a n+1th test of "msg_cnt" when leaving the
       loop and simplifies the code (ret variable no more needed)
   * Code optimization: do spin_lock/spin_unlock *only* if context != NULL
     No need to block everything in case context is NULL
   * Move TICK_FACTOR definition on top of the C file
   * Since driver_info usage, remove previous version of functions which give
     device name and adapter pointer
   * Fix Windows-style identifiers names
   * Update comment regarding return value
   * Add a util function that gives the name of the USB device
     (see driver_info new usage)
   * Simplify test of FW version
     Add a test on validity of fwinfo pointer too.
   * Statically defines the name and adapter of USB devices
     The “driver_info” field of “struct usb_device_id” is a pointer left free
     for the user to point to a new structure that will statically store the
     name and address of the device adapter, which will simplify code by
     avoiding redundant tests on the USB device ID.
   * Add comment to explain the code change
   * Fix usage of le16 bus_ctrl_count in loop upper limit
   * Introduce IXXAT_DEBUG in replacement to DEBUG
     trace_printk() SHOULDN'T be used outside of a DEBUG kernel but nothing
     prevents to build the OOT driver with -DDEBUG.
     Using IXXAT_DEBUG (instead of DEBUG) allows to get simple traces in
     dmesg log. Of course, in a DEBUG kernel, trace_printk() will be used.
     Code is rearranged to move these local definitions on top of the C source
     files.
   * Reordering the type definition sequence

### 2.0.607	(2025-07-14)

- README.md: add usage patterns
- adjust source to kernel coding guidelines
- fix errors and warnings found by checkpatch.pl

### 2.0.604	(2025-06-03)

- free shared device data on disconnect
- renamed ixxat_usb_device to ixxat_usb_candevice because it is the device struct to encapsulate a single CAN (device)
  create a separate ixxat_usb_device struct that holds usb device specific data (fw info, device info, clock start offset)
  implement different possibilities to synchronize device ticks to host clock, select it via IX_SYNCTOHOSTCLOCK define
- move dmesg output of CAN controller clock settings after device creation
- add ethtool_ops to support hardware timestamps
- fix calculation of timestamps and use of timer overrun messages
- read controller capabilities (includes ts clock info) from devices
- add support for new USB-to-CAN/FD (new device ids)
- cleanup code (move kernel version dependent part to ixxat_kernel_adapt.h)

## Previous packages

prior to 2.0.576 packages had been released packaged as .tgz files:

### 2.0.576	(2024-10-24)

- remove assignments to can.restart_ms as this should be done only by the SocketCAN framework and not the individual driver (ICBT-1301)
- fix empty-body warnings

### 2.0.520	(2024-06-04)

- Call can_put_echo_skb() on current skb after encoding the can message. It seems that calling it before
  sometimes messed up the skb and led to a kernel NULL pointer dereference bug when dereferencing
  skb->data inside ixxat_usb_encode_msg(). This had been seen on different kernel versions (5.x, 6.x)
  happened very sporadic within a very large time frame of 15 minutes up to 5.5 days.
- kernel >= 6.1.0: use can_dev_dropped_skb() instead of can_dropped_invalid_skb() to check skb in ixxat_usb_start_xmit()
- remove call to usb_reset_configuration() in probe because it leads to VMWare to crash later during device usage.
  It seems hat VMWare selects the wrong usb configuration after the reset.
- replace kfree_skb() with dev_kfree_skb() calls

### 2.0.504	(2024-04-15)

- accept command responses with less than the requested size (e.g. USB2CAN V2 FW versions < 1.6.3.0 do not send reserved parts of some response packets)
  but check firmware responses to have at least response header size (12 bytes)
- fix driver access to USB2CAN (fd) devices with firmware 1.0.1 (avoid exec unknown IXXAT_USB_BRD_CMD_GET_FWINFO2 command on CL1 firmware)

### 2.0.492	(2024-04-02)

- cleanup error messages
- USB devices: read firmware version and support sysfs attributes (serial, firmware_version, hardware, hardware_version, fpga_version)
- USB driver: replace unregister_netdev() with unregister_candev() call
- USB driver: read device info only once per device, not during controller init
- disable all trace_printk in release version
- set device address from hardware ID, init dev_id and dev_port to controller index
- add rsvd attributes to ixxat_usb_caps_cmd and ixxat_usb_info_cmd to fix struct sizes (to a multiple of sizeof(DWORD))

### 2.0.456	(2024-02-29)

- fix message reception not working with USB2CAN V2 and firmware version 1.6.3 (ICBT-223)

### 2.0.455	(2024-02-27)

- fix build against kernel version 5.12
- cleanup kernel version dependent code
- replace calls to netif_napi_add() with netif_napi_add_weight()
- handle different signatures of skb and dlc functions depending on kernel version
- solves a problem in message xmit
  The skb could be accessed after a free_skb call, this results in a incorrect behavior

### 2.0.366	(2020-03-12)

- initial version

## Known issues:

### Incompatibility with older firmware versions

Version 2.0.492 introduced more restrictive checking of firmware response packages which caused the driver to not work with
older firmware versions.

Updating the driver to 2.0.504 or higher or updating the firmware to at least 1.6.3.x (USB-to-CAN V2) or 1.7.0.x (USB-to-CAN fd)  resolves this.

### Dropped messages on kernel 4.15 to 4.17 (resolved in kernel 5.4.0)

We observed sporadic dropped messages on Ubuntu 18.04 LTS. According to the current knowledge this is not a driver issue but a problem within the
SocketCAN implementation in the specific kernel version. The problem has been observed with a self compiled kernel 4.17.0 on Ubuntu 18.04.01 LTS
and Ubuntu 16.04.05 LTS even with other CAN devices (Peak USB).

The issue can be provoked with the follwing commands:

    sudo ip link set can0 up type can bitrate 1000000
    sudo ip link set txqueuelen 10 dev can0
    cangen -g 0 -Ii -L8 -Di -n 1000 -i -x can0

If you check the interfaces with

    ip link

you see "qdisc fq_codel" which is an invalid setting for CAN networks:

    3: can0: <NOARP,ECHO> mtu 16 qdisc fq_codel state DOWN mode DEFAULT group default qlen 10 link/can

Changing this to a working value (pfifo_fast), must be done for every CAN:

    sudo tc qdisc replace dev can0 root pfifo_fast

More information on this issue can be found here:

https://github.com/systemd/systemd/issues/9194

According to that source the problem has been with a patch which then had been integrated into kernel 5.4-rc6.
Possible solution for this issue, use either

* sudo tc qdisc replace dev can0 root pfifo_fast
* check/patch config files in /etc/sysctl.d
* or upgrade kernel to version >= 5.4

### Segmentation fault occurs if used within VMWare Workstation

There had been segmentation faults observed if used with VMWare Workstation 15.5.7 on a Windows Host and Ubuntu 16.04 as the guest OS.
Currently there is no solution for this issue.

### CAN message reception errors with Ixxat USB-to-CAN V2 and firmware version 1.6.3

There are possible CAN message reception errors with Ixxat USB-to-CAN V2 and firmware version 1.6.3.
A firmware upgrade resolves this.
