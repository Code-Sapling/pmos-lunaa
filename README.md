# postmarketOS for Realme GT Master Edition (RMX3363, `lunaa`)

Downstream port on the LineageOS 20 kernel (5.4.242 QGKI, SM7325 "yupik").

**Working:** boot to sxmo/sway in ~30s, display 1080x2400 @ 120Hz, touchscreen,
power/volume buttons, audio, **wifi**, **battery and charging**, **GPU
acceleration** (Vulkan via turnip on KGSL), USB networking, SSH, IPA, Venus and
Adreno firmware.

**Not working:** camera (`CONFIG_SPECTRA_CAMERA=n`, needed to make the kernel
build), cellular (no modem driver), X11 (Xorg segfaults in glamor init —
Wayland only).

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
battery, no hardware video decode and no IPA.

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

`install-aports.sh` copies the aports into pmbootstrap's pmaports checkout,
where they live as **untracked** files — a `pmbootstrap pull` or a stray
`git clean` deletes them. Re-run it after any pmaports update. It also installs
the local `mesa` aport as `temp/mesa`, shadowing Alpine's.

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

## Two kernel patches this port needs

Both are in `aports/linux-realme-lunaa/` and both fix the touchscreen, in
different ways. Neither is device-specific — they affect the whole
oplus/OnePlus/Realme SM8350/SM7325 family under any Wayland compositor.

- **`0001-touchpanel-set-ABS_MT_WIDTH_MAJOR-range.patch`** —
  `oplus_touchscreen_v2` declares five `ABS_MT` axes and ranges only three, so
  libinput rejects the device outright and touch never appears at all.
- **`0002-dsi-display-notify-UNBLANK-on-enable.patch`** — the panel blank
  notifications are asymmetric: `dsi_display_disable()` sends `POWERDOWN` from
  the generic DRM path, but the matching `UNBLANK` only comes from an OPlus path
  driven by the legacy DRM DPMS property, which atomic-only wlroots never sets.
  So the first screen blank suspends the touch controller and nothing resumes
  it — touch is dead until reboot.

## Three things Android's userspace does and pmOS does not

Each is a single write that nothing in a mainline userspace performs. All are
automated by services in `device-realme-lunaa`; see CLAUDE.md §4 for why each
one is needed.

- **Boot the ADSP** — `echo 1 > /sys/kernel/boot_adsp/boot`. This is what makes
  the battery work: nothing on the application processor measures it, the data
  comes from charger firmware on the ADSP over `pmic_glink`. Without it
  `/sys/class/power_supply/` is simply empty. (`lunaa-adsp`)
- **Boot the WPSS and trigger qcacld** — `wpss_boot`, then `printf ON >
  /dev/wlan`, which qcacld waits for instead of initialising on modprobe.
  (`lunaa-wifi`)
- **Force `dwc3` into peripheral mode** — otherwise the USB gadget is created,
  `usb0` exists, and the host sees nothing at all. Still in the init shim, not
  yet a service.

## Why `boot-deploy`'s own images are not used

They do not boot. `tools/build-images.sh` applies two mandatory fixups:

1. **One compressor for the whole ramdisk.** An lz4 segment followed by a gzip
   segment makes the kernel panic silently at the logo — indistinguishable from
   a bootloader rejection. Everything must be a single lz4-legacy stream.
2. **An init shim** (`tools/finalinit.c`) that forces `dwc3` into peripheral
   mode and caps `/sys/class/firmware/timeout`, which otherwise adds ~88s to
   boot waiting for firmware that is not in the initramfs.

Folding both into a proper initramfs hook is the main thing standing between
this and an upstreamable port (CLAUDE.md §7).

## GPU

Vulkan works — turnip on the Adreno 642L via KGSL — but only with the local
`aports/mesa` aport. Alpine builds `-Dfreedreno-kmds=msm,virtio`, so turnip's
KGSL backend is not in the stock binary at all, and the aport also carries a
patch making the device selectable through `VK_EXT_physical_device_drm` (which
turnip deliberately hides on KGSL, so wlroots and zink otherwise refuse it).

`device-realme-lunaa` sets `WLR_RENDERER=vulkan`, which **hard-depends on that
local mesa**: with stock Alpine mesa the device comes up with no UI. The file's
own header spells out the pixman fallback.

## Layout

```
aports/     the four local aports: linux-, device-, firmware-realme-lunaa
            and mesa (real copies; pmaports holds untracked ones)
blobs/      stock DTB and firmware tarball -- generated, not committed
tools/      build-images.sh, extract-firmware.sh, install-aports.sh,
            finalinit.c (init shim), rescue-shell.c, vkprobe.c
out/        generated boot.img / vendor_boot.img
```

## Upstreamable fixes found here

Three real bugs affecting other hardware, none needing the proprietary blobs:

- **The touchscreen axis patch** (`0001-` above) — touch never appears under
  Wayland on any device using `oplus_touchscreen_v2`.
- **The DSI UNBLANK patch** (`0002-` above) — touch dies after the first screen
  blank on the whole SM8350/SM7325 OPlus display techpack, for every
  wlroots-based UI since wlroots 0.18 went atomic-only.
- **The dwc3 peripheral-mode requirement** — any SM7325 downstream port looks
  completely dead without it, with no way to see why.

The qcacld `ON` trigger and the `boot_adsp` write are worth writing up too: both
are known in Halium/Droidian circles but undocumented for pmOS.
