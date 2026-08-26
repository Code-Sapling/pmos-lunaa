# Claude Code Brief: postmarketOS port for Realme GT Master Edition (realme-lunaa)

You are helping port postmarketOS to a Realme GT Master Edition. A previous session spent an extended effort on a Halium 13 / Ubuntu Touch port that failed; this document captures everything learned so you do not repeat it.

**Read section 3 ("What Has Already Been Ruled Out") before proposing any theory about why something doesn't boot.** Several obvious hypotheses have already been tested and eliminated.

---

## 0. Critical constraints — read first

> **STATUS: the port works.** See §4. Boots in ~30s to sxmo/sway with display,
> touch, buttons, audio, wifi, USB networking and SSH. Reproducible from three
> local aports + `tools/build-images.sh`. Read §4 before §5 — much of §5 is
> reference material from the bring-up, not a procedure to follow from scratch.
>
> **Where things live.** The aports' real home is `aports/` **in this repo**.
> `tools/install-aports.sh` copies them into pmbootstrap's pmaports checkout,
> where they exist only as *untracked* files — a `pmbootstrap pull` or a stray
> `git clean` in that checkout deletes them. Re-run the script after any pmaports
> update, then `pmbootstrap checksum` the three packages.
>
> Also: `tools/` (build script, firmware extractor, init shim, kernel patch,
> rescue image), `blobs/` (stock DTB + firmware tarball — generated, see
> `tools/extract-firmware.sh`), `out/` (generated images), `~/pmos-backup/`
> (stock partition backups). `README.md` has the from-scratch build procedure.

**You cannot flash the device. The human does that.** You build images and give exact commands; they run them and report back. Never assume a flash succeeded.

**Normal fastboot is broken on this device.** The bootloader was unlocked via an EDL "Qualcomm Toolbox" method that installs a patched ABL. Consequences:
- `fastboot` (bootloader mode) crashes and reboots — unusable
- **`fastboot boot` (RAM-only test boot) is NOT available.** Every test is a real write to flash. Do not propose `fastboot boot` as a debugging step.
- **`fastbootd` works well** (`adb reboot fastboot`, userspace fastboot from the LineageOS recovery ramdisk). It can **flash `boot`, `vendor_boot`, `dtbo` and `vbmeta`** — all four confirmed working.
- **`fastboot fetch` only works on `vendor_boot`.** Verified 2026-08-25: the
  bootloader answers `Fetch is only allowed on [vendor_boot, vendor_boot_a,
  vendor_boot_b]`. It cannot fetch `boot`, `dtbo` or `vbmeta`. Use EDL or a
  root shell in recovery to read anything else.
- EDL (9008) always works and is the recovery path of last resort.

**pstore does not record, but the boot IS observable — see below.**
`/sys/fs/pstore` is empty even after failed boots, read as root from recovery.

**There IS a serial UART, contrary to what this document previously claimed.**
The stock DTB's `bootargs` prepend `earlycon=msm_geni_serial,0x994000` to every
boot, and ABL appends `printk.disable_uart=1` which mutes it. Removing that
parameter (and adding `console=ttyMSM0,115200n8 earlycon`) should produce real
console output on the SBU pins. This still needs a USB-C SBU adapter, but it is
a known-good path rather than a hope.

**Without hardware, use the dmesg-exfiltration technique (proven, 2026-08-25).**
A static `/init` can read the whole kernel ring buffer with `klogctl()` and
`dd` it to an unused partition on the *inactive* slot, which is then read back
via EDL. This is how the first kernel log from this device was ever obtained.
See §10. Do not claim a failure is unobservable before trying it.

**Iteration is expensive.** Roughly 5 minutes per attempt via fastbootd, 20+ minutes if an EDL restore is needed. Prefer changes that test multiple hypotheses at once, and always explain what a given attempt would prove.

---

## 1. Device facts (all verified, do not re-derive)

| Property | Value |
|---|---|
| Model | Realme GT Master Edition, RMX3363 |
| Codename | `lunaa` |
| SoC | Qualcomm SM7325 (Snapdragon 778G), platform name `yupik` |
| SoC relation | SM7325 is essentially identical to SC7280; mainline `sm7325.dtsi` just includes `sc7280.dtsi` |
| Partitions | A/B slots, dynamic partitions (`super`), **active slot `_b`** |
| Boot header version | **3** (separate `vendor_boot` and `dtbo` partitions) |
| Screen | 1080 × 2400 AMOLED |
| Current OS | LineageOS 20 (Android 13) |
| `ro.vndk.version` | 33 |
| `ro.board.first_api_level` | 30 (launched Android 11) |
| Kernel | `5.4.242-qgki-ga04ba60c4d6c`, QGKI, module-heavy |

### Partition block devices (from `/dev/block/by-name/`)

```
boot_a        -> sde11        boot_b        -> sde69
dtbo_a        -> sde15        dtbo_b        -> sde41
vendor_boot_a -> sde23        vendor_boot_b -> sde70
vbmeta_a      -> sde14        vbmeta_b      -> sde40
super         -> sda12
vbmeta_system_a -> sda6       vbmeta_system_b -> sda7
vbmeta_vendor_a -> sda8       vbmeta_vendor_b -> sda9
```

### Boot image parameters (extracted from stock via `unpack_bootimg.py`)

```
header version:        3
page size:             0x1000 (4096)
kernel load address:   0x00008000
ramdisk load address:  0x01000000
tags load address:     0x00000100
dtb load address:      0x01f00000
base:                  0x00000000
kernel format:         raw (uncompressed Image)
ramdisk format:        lz4_legacy
boot.img cmdline:      (empty — everything is in vendor_boot)
```

### vendor_boot cmdline (verbatim from LineageOS)

```
androidboot.hardware=qcom androidboot.memcg=1 androidboot.usbcontroller=a600000.dwc3 cgroup.memory=nokmem,nosocket loop.max_part=7 lpm_levels.sleep_disabled=1 msm_rtb.filter=0x237 pcie_ports=compat service_locator.enable=1 swiotlb=0 ip6table_raw.raw_before_defrag=1 iptable_raw.raw_before_defrag=1
```

(LineageOS also appends `buildvariant=userdebug`, dropped as a build artifact.)

---

## 2. Source repositories

| Component | Repo | Branch |
|---|---|---|
| Device tree | `github.com/pjgowtham/android_device_realme_lunaa` | `lineage-20` |
| Common tree | `github.com/pjgowtham/android_device_oneplus_sm8350-common` | `lineage-20` |
| Kernel | `android_kernel_oneplus_sm8350` — see warning below | `lineage-20` |

### ⚠️ Kernel repo warning

The correct repo is **`android_kernel_oneplus_sm8350`** (`oneplus`), as declared in the common tree's `lineage.dependencies`:

```json
{ "repository": "android_kernel_oneplus_sm8350", "target_path": "kernel/oneplus/sm8350" }
```

**Do NOT use `pjgowtham/android_kernel_oplus_sm8350`** (`oplus`, not `oneplus`) — that is a different repo marked "Only meant for Q3s/9SE" with only an `RMX3461` branch. Wrong device.

**Pin this commit:** `a04ba60c4d6cb892f72e5415b8dd1a5810393052`, from
`pjgowtham/android_kernel_oneplus_sm8350`. That is the exact commit the shipped
LineageOS 20 kernel was built from — the device reports
`5.4.242-qgki-ga04ba60c4d6c`.

Caveats, both verified:

- The commit is **no longer reachable from any branch head**. The branch was
  force-pushed at some point, so it has diverged from every branch
  (`lineage-20` is 22 ahead / 24 behind). GitHub still serves it by SHA and
  abuild checksums it, so the build is reproducible — but do not "fix" it by
  bumping to a branch head without a reason.
- The config chain below *is* present at that commit, and both files are
  byte-identical to their copies at later commits.

An earlier Halium attempt used a personal fork (`Code-Sapling/...`, branch
`halium`). **That fork is deleted and must not be referenced.** Its kernel
version string (`g2a9ce200ee27`) was previously recorded in section 1 as if it
were the ROM kernel; it was not, it was a locally built one.

**Do not merge upstream LineageOS commits into pjgowtham's tree.** The fork is ~22 commits ahead / ~4700 behind, and merging changes vermagic and exported-symbol CRCs so the existing vendor modules stop loading. Keep the diff from the known-working ROM kernel minimal.

### Kernel config chain (important)

```
BoardConfigCommon.mk:112  TARGET_KERNEL_CONFIG := vendor/lahaina-qgki_defconfig
BoardConfig.mk:16         TARGET_KERNEL_CONFIG += vendor/oplus_yupik_QGKI.config
```

Merge order matters:

```bash
cd <kernel>
ARCH=arm64 scripts/kconfig/merge_config.sh -O out \
  arch/arm64/configs/vendor/lahaina-qgki_defconfig \
  arch/arm64/configs/vendor/oplus_yupik_QGKI.config
```

Note the tree only builds `yupik-*` DTBs — there is no lunaa-specific DTB. Board configuration comes from **dtbo overlays**.

### ⚠️ `BRAND_SHOW_FLAG=realme` is mandatory

`drivers/power/Makefile`:

```make
ifeq ($(strip $(CONFIG_OPLUS_CHG_OP9RT_PMIC_VOOCPHY)), y)
	obj-$(CONFIG_OPLUS_SM8350_CHARGER)	+= oplus/
else ifeq ($(strip $(BRAND_SHOW_FLAG)), realme)
	obj-$(CONFIG_OPLUS_SM8350_CHARGER)	+= oplus/
else
	obj-$(CONFIG_OPLUS_SM8350_CHARGER)	+= oplus_chg/
endif
```

`BRAND_SHOW_FLAG` is a **make variable supplied by the Android build system**,
not a Kconfig symbol — no defconfig can set it. LineageOS passes `realme` for
lunaa. Without it the build falls into the `else` and compiles
`drivers/power/oplus_chg/` (the OnePlus charger) instead of
`drivers/power/oplus/` (the realme one), which fails with ~20 errors of the form
`struct oplus_chg_chip has no member named usb_enum_status`. Correct compiler,
correct config, wrong source directory.

Pass it on the make command line:

```sh
make O=out ARCH=arm64 BRAND_SHOW_FLAG=realme ...
```

`arch/arm64/boot/dts/vendor/Makefile` branches on the same variable.
(Later `lineage-20` commits fix this in the Makefile so the flag is no longer
needed — another reason the pinned ROM commit needs it and a branch head may not.)

### Vendor modules

The stock `vendor_boot` ramdisk contains only **7 modules** and only a `modules.load.recovery` (no `modules.load`):

```
adsp_loader_dlkm.ko, apr_dlkm.ko, msm_drm.ko, q6_notifier_dlkm.ko,
q6_pdr_dlkm.ko, snd_event_dlkm.ko, + modules.{alias,dep,softdep}
modules.load.recovery contains: adsp_loader_dlkm.ko, msm_drm.ko
```

The full module set lives on `vendor_dlkm` inside `super`, loaded later by Android's second-stage init.

---

## 3. What has been established (verified 2026-08-25)

**This section was rewritten after a long debugging session. Several claims in
the previous version were false and had misdirected the port for weeks.** Each
item below is backed by a specific experiment, named.

### Confirmed working

1. **Image construction is byte-exact.** Feeding stock inputs (stock kernel,
   stock ramdisk, stock DTB, stock cmdline) through
   `mkbootimg --header_version 3 --base 0x00000000` reproduces LineageOS's
   `boot.img` **and** `vendor_boot.img` byte-for-byte (`cmp` clean). Verified
   offline, costs nothing — **do this first any time image validity is in
   question.**

2. **Unsigned AVB footers boot fine.** An image with content byte-identical to
   stock but carrying an unsigned `avbtool add_hash_footer` footer
   (`Algorithm: NONE`, `Rollback Index: 0`, vs stock's `SHA256_RSA4096` /
   `1696550400`) **boots LineageOS normally**. Anti-rollback and AVB signing are
   NOT enforced on `boot`. `androidboot.verifiedbootstate=orange`.

3. **The bootloader executes anything we give it.** Follows from 2. ABL is not
   rejecting custom images and never was.

4. **Our kernel boots and runs arbitrary userspace.** Confirmed by dmesg:
   `Linux version 5.4.242-qgki (pmos@Abdullah-PC) ... #1-postmarketOS`.

5. **USB gadget hardware works.** `/sys/class/udc` contains `a600000.dwc3`
   (plus `dummy_udc.0`), and both `a600000.ssusb -> msm-dwc3` and
   `a600000.dwc3 -> dwc3` are **bound**. There is no driver obstacle to USB
   networking.

6. **Flashes land on the active slot** (`_b`) and take effect.

### ⚠️ The bug that cost ~6 flash cycles: ramdisk compressor mixing

**Do not build a ramdisk by appending a differently-compressed archive to an
existing one.** The kernel's initramfs unpacker handles multiple concatenated
segments, but on this kernel an **lz4 segment followed by a gzip segment fails
fatally** — the unpack aborts, no `/init` survives, and the kernel panics
silently at the Realme logo. It looks exactly like a bootloader rejection.

| Ramdisk layout | Result |
|---|---|
| stock lz4 + appended gzip cpio | **hang** (this was the bug) |
| single lz4-legacy stream containing concatenated *uncompressed* cpios | **works** |
| Android vendor lz4 + our boot lz4 (two lz4 segments) | **works** |

Rule: **one compressor for every segment.** Stock uses lz4-legacy (magic
`02 21 4c 18`) everywhere. Build ours the same way:

```sh
lz4 -d stock_generic_ramdisk stock.cpio     # decompress
cat stock.cpio overlay.cpio > combined.cpio # concat UNCOMPRESSED cpios
lz4 -l -9 combined.cpio ramdisk.lz4         # ONE legacy-lz4 stream
```

Note the kernel's `RD_LZ4` only understands **legacy** lz4 (`0x184C2102`), not
the modern frame format (`0x184D2204`). `lz4 -l` is mandatory.

### ⚠️ dwc3 must be forced into peripheral mode (THE USB FIX)

**Confirmed 2026-08-25 on hardware.** pmOS's initramfs runs correctly and
`setup_usb_network` succeeds — the gadget `g1` is created, `usb0` appears in
`/proc/net/dev`, `/dev/ttyGS0` exists — but **the host still sees nothing**.

Cause: `dwc3` runs dual-role and stays idle until an extcon notifier reports a
cable attach. That notifier comes from the Type-C / charger vendor drivers,
which live on `vendor_dlkm` as **modules we do not load**. The controller is
bound and healthy; it simply never asserts the pullup.

Fix — write `peripheral` to the msm-dwc3 mode knob:

```sh
echo peripheral > /sys/bus/platform/devices/a600000.ssusb/mode
```

USB enumerated on the host within seconds of this write. Fallback paths worth
trying in order: `/sys/devices/platform/soc/a600000.ssusb/mode`, then
`/sys/bus/platform/devices/a600000.dwc3/mode`.

Do this **before** `setup_usb_network`, and re-assert it periodically — the
gadget/UDC binding does not survive a mode change made afterwards in all cases.
This belongs in the device package as an initramfs hook.

Neither `echo a600000.dwc3 > g1/UDC` nor
`echo connect > /sys/class/udc/a600000.dwc3/soft_connect` was needed once the
mode was right.

### ⚠️ `pmos.debug-shell` breaks USB networking on this device

Confirmed on hardware. `debug_shell()` in `init_functions.sh` does:

```sh
have_udc="$(cat $CONFIGFS/g1/UDC)"
if [ -n "$have_udc" ]; then setup_usb_acm_configfs; else start_unudhcpd; fi
```

`setup_usb_acm_configfs` unbinds the UDC to add the ACM function, and on this
device the rebind drops `dwc3` back out of peripheral mode. Symptom: the host
interface still enumerates but goes `NO-CARRIER`, and ping stops working —
strictly worse than not enabling the debug shell at all.

**Workaround:** do not set `pmos.debug-shell`. Start `telnetd` independently
instead (busybox-extras is already in the initramfs):

```sh
/bin/busybox-extras telnetd -F -p 23 -l /bin/sh
```

Re-assert `peripheral` mode periodically regardless — anything that rebinds the
UDC can knock the controller back to idle.

### Getting a shell when pmOS's init stalls

A **rescue init** works reliably and is the fastest way to inspect a failing
boot. Static, non-PIE C that: mounts proc/sysfs/devtmpfs/devpts/configfs,
forces `peripheral` mode, builds the gadget via configfs, sets the IP with
`ioctl(SIOCSIFADDR)`, and serves a shell on TCP 23 from its own `accept()` loop
`exec`ing `/bin/busybox sh -i`. Source lives in the session scratchpad as
`shell.c` / `finalinit.c`.

Do **not** rely on busybox `telnetd` for this — invoking it from a static init
failed repeatedly here (busybox picks the applet from `argv[0]`, and
`busybox-extras` is not itself an applet name). A hand-rolled listener has far
fewer failure modes.

**Host-side gotcha that wasted a cycle:** without pmOS's `start_unudhcpd` the
host gets no DHCP lease, and reflashing recreates the netdev so any manual
address is lost. `ping`/`nc` then fail before a packet leaves the PC and it
looks like the device is dead. Always check:

```sh
ip -br addr show <iface>          # must show 172.16.42.2/24
sudo ip addr add 172.16.42.2/24 dev <iface>
```

**In a rescue shell the busybox applets are not symlinked** (pmOS's
`busybox --install -s` never ran), so `cat`, `ls`, `uname` are "not found".
Prefix everything with `/bin/busybox`, or run `/bin/busybox --install -s` first.

### The rootfs must actually be flashed

`pmos_root_uuid` / `pmos_boot_uuid` on the cmdline are matched against real
filesystems. `realme-lunaa.img` is a GPT disk image holding `pmOS_boot` (ext2)
and `pmOS_root` (ext4) whose UUIDs match those cmdline values; it goes to
**`userdata` = `/dev/sda13`**. Read it locally with
`losetup -fP --sector-size 4096` — 4096 is required, which is why
`deviceinfo_rootfs_image_sector_size="4096"` matters.

If it is not flashed, `blkid /dev/sda13` is empty, `wait_root_partition` spins,
and `fail_halt_boot` eventually reboots the device (~90 s) — which looks like a
crash but is not.

### Also true

7. **The kernel is good.** Independently reconfirmed — it is the same binary in
   items 4 and 5 above.
8. **USB drivers are built-in**, but note `CONFIG_USB_DWC3_QCOM` is **not set**
   and never was; the driver actually used is `CONFIG_USB_DWC3_MSM=y` (the
   downstream Qualcomm glue). The previous version of this document asserted
   `USB_DWC3_QCOM=y` was "verified"; it is not in the config.
9. **`RD_LZ4=y` and `RD_GZIP=y`** are both present.

### Claims removed from this section as false

- ~~"AVB / verified boot is NOT blocking"~~ — the *conclusion* is right (item 2)
  but the original evidence was not reproduced; item 2 is the real proof.
- ~~"`fastboot fetch` reads boot/vendor_boot/dtbo/vbmeta"~~ — only `vendor_boot`.
- ~~"`CONFIG_USB_DWC3_QCOM=y` verified"~~ — absent from the config.
- ~~"There is no serial console"~~ — there is; see §0.
- ~~"a hang at the logo means the bootloader/kernel"~~ — a **userspace panic is
  indistinguishable from a bootloader rejection** on this device. Both are a
  silent hang at the logo. Never infer the layer from the symptom.

### The Halium failure symptom (for reference)

Every attempt: hang at the Realme logo with the orange unlocked-bootloader
warning, no USB RNDIS interface, no adb, no recovery, no fastbootd, empty
pstore. Only EDL recovers.

**Note:** a hang at the logo is expected for any non-Android boot — nothing
repaints the framebuffer. Judge success by whether a USB network interface
appears, not by the screen. Given the compressor-mixing finding above, it is
plausible Halium failed for a related initramfs-packing reason rather than
anything device-specific.

## 4. STATUS: THE PORT WORKS (2026-08-25)

**postmarketOS boots on realme-lunaa, with a working touchscreen UI.**
sxmo on sway, display at 1080x2400/120Hz, touch, hardware buttons, USB
networking and SSH. Boots in ~30s. This is the first Linux of any kind to run
on this model — the pmOS wiki still lists it as non-booting.

What made it work, after ~15 flash cycles:

1. **One compressor for the whole ramdisk** (§3). Appending a gzip cpio onto
   the lz4 ramdisk silently panics the kernel and looks exactly like a
   bootloader rejection. This was the single biggest time sink and it was
   self-inflicted.
2. **Force `dwc3` into peripheral mode** (§3). Without it the gadget is created,
   `usb0` exists, and the host sees nothing at all.
3. **Flash the rootfs, and rebuild the boot images when you do** (§3, §11).

Build with `tools/build-images.sh`; the init shim source is
`tools/finalinit.c`, and `tools/rescue-shell.c` is the standalone recovery
image. Nothing here needs a serial cable.

### Display works (2026-08-25)

`msm_drm.ko` loads, `/dev/dri/card0` + `renderD128` exist, and `modetest`
paints the panel correctly at **both 120 Hz and 60 Hz**:

```sh
sudo modetest -M msm_drm -s 56:#0 -v     # connector 56 = DSI-1, #0 = preferred
sudo sh -c 'echo 1500 > /sys/class/backlight/panel0-backlight/brightness'
```

Driver name is **`msm_drm`**, not `msm`. Connector `DSI-1` id 56. Mode names are
non-standard (`1080x2400x120x138333cmd`) so select by index (`#0`).
Panel: **samsung AMS643YE01, DSC, command mode**, `backlight type=dcs`,
`topology=sde_singlepipe_dsc`. Backlight only responds once a DRM master has
powered the panel — set brightness *after* starting modetest, not before.

Touchscreen works too: focaltech FT3518 on `a94000.i2c`, `input3`/`input4`.

### The modules fix (required, see §4 history)

`downstreamkernel_package` installs **only** `vmlinuz` + `kernel.release` — it
has no `modules_install` step. Without the addition below the device boots with
zero modules: no display, no charging, no wifi. Added to
`linux-realme-lunaa/APKBUILD` `package()`:

```sh
unset LDFLAGS
make -C "$builddir" O="$_outdir" ARCH="$_carch" CC="${CC:-gcc}" \
	BRAND_SHOW_FLAG=realme \
	INSTALL_MOD_PATH="$pkgdir" INSTALL_MOD_STRIP=1 \
	modules_install
```

`BRAND_SHOW_FLAG=realme` is needed here too, not just in `build()`. Result: 84
modules including `msm_drm.ko` and `wlan.ko`, with full depmod metadata. udev
autoloads them by modalias — no `modules.load` ordering file was needed.

### ⚠️ Touchscreen: kernel patch required (libinput rejects the device)

`oplus_touchscreen_v2/touchpanel_common_driver.c` declares **five** axes with
`set_bit()` — `ABS_MT_TOUCH_MAJOR`, `ABS_MT_WIDTH_MAJOR`, `ABS_MT_POSITION_X`,
`ABS_MT_POSITION_Y`, `ABS_MT_PRESSURE` — but ranges only three of them.
`ABS_MT_WIDTH_MAJOR` is never ranged and `ABS_MT_PRESSURE`'s
`input_set_abs_params()` call is **commented out** in the vendor source, so both
end up `min == max == 0` while the driver still reports values on them.

libinput refuses the whole device:

```
event3 - touchpanel: kernel bug: device has min == max on ABS_MT_WIDTH_MAJOR
event3 - touchpanel: kernel bug: device has min == max on ABS_MT_PRESSURE
```

**Fix both in one go.** They surface one at a time — fixing only WIDTH_MAJOR
just reveals PRESSURE on the next boot, costing an extra flash cycle. Audit
every `set_bit(ABS_...)` against every `input_set_abs_params()` before building.

Verified working: `libinput list-devices` reports
`Capabilities: keyboard touch` with no `kernel bug` line.

`libinput debug-events` then emits no `DEVICE_ADDED` for it, wlroots/sway never
see a touch device, and the touchscreen is completely dead under Wayland.
Android does not validate axis ranges, which is why the vendor never noticed.

Fixed by `0001-touchpanel-set-ABS_MT_WIDTH_MAJOR-range.patch` (one line, next to
the existing `ABS_MT_TOUCH_MAJOR` call). **Worth sending upstream** — it affects
every oplus/OnePlus/Realme device using `oplus_touchscreen_v2` under Wayland.

Note `INPUT_PROP_DIRECT` *is* set by the driver (`PROP=2`), so a udev rule
forcing `ID_INPUT_TOUCHSCREEN=1` does **not** help. The axis range is the issue.

### sxmo needs a deviceprofile

sxmo matches on the device-tree compatible, here **`qcom,yupik-idp`**, and warns
`No deviceprofile found` without one. Profiles are plain shell scripts at
`/usr/bin/sxmo_deviceprofile_<compatible>.sh`:

```sh
export SXMO_MONITOR="DSI-1"
export SXMO_SWAY_SCALE="3"
```

Scale 3 gives 360x800 logical px, same as the Fairphone 4. Without it the UI is
unusably small at 1080x2400.

### No GPU acceleration — use `WLR_RENDERER=pixman`

Mesa has no driver for this Adreno: `eglInitialize` fails with
`DRI2: failed to load driver`, zink then fails with no Vulkan ICD, and wlroots'
vulkan renderer fails the same way. **Xorg segfaults** in glamor init because of
this (`AccelMethod none` does not prevent it) — X11 UIs are effectively
unusable. Sway works via `WLR_RENDERER=pixman` in
`/usr/share/tinydm/env-wayland.d/10-pixman.sh`. Software rendering only.

**Why GPU acceleration cannot work as shipped (verified 2026-08-25):**

- The Adreno 642L is behind **KGSL** (`/dev/kgsl-3d0`), not a DRM GPU node.
  `/dev/dri/renderD128` belongs to the *display* driver (`msm_drm`/SDE).
- `MESA_LOADER_DRIVER_OVERRIDE=kgsl` does nothing: mesa's GL path goes through
  GBM, which opens a DRM node. `kgsl_dri.so` exists but GBM cannot route to it.
  KGSL support in mesa lives in **Turnip** (`tu_knl_kgsl.c`), the Vulkan driver.
- **Alpine's `mesa-vulkan-freedreno` is built WITHOUT the KGSL backend.**
  Confirmed by inspecting `/usr/lib/libvulkan_freedreno.so`: the string
  `/dev/kgsl-3d0` is absent and there are 0 kgsl symbols (vs 16 msm/drm refs).
  Turnip therefore probes only DRM devices:

  ```
  TU: error: tu_knl.cc:406: device /dev/dri/renderD128 (msm_drm)
      is not compatible with turnip (VK_ERROR_INCOMPATIBLE_DRIVER)
  ```

  Do not waste time on `TU_DEBUG`, permissions or env vars — the code is not
  in the binary. Checking `strings ... | grep /dev/kgsl` takes seconds and
  settles it.

Enabling it would need a **local mesa aport** built with
`-Dfreedreno-kmds=msm,kgsl`. Two further obstacles even then: `/dev/ion` is
`root:root 0600` (Turnip's KGSL path allocates through ION on 5.4) and
`/dev/dma_heap/` does not exist. Treat this as a separate project with an
uncertain outcome, not a quick win.

### ⚠️ Boot takes ~130s without capping the firmware timeout

`/sys/class/firmware/timeout` defaults to **60**. Two built-in Qualcomm
subsystems request firmware that lives on the Android `vendor` partition (which
pmOS does not mount), the request falls back to a userspace helper that does not
exist in the initramfs, and each one blocks for the full 60 seconds:

```
+30.7s at  62.43   ipa_fws: Failed to locate yupik_ipa_fws.mdt (rc:-110)
+57.3s at 123.87   ipa_fws: Failed to locate yupik_ipa_fws.mdt (rc:-110)
+ 3.6s at 127.97   venus:   Failed to locate vpu20_1v.mdt      (rc:-110)
+ 2.2s at 130.22   EXT4-fs (loop0p2): re-mounted   <- rootfs finally mounts
```

`rc:-110` is `-ETIMEDOUT`. The init shim writes `1` to
`/sys/class/firmware/timeout` before handing off, which fails these fast.

**Measured result: boot went from ~158s to ~30s**, and sxmo's background,
clock and menu now appear immediately instead of ~30s late. The same three
firmware failures still appear in dmesg, just at 1s each.

**OpenRC is not the problem** — measured with `rc_logger="YES"` in
`/etc/rc.conf`, sysinit+boot+default complete in ~7 seconds total. Do not go
hunting for slow services; find kernel-side gaps instead:

```sh
dmesg | awk -F'[][]' '{t=$2+0; if (p && t-p > 1) printf "+%6.1fs at %8.2f  %s\n", t-p, t, substr($0,1,110); p=t}'
```

The real fix is to extract `/vendor/firmware*` from the stock ROM into
`/lib/firmware`, which would also restore IPA (network offload) and Venus
(hardware video decode).

### sxmo: set `SXMO_NO_MODEM=1` in the deviceprofile

`sxmo_hook_start.sh` checks `command -v ModemManager`, and the binary is
installed even after `rc-update del modemmanager`. It then starts
`sxmo_modemmonitor` against a dead DBus service **and takes a 120-second
wakelock** to "let the modem warm up". There is no modem driver on this port.

### ⚠️ Never `modprobe -r wlan` — it reboots the device

qcacld-3.0's module exit path tears down the icnss2/wpss subsystem and hangs the
kernel: everything freezes (ssh, sway, touch, buttons) for ~10s and the device
hard-reboots. Load `wlan.ko` once and leave it. To retry, reboot first.

### Vendor firmware: extracted and working

`/vendor/firmware` from the LineageOS ZIP has everything the kernel asks for:

```sh
payload-dumper-go -p vendor lineage-20.0-*-lunaa.zip     # -> vendor.img (ext4)
sudo mount -o ro,loop vendor.img /mnt/vendor-img
# 7.6MB total; the .mdt sets that matter:
#   yupik_ipa_fws.*  (IPA)   vpu20_1v.*  (Venus)   a660_zap.*  (Adreno zap shader)
#   wlan/qca_cld/{WCNSS_qcom_cfg.ini,wlan_mac.bin}
```

WCN6750 wifi firmware is **not** on `vendor` — it is on the physical `modem`
partition, which mounts directly with no device-mapper needed:

```sh
sudo mount -o ro /dev/disk/by-partlabel/modem_b /mnt/modem
ls /mnt/modem/image/qca6750/      # bdwlan.* board data
ls /mnt/modem/image/wpss.mdt      # WLAN subsystem processor firmware
```

Confirmed loading: `ipa_fws: Brought out of reset`, and
`icnss2 17a10040.qcom,wcn6750: Platform driver probed successfully`.

**Do not bother mapping `vendor` from `super`.** `make-dynpart-mappings` produces
a misaligned mapping on this device (reading `/dev/mapper/vendor_a` returns SQL
text from elsewhere in the partition, and `blkid` finds no filesystem). Use the
ZIP instead.

### The firmware/initramfs timing trap

`request_firmware()` at ~0.77s fails with `-2` because **`/lib/firmware` lives in
the rootfs, which is not mounted yet** — that early in boot only the initramfs
exists. The driver then falls back to the userspace helper and waits out
`/sys/class/firmware/timeout`.

So the two obvious settings trade off against each other:

- `timeout=60` — IPA loads (on retry at ~63s, once the rootfs is up) but boot is slow
- `timeout=1`  — boot is fast but the retry happens before the rootfs mounts, so
  the firmware never loads at all

The real fix is to ship the early-needed blobs **inside the initramfs** via a
`/usr/share/mkinitfs/files/*.files` list in the device package, so the first
request succeeds and the timeout is irrelevant.

### Packaging: what is reproducible now

Three local aports carry everything that used to be applied by hand:

**`device/downstream/firmware-realme-lunaa`** (19MB apk) — proprietary blobs
extracted from the stock ROM, installed to `/lib/firmware`:
IPA (`yupik_ipa_fws.*`), Venus (`vpu20_1v.*`), Adreno zap shader
(`a660_zap.*`), ADSP/CDSP, and the WCN6750 wifi set (`wpss.*`, `qca6750/`,
`bdwlan.elf`, `regdb.bin`, `qdss_trace_config.cfg`, `wlan/qca_cld/`).
Also ships `usr/share/mkinitfs/files/00-lunaa-firmware.files` so the IPA
firmware is **inside the initramfs** — it is requested at ~0.77s, before the
rootfs mounts, so without this it can never load with a short firmware timeout.

Rebuild the blobs from the ROM with:

```sh
payload-dumper-go -p vendor,modem,odm lineage-20.0-*-lunaa.zip
# vendor.img: /firmware
# modem.img:  image/{wpss.*,adsp.*,cdsp.*,qca6750/}
# odm.img:    vendor/etc/wifi/WCNSS_qcom_cfg.ini
```

**Never upstream this package** — the blobs are not redistributable. A pmaports
submission documents extracting them instead.

**`device-realme-lunaa`** also ships:
- `/usr/share/tinydm/env-wayland.d/10-pixman.sh` (`WLR_RENDERER=pixman`)
- `/usr/bin/lunaa-wifi-up.sh` + `/etc/init.d/lunaa-wifi` — the WCN6750 bring-up
  sequence (§4), added to the default runlevel by a `.post-install`
- and depends on `firmware-realme-lunaa`

**`device-realme-lunaa-sxmo`** (subpackage) ships only
`/usr/bin/sxmo_deviceprofile_qcom,yupik-idp.sh` (scale 3, `SXMO_MONITOR`,
`SXMO_NO_MODEM=1`). Kept separate because it is UI config, not a hardware fact —
dead weight under phosh/plasma, and the kind of thing pmaports reviewers flag.
It is pulled in via `extra_packages` in `~/.config/pmbootstrap_v3.cfg`:

```
extra_packages = openssh,device-realme-lunaa-sxmo
```

`10-pixman.sh` deliberately stays in the **main** package: "no working GL on
this device" is a property of the hardware and applies to any wlroots
compositor, not just sxmo.

Note **abuild forbids `,` in source filenames**, so the deviceprofile ships as
`sxmo_deviceprofile.sh` and is installed to the comma'd path in the subpackage
function.

**`tools/`** in the repo holds `build-images.sh`, the init shim
(`finalinit.c` — forces dwc3 peripheral mode and caps the firmware timeout),
the touchscreen kernel patch, and `rescue-shell.c`.

### Wifi WORKS — three manual steps Android does and pmOS does not

`wlan0` comes up, NetworkManager/nmtui sees networks, apk and curl work over it.
Automated by `lunaa-wifi-up.sh` + the `lunaa-wifi` OpenRC service in
`device-realme-lunaa`. The sequence, and why each step is needed:

1. **Boot the WPSS subsystem.** Nothing does this automatically — icnss2
   registers it and waits forever.
   ```sh
   echo 1 > /sys/devices/platform/soc/17a10040.qcom,wcn6750/wpss_boot
   ```
2. **Create `/dev/wlan`.** qcacld registers a chrdev named **`qcwlanstate`**
   (see `/proc/devices`) but no node is created. Class is `/sys/class/wlan/wlan`,
   and `/sys/class/wlan/wlan/dev` gives the major:minor.
   ```sh
   mknod /dev/wlan c $(awk '/qcwlanstate/{print $1}' /proc/devices) 0
   ```
3. **Write `ON` to it.** *qcacld does not initialise on modprobe.* It registers
   the chrdev and blocks until userspace writes `"ON"` — normally the Android
   wifi HAL. This is the step that stalled the port for hours.
   ```sh
   printf ON > /dev/wlan
   ```

**The write takes several seconds and returns `EINVAL`. Ignore it — `wlan0`
comes up anyway.** The exact source of that errno is *not* pinned down. icnss2
is spinning in its DMS retry loop at the time (`DMS QMI connection not
established`, looking for a MAC from a modem subsystem this port never boots)
and dmesg shows qcacld's `wait_for_completion_timeout` printing `Timed-out!!` —
but the observed 5-10s does not cleanly match that 30s timeout, so the two may
not be the same event. Empirically harmless; do not spend time on it unless
something else breaks. `enable_mac_provision` defaults to 0, so the DMS failure
is not fatal to the load.

Firmware prerequisites (all in `firmware-realme-lunaa`): `wpss.*`, and
`bdwlan.elf` / `regdb.bin` / `qdss_trace_config.cfg` at the **top level** of
`/lib/firmware`, not only under `qca6750/`.

Debugging tip: qcacld's own logs are suppressed by default. Load with
`modprobe wlan qdf_log_dump_at_kernel_enable=1` to see them.

### Still outstanding

- The `peripheral`-mode write lives in a C shim that wraps pmOS's init. It
  should become a proper initramfs hook in `device-realme-lunaa` so the shim can
  be deleted. Until then `boot-deploy`'s own images do **not** boot.
- `CONFIG_SPECTRA_CAMERA=n` — the camera subsystem is disabled to get the kernel
  to build (§5 Phase E). No camera.
- **No battery/charging.** `/sys/class/power_supply/` is empty. No charger
  module was produced, so `CONFIG_OPLUS_SM8350_CHARGER` is presumably built-in
  and failing to probe — note the `i2c_geni 984000.i2c: i2c error :-107` spam in
  dmesg, which may be the same bus.
- **Vendor firmware missing.** `yupik_ipa_fws.mdt` (IPA) and `vpu20_1v.mdt`
  (Venus) fail with `-2` (ENOENT); the blobs live on the `vendor` partition
  inside `super`, which is not mounted. Affects network offload and hardware
  video decode, not display. Extract to `/lib/firmware` to fix.
- **GPU acceleration untested.** Display is KMS-only so far. The Adreno lives
  behind KGSL (`/sys/class/kgsl`), not the mainline `msm` DRM GPU node, so mesa
  needs its **kgsl** freedreno backend. Software rendering is the fallback.

### Do not put `pmos.debug-shell` in `deviceinfo_kernel_cmdline`

It was briefly added and then removed: it makes `debug_shell()` reconfigure the
gadget for ACM, which drops `dwc3` out of peripheral mode and kills USB
networking entirely (see §3). The rescue-shell path in `tools/` is the
supported way to get a shell instead.

## 5. Build reference

**This section is reference, not a to-do list.** Phases A–E document how each
piece was arrived at and remain accurate for the kernel config, the deviceinfo
keys and the kernel APKBUILD. **Phase F is the actual day-to-day procedure** —
start there.

pmbootstrap **3.11.1** from the Arch package. (PyPI versions are yanked;
pmbootstrap is no longer distributed via pip.)

### Phase A — Backups (do this first)

fastbootd can read partitions, so this is straightforward:

```bash
mkdir -p ~/pmos-backup && cd ~/pmos-backup
adb reboot fastboot
fastboot fetch boot boot.img
fastboot fetch vendor_boot vendor_boot.img
fastboot fetch dtbo dtbo.img
fastboot fetch vbmeta vbmeta.img
ls -la
```

If `fetch` isn't supported, fall back to `payload-dumper-go -p boot,vendor_boot,dtbo,vbmeta lineage-20.0-*-lunaa.zip`, or dd from recovery using the block devices in section 1.

**Verify these exist before any flash.** Recovery lives *inside* boot.img on this A/B device — flashing boot destroys recovery.

### Phase B — Init

```bash
pmbootstrap init
```

Vendor `realme`, codename `lunaa`, aarch64, handset, **downstream** kernel, UI **`none`**, extra package `openssh`.

A console + SSH is the target for boot #1. Adding a compositor before the kernel is known to boot only adds failure modes.

Since a downstream kernel is selected, packages belong in `device/downstream/`. A previous attempt hit `Package linux-realme-lunaa found in multiple aports subfolders` from duplicate copies in `testing/` and `downstream/`. After init, verify:

```bash
cd ~/.local/var/pmbootstrap/cache_git/pmaports
find . -type d -name "*realme-lunaa*"
```

Exactly two directories, both under `device/downstream/`
(`device-realme-lunaa` and `linux-realme-lunaa` — picking the `downstream` port
type generates both).

**pmbootstrap papercut:** if `~/.config/pmbootstrap_v3.cfg` still has
`device = realme-lunaa` from a previous run but the port does not exist in
pmaports, `init` aborts with

```
ERROR: This device does not exist anymore, check <https://postmarketos.org/renamed>
```

instead of offering to create it (`config/init.py`: `if device == context.config.device: raise`).
Deleting the work dir does **not** help — the config lives in `~/.config`.
Remove the `device = ` line from `~/.config/pmbootstrap_v3.cfg` and re-run.

### Phase C — Kernel config

```bash
pmbootstrap kconfig check realme-lunaa
pmbootstrap kconfig edit realme-lunaa
```

**Must be `=y`:** `DEVTMPFS`, `DEVTMPFS_MOUNT`, `SYSVIPC`, `VT`, `FHANDLE`, `CGROUPS` (full family), all namespaces (`PID_NS`, `IPC_NS`, `UTS_NS`, `NET_NS`, `USER_NS`), `TMPFS_POSIX_ACL`, `TMPFS_XATTR`, `USB_CONFIGFS`, `USB_CONFIGFS_RNDIS`, `USB_CONFIGFS_ECM`, `USB_LIBCOMPOSITE`, `PSTORE`, `PSTORE_RAM`, `PSTORE_CONSOLE`

**Must be OFF:** `AUDIT`, `STATIC_USERMODEHELPER`. Both confirmed `=y` in the
real ROM defconfig — `STATIC_USERMODEHELPER=y` really is a genuine boot-breaker
here. `ANDROID_PARANOID_NETWORK` is a **non-issue**: the symbol does not exist
in 5.4 QGKI at all, so there is nothing to disable.

Merging the pristine ROM chain and applying the pmOS delta changes exactly six
symbols:

| Symbol | ROM | pmOS |
|---|---|---|
| `STATIC_USERMODEHELPER` | `=y` | off |
| `AUDIT` | `=y` | off |
| `PID_NS` | off | `=y` |
| `FHANDLE` | off | `=y` |
| `VT` | off | `=y` |
| `DRM_FBDEV_EMULATION` | off | `=y` |

**`RD_ZSTD` / `DECOMPRESS_ZSTD` cannot be satisfied** — they do not exist in 5.4
(added in 5.9). `pmbootstrap kconfig check` will always flag them. This is safe
*only* because `deviceinfo_initfs_compression` is pinned to `gzip`; a zstd
initramfs would be undecompressable and hang silently with no way to observe it
on this device. Do not remove that pin.

**The USB gadget path is entirely built-in** — verified in the ROM defconfig:
`USB_DWC3=y`, `USB_DWC3_MSM=y`, `USB_GADGET=y`, `USB_CONFIGFS=y`,
`USB_LIBCOMPOSITE=y`, and crucially `MSM_HSUSB_PHY=y` (the USB2 PHY).
`USB_CONFIGFS_NCM=y` and `USB_CONFIGFS_ECM=y` are already set. Nothing has to
load from `modules-initfs` for USB networking to come up. Note pmOS tries
`ncm.usb0` **first** and only falls back to `rndis.usb0`.

**Leave alone:** every `=m` in the QGKI fragment. Converting modules to built-ins breaks loading against existing vendor blobs.

### Phase D — deviceinfo

pmOS uses different key names than the UBports tooling. Verify every key against
`https://docs.postmarketos.org/pmaports/main/deviceinfo-reference.html` for pmbootstrap 3.11 — do not copy blindly from older examples.

**RESOLVED — how pmbootstrap 3.11 handles header v3.** `boot-deploy` 0.24.0
(`create_bootimg()`) takes a completely separate branch for header 3/4:

```sh
mkbootimg --header_version 3 \
    --dtb "${_dtb}" \
    --kernel "${_kernelfile}" \
    --vendor_ramdisk "$_ramdisk" \
    --vendor_cmdline "$(get_cmdline)" \
    --pagesize "${deviceinfo_flash_pagesize}" \
    --vendor_boot "$_vendor_bootimg" \
    -o "$_bootimg"
```

So on this device:

- **`boot.img` gets the kernel and nothing else** — no ramdisk, no cmdline.
- **`vendor_boot.img` gets the initramfs, the cmdline and the dtb.**

Consequences:

1. **Both images must be flashed.** Flashing only `boot` and keeping the stock
   `vendor_boot` boots our kernel with *Android's* ramdisk and cmdline — a
   guaranteed silent hang that looks exactly like the Halium symptom.
2. **`deviceinfo_dtb` is mandatory.** `--dtb "$_dtb"` is unconditional on this
   path and `find_dtb()` hard-exits if the file is not in the rootfs. (The
   deviceinfo schema's claim that `dtb` is "only used on mainline kernels or
   devices with a header version of 2" is **stale** — the code contradicts it.)
3. There is no vendor/generic ramdisk concatenation to rely on. Our vendor
   ramdisk is the only ramdisk, so no Android files leak in.

**The DTB.** The stock `vendor_boot` dtb section is a Qualcomm multi-DTB blob:
**17 concatenated FDTs** that ABL selects from by `qcom,msm-id`/`board-id`
(entries 4, 12 and 14 are the yupik ones, msm-id `0x1db`/`0x203`; there is no
lunaa-specific DTB, board config comes from the dtbo overlays). `mkbootimg --dtb`
copies the file verbatim without parsing it, so ship **that entire blob**. Our
`vendor_boot` then differs from the known-booting one in only the ramdisk and
the cmdline, and the stock dtbo overlays stay valid. Extract it with the
unpacker in section 6.

**The cmdline.** Because we replace `vendor_boot`, LineageOS's vendor cmdline is
no longer supplied by the stock image and must be carried over verbatim —
`swiotlb=0`, `lpm_levels.sleep_disabled=1` etc. are real kernel parameters.

Working `deviceinfo` (`device/downstream/device-realme-lunaa/deviceinfo`):

```sh
deviceinfo_format_version="0"
deviceinfo_name="Realme GT Master Edition"
deviceinfo_manufacturer="Realme"
deviceinfo_codename="realme-lunaa"
deviceinfo_year="2021"
deviceinfo_arch="aarch64"
deviceinfo_chassis="handset"
deviceinfo_screen_width="1080"
deviceinfo_screen_height="2400"
deviceinfo_flash_method="fastboot"
deviceinfo_generate_bootimg="true"
deviceinfo_header_version="3"
deviceinfo_bootimg_qcdt="false"
deviceinfo_flash_pagesize="4096"
deviceinfo_rootfs_image_sector_size="4096"
deviceinfo_initfs_compression="gzip"
deviceinfo_dtb="lunaa-yupik"
deviceinfo_append_dtb="false"
deviceinfo_kernel_cmdline="<LineageOS vendor cmdline verbatim>"
deviceinfo_flash_fastboot_partition_kernel="boot"
deviceinfo_flash_fastboot_partition_vendor_boot="vendor_boot"
deviceinfo_flash_fastboot_partition_rootfs="userdata"
```

Notes:

- **`deviceinfo_gpu_accelerated` is not a valid key** any more — it was renamed
  to `deviceinfo_drm`. Verified against `deviceinfo_schema.toml`; only
  `format_version`, `name`, `manufacturer`, `codename`, `year` and `arch` are
  mandatory.
- `deviceinfo_flash_offset_*` are **ignored** for header v3 (that header has no
  load-address fields). Harmless to include, pointless.
- `rootfs_image_sector_size="4096"` matters — this device is UFS.
- The device package must depend on **`android-tools-mkbootimg`** (Alpine
  community, AOSP 37.0.0; provides `/usr/bin/mkbootimg` and supports
  `--vendor_boot`). Not `mkbootimg`, which does not exist in the pmOS repo, and
  not `mkbootimg-osm0sis`, which the v3 path never calls.
- Ship the DTB blob from the device package to `/boot/dtbs/lunaa-yupik.dtb`;
  `find_dtb()` searches `/boot/dtbs*/` then `/usr/share/dtb/`.

**Debug-shell cmdline keys — all three are traps, in different ways.**

- `pmos_debug` and `PMOS_NO_OUTPUT_REDIRECT` are **not parsed at all** by
  initramfs 3.12.3. `parse_cmdline` in `init_functions.sh` ignores them
  silently. They were in earlier drafts of this document and cost a flash cycle.
- `pmos.debug-shell` (dot, hyphen) **is** the real key — but **do not use it on
  this device.** It makes `debug_shell()` reconfigure the gadget for ACM, which
  drops dwc3 out of peripheral mode and kills USB networking entirely (§3).

Consequence: without a debug shell the initramfs comes up and answers ping, but
**nothing listens on port 23** — `telnetd` only runs inside `debug_shell()`. Use
the standalone rescue image (`tools/rescue-shell.c`, §3) instead.

### Phase E — Kernel package

`device/downstream/linux-realme-lunaa/APKBUILD`, using `downstreamkernel_prepare`
and `downstreamkernel_package`. Commit is pinned in section 2. Key points, all
learned the hard way:

- **`BRAND_SHOW_FLAG=realme`** on the make line — see section 2. Without it the
  build fails in `drivers/power/`.
- **Use gcc, not clang.** The modern downstream kernels in pmaports export
  `CC=clang`, but gcc is what demonstrably produced a booting kernel from this
  tree. `CC="${CC:-gcc}"` (the template default) is correct here.
- **Delete the four gcc patches** the `pmbootstrap init` wizard adds
  (`gcc7-give-up-on-ilog2`, `gcc8-fix-put-user`, `gcc10-extern_YYLOC`,
  `kernel-use-the-gnu89-standard`). They target 3.x/4.x kernels and do not apply
  to 5.4. Set `REPLACE_GCCH=0 . downstreamkernel_prepare`.
- **Pin `KERNEL_IMAGE_NAME="Image"`.** `downstreamkernel_package` globs
  `zImage-dtb, Image.gz-dtb, *zImage, Image` in that order and will otherwise
  pick a gzipped image; stock is raw uncompressed `Image`.
- `downstreamkernel_prepare` copies the config to `.config` and runs
  `make oldconfig`, so a merged **defconfig** is fine — it does not need to be a
  fully expanded `.config`. Note this means `pmbootstrap kconfig check` run
  against the sparse defconfig reports many false positives for symbols that
  `oldconfig` will enable by default.

Config alongside as `config-realme-lunaa.aarch64`, built by merging the pristine
ROM chain (`vendor/lahaina-qgki_defconfig` + `vendor/oplus_yupik_QGKI.config`,
in that order — the fragment overrides exactly one symbol, `CNSS_QCA6490` y→n,
and adds 7) and then applying the pmOS delta from Phase C.

### Phase F — Build, flash, verify (THE ACTUAL PROCEDURE)

```sh
pmbootstrap install --password <pw>
pmbootstrap export
~/pmos-lunaa/tools/build-images.sh          # writes ~/pmos-lunaa/out/{boot,vendor_boot}.img

adb reboot fastboot
fastboot flash userdata    /tmp/postmarketOS-export/realme-lunaa.img
fastboot flash boot        ~/pmos-lunaa/out/boot.img
fastboot flash vendor_boot ~/pmos-lunaa/out/vendor_boot.img
fastboot reboot
```

**Flash all three, every time.** `pmbootstrap install` regenerates the
filesystem UUIDs, and the boot images carry `pmos_boot_uuid`/`pmos_root_uuid` in
the vendor_boot cmdline. Boot images built against a different rootfs will hang
in `wait_root_partition`. `build-images.sh` always reads the cmdline out of
boot-deploy's freshly generated `vendor_boot.img`, so it picks up the current
UUIDs automatically — but only if you run it *after* `pmbootstrap export`.

**Boot-images-only flash is safe** (skip `userdata`) when the rootfs has not
been reinstalled — e.g. after changing only the init shim. Verify first:

```sh
python3 -c "
d=open('$HOME/pmos-lunaa/out/vendor_boot.img','rb').read(4096)
c=d[28:28+2048].split(b'\0')[0].decode()
print(' '.join(w for w in c.split() if 'pmos_' in w))"
sudo losetup -fP --sector-size 4096 /tmp/postmarketOS-export/realme-lunaa.img && sudo blkid /dev/loop*p*
```

The UUIDs must match. Note the **4096 sector size** — the image will not
loop-mount without it.

**Why `boot-deploy`'s own images are not used:** they do not boot. Two fixups
are mandatory and `build-images.sh` applies both — the ramdisk must be a single
lz4-legacy stream (§3), and an init shim must force dwc3 into peripheral mode
and cap the firmware timeout (§3, §4). See `tools/finalinit.c`.

**Do not flash `dtbo`.** Our `vendor_boot` reuses LineageOS's exact DTB blob, so
the stock dtbo overlays still apply.

### Phase G — Connect

After flashing, pmOS's `unudhcpd` hands the host an address automatically:

```sh
watch -n1 'ip -br addr show <iface>'        # wait for 172.16.42.2/24
ssh <user>@172.16.42.1
```

**The device is 172.16.42.1, the host is 172.16.42.2.** If the host interface
shows no address (which happens with rescue images that skip pmOS's init, and
after any reflash recreates the netdev), assign it by hand:

```sh
sudo ip addr add 172.16.42.2/24 dev <iface>
```

Failing to do this looks exactly like a dead device: ping fails before a packet
leaves the PC. Check `ip -br addr` before concluding anything (§11).

**First boot takes longer** — filesystem resize and SSH host-key generation.
`ssh` refusing 60s in is not failure; wait and retry before reflashing.

---

## 6. Recovery procedures

```bash
# Standard restore
adb reboot fastboot
fastboot flash boot ~/pmos-backup/boot.img
fastboot flash vendor_boot ~/pmos-backup/vendor_boot.img
fastboot reboot

# If fastbootd is unreachable (recovery lives inside boot.img — flashing boot destroys it)
# → EDL: hold Vol Up + Vol Down while plugging in, flash via Qualcomm Toolbox

# Force power off
# Hold Vol Up + Vol Down + Power ~20 seconds
```

### Extracting the stock DTB blob from the vendor_boot backup

Needed for `deviceinfo_dtb` (Phase D). Pure offline work on the backup:

```python
import struct
d = open("vendor_boot.img", "rb").read()
assert d[:8] == b"VNDRBOOT"
pgsz, = struct.unpack("<I", d[12:16])
vrs,  = struct.unpack("<I", d[24:28])
hdr_size, dtb_size = struct.unpack("<2I", d[2096:2104])
pg = lambda n: (n + pgsz - 1) // pgsz * pgsz
o = pg(hdr_size) ; o = pg(o + vrs)          # skip header, then vendor_ramdisk
open("lunaa-yupik.dtb", "wb").write(d[o:o + dtb_size])
```

Expected: page size 4096, `vendor_ramdisk_size` 20378682, `dtb_size` 8059318,
sha512 starting `3e29010f3d709d67`. Note `fastboot fetch` returns the whole
192 MB partition zero-padded, not a trimmed image — that is normal.

**Do NOT downgrade firmware.** Anti-rollback is active on this platform and there is a documented RMX3363 ARB-bricked to EDL-only. The existing unlock works; do not re-do it.

---

## 7. What is left, and what to do next

The port is done and reproducible. Remaining work, in the order I would do it:

1. **Retire the C init shim.** `tools/finalinit.c` forces dwc3 into peripheral
   mode and caps `/sys/class/firmware/timeout`. Both belong in a proper
   initramfs hook in `device-realme-lunaa`, after which plain `boot-deploy`
   output would boot and `build-images.sh` could be dropped. This is the single
   change that would make the port upstreamable.
2. **Battery / charging.** `/sys/class/power_supply/` is empty. No charger
   module was produced, so `CONFIG_OPLUS_SM8350_CHARGER` is presumably built-in
   and failing to probe. Look at the `i2c_geni 984000.i2c: i2c error :-107`
   spam in dmesg — the charger may sit on that bus.
3. **GPU acceleration.** Needs a local mesa aport built with
   `-Dfreedreno-kmds=msm,kgsl`. `/dev/ion` being `root:root 0600` with no
   `/dev/dma_heap` is a second obstacle behind it. See §4 — this only buys
   smoother animation; it does not fix boot time or the UI.
4. **Camera.** `CONFIG_SPECTRA_CAMERA=n` was needed to compile (§5 Phase E).
   Re-enabling means fixing the `-mgeneral-regs-only` float errors in the ON
   Semi OIS firmware and ~50 undefined symbols from `CONFIG_SPECTRA_OPLUS`.

### Worth upstreaming (independent of the port itself)

Two are real bugs affecting other hardware, and neither needs the proprietary
blobs:

- **The touchscreen patch** (`tools/0001-touchpanel-*.patch`). Breaks touch
  under Wayland on *every* oplus/OnePlus/Realme device using
  `oplus_touchscreen_v2`.
- **The dwc3 peripheral-mode requirement** (§3). Any SM7325 downstream port
  looks completely dead without it — no USB, no way to see why.

The qcacld `ON`-trigger sequence (§4) is also worth writing up; it is known in
Halium/Droidian circles but not documented for pmOS.

### Do not recommend

Halium 11 (same architecture, same failure mode), edk2/UEFI (no working SM7325
port exists), or kexec (`CONFIG_KEXEC` disabled on QGKI 5.4, and it would not
remove the need for a bootable kernel anyway).

---

## 8. Reference

- pmbootstrap: `https://gitlab.postmarketos.org/postmarketOS/pmbootstrap`
- Porting guide: `https://wiki.postmarketos.org/wiki/Porting_to_a_new_device`
- deviceinfo reference: `https://docs.postmarketos.org/pmaports/main/deviceinfo-reference.html`
- Downstream kernel packaging: `https://wiki.postmarketos.org/wiki/Downstream_kernel_specific_package_interface`
- Troubleshooting boot: `https://wiki.postmarketos.org/wiki/Troubleshooting:boot`
- Community: Matrix/IRC `#postmarketos-porting`

**Same-SoC reference device:** Nothing Phone (1), codename `nothing-spacewar`, SM7325, an official pmOS device on the mainline `linux-postmarketos-qcom-sc7280` kernel. Its DTS (`arch/arm64/boot/dts/qcom/sm7325-nothing-spacewar.dts` in torvalds/linux) is the template if a mainline port is ever attempted.

**No Linux port existed for lunaa before this one.** The pmOS wiki still lists
the device as non-booting. There was no prior art to diff against, which is why
so much of this document is about *how* things were found rather than just what
the answers are — a second device in this family would be far quicker, but only
if the reasoning is preserved along with the results.

---

## 9. How to work with the human

- They are on **Arch Linux**, using **fish** shell. Adjust command syntax accordingly (fish doesn't handle `VAR=x cmd` or some redirections the same way as bash).
- They are technically capable and willing to modify source, but this is their first device port.
- Explain what each attempt would prove before they spend a flash cycle on it.
- When something fails, ask for specific command output rather than guessing. Blind iteration has already cost this project a great deal of time.
- Be honest about probability, and about what is actually known versus assumed.
  The port succeeded, but several wrong turns came from stating a guess with
  more confidence than the evidence supported — see §11.

---

## 10. Observability on a device with no console

This is the toolkit that finally cracked the port. Use it before theorising.

### Rule 0 — do the offline check first

Before spending a flash cycle on "is the image valid?", rebuild the *stock*
image from its own extracted parts and `cmp` it against the backup. It is free
and it exonerates (or convicts) the entire packing pipeline in seconds. Doing
this on day one would have saved most of this project's flash cycles.

### Rule 1 — a hang tells you nothing about which layer failed

Bootloader rejection, early kernel panic, and a userspace `/init` that dies all
present identically: stuck at the Realme logo, no USB, empty pstore. Design
tests that *distinguish* layers rather than tests that assume one.

### Rule 2 — the reboot syscall is a reliable 1-bit channel

A `/init` that sleeps then calls `reboot()` proves userspace was reached, and
needs only: ABL loads, kernel starts, initramfs unpacks, `execve` succeeds.
A visible reboot cycle is unmistakable. Build it **statically linked and
non-PIE** (`gcc -static -no-pie`) so no loader, no `/lib`, no shebang and no
interpreter can be blamed.

Compile one inside the aarch64 rootfs chroot (runs under qemu binfmt):

```sh
pmbootstrap chroot --rootfs -- apk add gcc musl-dev
pmbootstrap chroot --rootfs -- gcc -static -no-pie -Os -o /tmp/init /tmp/init.c
```

### Rule 3 — exfiltrate dmesg through an inactive-slot partition

`klogctl(SYSLOG_ACTION_READ_ALL)` returns the whole ring buffer with no console
involved. Write it to a partition on the **inactive** slot (the device boots
`_b`, so `dtbo_a` is free and harmless), then read it back **via EDL** — note
`fastboot fetch` will NOT read `dtbo` (§0).

Resolve the block device by scanning `/sys/class/block/*/uevent` for
`PARTNAME=`; devtmpfs gives `/dev/sdeN`, it does **not** create
`/dev/block/by-name/` (that is ueventd's job). Sleep ~8 s first so UFS and dwc3
finish probing.

### Signals that do NOT work here

- **The vibrator.** `CONFIG_INPUT_QCOM_HV_HAPTICS=y` registers an *input/FF*
  device, not `/sys/class/leds/vibrator`; `CONFIG_AW8697_HAPTIC` (likely the
  real one) is `=m` and not in the initramfs. Writing to `/sys/class/leds/...`
  silently does nothing. A cycle was wasted on this.
- **`PMOS_NO_OUTPUT_REDIRECT`.** There is no framebuffer console: it needs
  `msm_drm.ko` (a module we never load) plus `DRM_FBDEV_EMULATION` (which
  breaks the vmlinux link, see §5 Phase C). The flag is harmless but useless.
- **pstore.** Always empty.

### Full ABL-appended cmdline (from a real boot, for reference)

ABL prepends the DTB `bootargs` and appends a large block of `androidboot.*`.
Useful values seen: `androidboot.slot_suffix=_b`, `androidboot.dtbo_idx=2`,
`androidboot.dtb_idx=3`, `androidboot.force_normal_boot=1`, `init=/init`,
`rootwait ro`, `androidboot.verifiedbootstate=orange`,
`androidboot.bootdevice=1d84000.ufshc`, `printk.disable_uart=1`,
`earlycon=msm_geni_serial,0x994000`, `log_buf_len=256K`.

---

## 11. Gotchas that cost real time (read before iterating)

**Every `pmbootstrap install` regenerates the filesystem UUIDs.** The
`pmos_boot_uuid` / `pmos_root_uuid` values are baked into the vendor_boot
cmdline, so a fresh rootfs makes the *previously flashed* boot images unable to
find it. **Always reflash `boot` + `vendor_boot` together with `userdata`.**
`tools/build-images.sh` reads the cmdline out of boot-deploy's regenerated
`vendor_boot.img`, so it always picks up the current UUIDs.

**apk runs `.post-install` only on a fresh install, never on an upgrade.** On
an upgrade it looks for `.post-upgrade` instead. `device-realme-lunaa` shipped
only `.post-install`, so once an earlier revision was installed every later
revision was an *upgrade* and `rc-update add lunaa-wifi default` never ran —
the `lunaa-wifi` service was in the image but not in any runlevel, and wifi was
dead at boot while `rc-service lunaa-wifi start` worked by hand. Both scripts
are now shipped and identical (`install="$pkgname.post-install
$pkgname.post-upgrade"`). Any future device-package service must do the same.

Diagnose this entirely offline — no flash cycle:

```sh
ls ~/.local/var/pmbootstrap/chroot_rootfs_realme-lunaa/etc/runlevels/default/
grep "Executing" ~/.local/var/pmbootstrap/log.txt | grep realme
```

An empty second command while other packages' scripts are listed is the tell.

**The host's IP is not automatic without pmOS's `unudhcpd`.** With a rescue
image that skips pmOS's init, the PC gets no DHCP lease; reflashing also
recreates the netdev and drops any manual address. `ping` then fails before a
packet leaves the PC and it looks like the phone is dead. Check
`ip -br addr show <iface>` shows `172.16.42.2/24` before concluding anything.

**Wait for the first boot.** Filesystem resize and SSH host-key generation take
a while. `ssh` refusing 60 s in does not mean failure — it did come up shortly
after, twice.

**The rescue shell dies at `switch_root`.** It deletes the initramfs, so a
surviving listener accepts and then resets the connection (`Connection reset by
peer`) because `/bin/busybox` no longer exists for that process. To debug a
booted system you need sshd, not the rescue shell.

**The user password is not recoverable.** pmbootstrap stores only a `$6$` hash
and `root` is locked. If it is lost, re-run
`pmbootstrap install --password <pw>` and reflash all three images (see the
UUID note above).

### Measure before theorising

Two cheap measurements settled questions that three rounds of guessing did not:

- `cat /proc/uptime` — the second field is idle core-seconds. 96% idle meant the
  slow boot was *waiting*, not computing, which immediately ruled out the GPU.
- the dmesg gap scan below — prints every stall longer than a second, with the
  line that follows it:

```sh
dmesg | awk -F'[][]' '{t=$2+0; if (p && t-p > 1) printf "+%6.1fs at %8.2f  %s\n", t-p, t, substr($0,1,110); p=t}'
```

Guesses that cost flash cycles and were wrong: OpenRC service timeouts (OpenRC
takes 7s total — measured with `rc_logger="YES"` in `/etc/rc.conf`), AVB/
rollback rejection, and "wifi needs the modem subsystem".

### Read the driver, not the symptom

Wifi looked like it needed a modem QMI service. It actually needed three
userspace actions Android performs (§4), all findable by reading qcacld's init
path. The decisive clue — `wlan_hdd_state ... initialized` and then silence —
was in the log for several rounds before anyone looked up what that function
does. `strings` on a built `.so` also settles "is this backend compiled in?" in
seconds; assuming it was, from two incidental symbol names, sent the GPU
investigation down a dead end.

### Change one thing per flash cycle

Several cycles were spent on images that changed two variables at once, which
made the result uninterpretable. On a device where every test is a real write to
flash, this is expensive. Where a single image *can* test two hypotheses (e.g.
identical ramdisk content in both slots, so it works whichever the bootloader
honours), say so explicitly in advance.

### Audit exhaustively, not incrementally

The touchscreen needed ranges on **five** `ABS_MT` axes. Fixing the one libinput
named revealed the next one on the following boot, costing an extra cycle. The
information to fix both was in the first `grep`.
