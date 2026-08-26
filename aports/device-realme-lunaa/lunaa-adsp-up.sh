#!/bin/sh
# Boot the ADSP (Q6 audio DSP) on realme-lunaa.
#
# This is what makes the battery work. Nothing on the application processor
# measures the battery on this device: charging and gauge data come from
# charger firmware running on the ADSP and are read over pmic_glink.
#
#   drivers/power/oplus/v1/charger_ic/oplus_battery_sm8350.c registers the
#   battery/usb/wls power supplies, but only after pmic_glink_register_client()
#   succeeds. pmic_glink binds to an rpmsg channel named PMIC_RTR_ADSP_APPS
#   (drivers/soc/qcom/pmic_glink.c) -- an ADSP glink edge. No ADSP means no
#   edge, no channel, no probe, and /sys/class/power_supply is empty.
#
# techpack/audio/dsp/adsp-loader.c creates this sysfs node but never boots
# anything itself: its probe ends at
#
#	wqueue:
#		INIT_WORK(&adsp_ldr_work, adsp_load_fw);
#		return 0;
#
# which initialises the work and never schedules it. Only a userspace write of
# 1 (BOOT_CMD) reaches adsp_boot_store() -> adsp_loader_do() ->
# subsystem_get("adsp"). Android's audio HAL does that write; pmOS does not.
# Same shape as the qcacld "ON" trigger in lunaa-wifi-up.sh.
#
# Needs adsp.mdt + adsp.b* in /lib/firmware -- shipped by firmware-realme-lunaa.
BOOT=/sys/kernel/boot_adsp/boot

# adsp_loader_dlkm is normally autoloaded by udev from the qcom,adsp-loader
# device-tree modalias. Load it explicitly rather than exiting silently, which
# is indistinguishable from the service never having run.
[ -e "$BOOT" ] || modprobe adsp_loader_dlkm 2>/dev/null

i=0
while [ ! -w "$BOOT" ] && [ $i -lt 20 ]; do
	sleep 1
	i=$((i + 1))
done
[ -w "$BOOT" ] || exit 0

echo 1 > "$BOOT" 2>/dev/null || exit 0

# PIL load plus the charger probe take a couple of seconds; nothing depends on
# this finishing, it is only here so the log below reflects the end state.
i=0
while [ ! -d /sys/class/power_supply/battery ] && [ $i -lt 15 ]; do
	sleep 1
	i=$((i + 1))
done
exit 0
