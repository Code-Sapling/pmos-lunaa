#!/bin/sh
# Build blobs/lunaa-firmware.tar.gz from a stock LineageOS 20 ROM zip.
#
# These are proprietary Qualcomm blobs. They cannot be redistributed, which is
# why the tarball is not kept in the repo -- everyone builds their own from the
# ROM they already have.
#
#   usage: tools/extract-firmware.sh /path/to/lineage-20.0-*-lunaa.zip
#
# Needs: payload-dumper-go, sudo (for loop mounts). Produces ~39MB staged /
# ~19MB compressed.
set -e
ZIP="$1"
[ -f "$ZIP" ] || { echo "usage: $0 <lineage-20.0-*-lunaa.zip>"; exit 1; }
R="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
WORK="$R/blobs/extract"
STAGE="$R/blobs/fwstage"
mkdir -p "$WORK"

echo ">>> extracting vendor, modem, odm from the payload (a few minutes)"
cd "$WORK"
payload-dumper-go -p vendor,modem,odm -o . "$ZIP"

rm -rf "$STAGE"; mkdir -p "$STAGE"
sudo mkdir -p /mnt/lunaa-v /mnt/lunaa-m /mnt/lunaa-o

echo ">>> vendor: IPA, Venus, Adreno zap shader"
sudo mount -o ro,loop "$WORK/vendor.img" /mnt/lunaa-v
sudo cp -a /mnt/lunaa-v/firmware/. "$STAGE"/
# these two are symlinks into partitions Android mounts at runtime; dangling here
sudo rm -f "$STAGE"/wlan/qca_cld/WCNSS_qcom_cfg.ini "$STAGE"/wlan/qca_cld/wlan_mac.bin
sudo umount /mnt/lunaa-v

echo ">>> modem: WCN6750 wifi + DSPs (modem.* skipped, no modem support)"
sudo mount -o ro,loop "$WORK/modem.img" /mnt/lunaa-m
sudo cp -a /mnt/lunaa-m/image/wpss.* /mnt/lunaa-m/image/adsp.* \
           /mnt/lunaa-m/image/cdsp.* /mnt/lunaa-m/image/qca6750 "$STAGE"/
# icnss2 requests these by bare name at the /lib/firmware top level
sudo cp -a /mnt/lunaa-m/image/qca6750/bdwlan.elf \
           /mnt/lunaa-m/image/qca6750/regdb.bin \
           /mnt/lunaa-m/image/qca6750/qdss_trace_config.cfg "$STAGE"/
sudo umount /mnt/lunaa-m

echo ">>> odm: wifi config"
sudo mount -o ro,loop "$WORK/odm.img" /mnt/lunaa-o
sudo mkdir -p "$STAGE/wlan/qca_cld"
sudo cp /mnt/lunaa-o/vendor/etc/wifi/WCNSS_qcom_cfg.ini "$STAGE/wlan/qca_cld/"
sudo umount /mnt/lunaa-o

sudo chown -R "$(id -u):$(id -g)" "$STAGE"

# No modem subsystem here, so icnss2 cannot reach DMS for the WLAN MAC. Give
# qcacld static locally-administered addresses to fall back on.
if ! grep -qi Intf0MacAddress "$STAGE/wlan/qca_cld/WCNSS_qcom_cfg.ini"; then
	cat >> "$STAGE/wlan/qca_cld/WCNSS_qcom_cfg.ini" <<'INI'

# postmarketOS: no modem subsystem, so DMS cannot supply the WLAN MAC.
Intf0MacAddress=0221AA334455
Intf1MacAddress=0221AA334456
Intf2MacAddress=0221AA334457
Intf3MacAddress=0221AA334458
INI
fi

tar czf "$R/blobs/lunaa-firmware.tar.gz" -C "$R/blobs" fwstage
echo
echo ">>> wrote $R/blobs/lunaa-firmware.tar.gz ($(du -h "$R/blobs/lunaa-firmware.tar.gz" | cut -f1))"
echo ">>> $WORK holds ~2.2GB of extracted images and can be deleted"
