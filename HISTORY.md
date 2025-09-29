# ix_usb_can

## History


### 2.1.1	(2025-09-29)

- rename "USB-to-CAN/FD Standard Brick" to "USB-to-CAN/FD Standard Card"
- add support for device id 08DB:0014 ("USB-to-CAN/FD Standard Module")
- merge fixes submitted by Stephane Grosjean (s.grosjean@peak-system.com)
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
