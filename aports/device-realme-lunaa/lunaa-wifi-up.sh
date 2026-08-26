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
# The final write takes several seconds and returns EINVAL. The exact source of
# that errno has not been pinned down -- icnss2 is spinning in its DMS retry
# loop at the time ("DMS QMI connection not established", looking for a MAC from
# a modem subsystem this port never boots), but the timing does not cleanly
# match qcacld's completion timeout. Empirically it is harmless: the driver
# loads and wlan0 appears. Backgrounded so it cannot delay boot either way.
WCN=/sys/devices/platform/soc/17a10040.qcom,wcn6750/wpss_boot

# icnss2 is built-in, so this normally exists already -- but wait a little
# anyway rather than exiting silently, which is indistinguishable from the
# service never having run at all.
i=0
while [ ! -w "$WCN" ] && [ $i -lt 20 ]; do
	sleep 1
	i=$((i + 1))
done
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

# takes several seconds, returns EINVAL, and works anyway (see above)
printf ON > /dev/wlan 2>/dev/null || true
