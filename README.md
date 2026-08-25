# postmarketOS for Realme GT Master Edition (RMX3363, `lunaa`)

Downstream port on the LineageOS 20 kernel (5.4.242 QGKI, SM7325 "yupik").

**Working:** boot to sxmo/sway in ~30s, display 1080x2400 @ 120Hz, touchscreen,
power/volume buttons, audio, **wifi**, USB networking, SSH, IPA, Venus and
Adreno firmware.

**Not working:** GPU acceleration (software rendering only), battery/charging
reporting, camera.

This is the first Linux of any kind to run on this model — the pmOS wiki still
lists the device as non-booting.

See **[CLAUDE.md](CLAUDE.md)** for the full technical record: every finding,
every dead end, and why each workaround exists. Read §4 first.

---

## Build it yourself

You need: an unlocked RMX3363, `pmbootstrap` 3.11+, and the stock LineageOS 20
zip for this device. Roughly 30GB of disk and an hour for the first kernel
build.

### 1. Firmware blobs (from your own ROM)

Proprietary Qualcomm firmware is **not** in this repo and cannot be
redistributed. Build it from the ROM you already have:

```sh
tools/extract-firmware.sh ~/Downloads/lineage-20.0-*-lunaa.zip
```

Produces `blobs/lunaa-firmware.tar.gz` (~19MB). Without it there is no wifi, no
hardware video decode, and no IPA.

### 2. The device tree blob

`blobs/lunaa-yupik.dtb` is LineageOS's stock multi-DTB blob, extracted verbatim
from a `vendor_boot` backup. See CLAUDE.md §6 for the extraction snippet —
it is 8MB and device-specific, so it is not committed either.

### 3. Configure and install the aports

```sh
pmbootstrap init            # vendor realme, codename lunaa, aarch64, handset,
                            # downstream kernel, UI: sxmo-de-sway
tools/install-aports.sh
pmbootstrap checksum linux-realme-lunaa device-realme-lunaa firmware-realme-lunaa
```

Add the sxmo config subpackage to `~/.config/pmbootstrap_v3.cfg`:

```
extra_packages = openssh,device-realme-lunaa-sxmo
```

### 4. Build and flash

```sh
pmbootstrap install --password <pw>
pmbootstrap export
tools/build-images.sh

adb reboot fastboot
fastboot flash userdata    /tmp/postmarketOS-export/realme-lunaa.img
fastboot flash boot        out/boot.img
fastboot flash vendor_boot out/vendor_boot.img
fastboot reboot
```

**Flash all three every time.** `pmbootstrap install` regenerates the filesystem
UUIDs and the boot images carry them in the cmdline; mismatched images hang in
`wait_root_partition`.

Then `ssh <user>@172.16.42.1` once the host gets 172.16.42.2 over USB.

---

## Why `boot-deploy`'s own images are not used

They do not boot. `tools/build-images.sh` applies two mandatory fixups:

1. **One compressor for the whole ramdisk.** An lz4 segment followed by a gzip
   segment makes the kernel panic silently at the logo — indistinguishable from
   a bootloader rejection. Everything must be a single lz4-legacy stream.
2. **An init shim** (`tools/finalinit.c`) that forces `dwc3` into peripheral
   mode — without it the USB gadget is created, `usb0` exists, and the host sees
   nothing at all — and caps `/sys/class/firmware/timeout`, which otherwise adds
   ~88s to boot waiting for firmware that is not in the initramfs.

Folding both into a proper initramfs hook is the main thing standing between
this and an upstreamable port (CLAUDE.md §7).

## Layout

```
aports/     the three local aports (real copies; pmaports holds untracked ones)
blobs/      stock DTB and firmware tarball -- generated, not committed
tools/      build-images.sh, extract-firmware.sh, install-aports.sh,
            finalinit.c (init shim), rescue-shell.c, the touchscreen patch
out/        generated boot.img / vendor_boot.img
```

## Upstreamable fixes found here

Both are real bugs affecting other hardware and need none of the blobs:

- **`tools/0001-touchpanel-set-ABS_MT_WIDTH_MAJOR-range.patch`** —
  `oplus_touchscreen_v2` declares five `ABS_MT` axes and ranges three, so
  libinput rejects the device outright. Touch is dead under Wayland on *every*
  oplus/OnePlus/Realme device using that driver.
- **The dwc3 peripheral-mode requirement** — any SM7325 downstream port looks
  completely dead without it, with no way to see why.
