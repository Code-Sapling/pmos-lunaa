#!/bin/sh
# Bring up the WCN6750 wifi on realme-lunaa.
#
# Three things Android's userspace does that postmarketOS does not:
#   1. boot the WPSS (wireless processor) subsystem  -- nothing does this
#      automatically, icnss2 just registers it and waits
#   2. create /dev/wlan -- qcacld registers a "qcwlanstate" chrdev but no
#      device node is made for it
#   3. write "ON" to it -- qcacld does NOT initialise on modprobe; it waits
#      for this trigger, which the Android wifi HAL normally sends
#
# The final write blocks ~30s and then returns EINVAL: icnss2 spins in its DMS
# retry loop looking for a MAC from a modem subsystem this port never boots
# ("DMS QMI connection not established"). That is survivable -- qcacld falls
# through to its own MAC fallback and wlan0 appears anyway. Hence the &.
WCN=/sys/devices/platform/soc/17a10040.qcom,wcn6750/wpss_boot

[ -w "$WCN" ] || exit 0
echo 1 > "$WCN" 2>/dev/null || exit 0
sleep 5

modprobe wlan 2>/dev/null
sleep 3

if [ ! -c /dev/wlan ]; then
	maj=$(awk '/qcwlanstate/{print $1}' /proc/devices)
	[ -n "$maj" ] || exit 0
	mknod /dev/wlan c "$maj" 0 || exit 0
	chmod 660 /dev/wlan
fi

# blocks for the DMS timeout, then wlan0 shows up
printf ON > /dev/wlan 2>/dev/null || true
