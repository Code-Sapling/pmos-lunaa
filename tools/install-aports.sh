#!/bin/sh
# Copy this port's aports into pmbootstrap's pmaports checkout.
#
# They live there as UNTRACKED files in a git repo pmbootstrap manages, so a
# `pmbootstrap pull` or a stray `git clean` can delete them. The copies in
# aports/ are the real ones; re-run this after any pmaports update.
set -e
SRC="$(CDPATH= cd -- "$(dirname -- "$0")/../aports" && pwd)"
DST="${PMAPORTS:-$HOME/.local/var/pmbootstrap/cache_git/pmaports}/device/downstream"

[ -d "$DST" ] || { echo "pmaports not found at $DST (set PMAPORTS=)"; exit 1; }

for pkg in linux-realme-lunaa device-realme-lunaa firmware-realme-lunaa; do
	mkdir -p "$DST/$pkg"
	cp -a "$SRC/$pkg/." "$DST/$pkg/"
	echo "installed $pkg"
done

# Two large generated files are deliberately not kept in aports/:
#   lunaa-yupik.dtb        -> blobs/ (extract from the stock vendor_boot, §6)
#   lunaa-firmware.tar.gz  -> tools/extract-firmware.sh (proprietary, not
#                             redistributable; build it from your own ROM)
[ -f "$SRC/../blobs/lunaa-yupik.dtb" ] &&
	cp "$SRC/../blobs/lunaa-yupik.dtb" "$DST/device-realme-lunaa/" &&
	echo "installed lunaa-yupik.dtb"
[ -f "$SRC/../blobs/lunaa-firmware.tar.gz" ] &&
	cp "$SRC/../blobs/lunaa-firmware.tar.gz" "$DST/firmware-realme-lunaa/" &&
	echo "installed lunaa-firmware.tar.gz"

echo
echo "Now regenerate checksums (the two blobs above are not checksummed here):"
echo "  pmbootstrap checksum linux-realme-lunaa device-realme-lunaa firmware-realme-lunaa"
