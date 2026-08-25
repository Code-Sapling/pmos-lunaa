#!/bin/sh
# Build bootable pmOS images for realme-lunaa.
#
# Why this exists: boot-deploy's own boot.img/vendor_boot.img do NOT boot on
# this device. Two things must be fixed up by hand:
#   1. the ramdisk must be ONE lz4-legacy stream (see CLAUDE.md §3)
#   2. an init shim must force dwc3 into peripheral mode or USB never comes up
#
# Prereqs: pmbootstrap install && pmbootstrap export
#          stock_dtb extracted from the vendor_boot backup (CLAUDE.md §6)
set -e
EXPORT=/tmp/postmarketOS-export
OUT=${OUT:-$HOME/pmos-lunaa/out}
DTB=${DTB:-$HOME/pmos-lunaa/blobs/lunaa-yupik.dtb}
PART_SIZE=201326592

WORK=$(mktemp -d); trap 'rm -rf "$WORK"' EXIT
HERE=$(dirname "$0")

# 0. build the init shim if missing or stale. It is a static aarch64 binary, so
#    it is compiled inside pmbootstrap's aarch64 rootfs chroot (qemu binfmt).
if [ ! -x "$HERE/finalinit" ] || [ "$HERE/finalinit.c" -nt "$HERE/finalinit" ]; then
	echo ">>> building finalinit from source"
	CH=${PMB_CHROOT:-$HOME/.local/var/pmbootstrap/chroot_rootfs_realme-lunaa}
	pmbootstrap chroot --rootfs -- apk add -q gcc musl-dev
	cp "$HERE/finalinit.c" "$CH/tmp/finalinit.c"
	pmbootstrap chroot --rootfs -- gcc -static -no-pie -Os -o /tmp/finalinit /tmp/finalinit.c
	cp "$CH/tmp/finalinit" "$HERE/finalinit"
	chmod +x "$HERE/finalinit"
fi

# 1. pmOS initramfs (gzip) -> raw cpio
gzip -dc "$EXPORT/initramfs" > "$WORK/pmos.cpio"

# 2. overlay: our init shim + pmOS's init renamed
mkdir -p "$WORK/ov"
cp "$HERE/finalinit" "$WORK/ov/init"
( cd "$WORK" && mkdir -p x && cd x && cpio -id --quiet < ../pmos.cpio && cp init ../ov/init.pmos )
chmod 755 "$WORK/ov/init" "$WORK/ov/init.pmos"
( cd "$WORK/ov" && printf 'init\ninit.pmos\n' | cpio -o -H newc --quiet --owner root:root > ../overlay.cpio )

# 3. ONE lz4-legacy stream -- the -l is mandatory, kernel RD_LZ4 is legacy-only
cat "$WORK/pmos.cpio" "$WORK/overlay.cpio" > "$WORK/combined.cpio"
lz4 -l -9 -f "$WORK/combined.cpio" "$WORK/ramdisk.lz4"

# 4. cmdline straight from boot-deploy's vendor_boot (carries the live UUIDs,
#    which CHANGE on every 'pmbootstrap install'), minus keys pmOS ignores
CMD=$(python3 -c "
d=open('$EXPORT/vendor_boot.img','rb').read(4096)
print(d[28:28+2048].split(b'\0')[0].decode().replace('PMOS_NO_OUTPUT_REDIRECT pmos_debug ',''))")

mkdir -p "$OUT"
mkbootimg --header_version 3 --base 0x00000000 \
	--kernel "$EXPORT/vmlinuz" \
	--ramdisk "$WORK/ramdisk.lz4" --dtb "$DTB" \
	--vendor_ramdisk "$WORK/ramdisk.lz4" --vendor_cmdline "$CMD" \
	--pagesize 4096 \
	--vendor_boot "$OUT/vendor_boot.img" -o "$OUT/boot.img"

# 5. AVB footers padded to the full partition, so no stale footer survives.
#    Unsigned is fine -- this bootloader does not enforce it (CLAUDE.md §3).
avbtool add_hash_footer --image "$OUT/boot.img" \
	--partition_name boot --partition_size $PART_SIZE
avbtool add_hash_footer --image "$OUT/vendor_boot.img" \
	--partition_name vendor_boot --partition_size $PART_SIZE
echo "built: $OUT/boot.img $OUT/vendor_boot.img"
