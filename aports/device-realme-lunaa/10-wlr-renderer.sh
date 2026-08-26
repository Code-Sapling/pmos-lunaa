# Renderer selection for wlroots compositors on realme-lunaa.
#
# This is a hardware fact, not a UI preference, which is why it lives in the
# main device package rather than the -sxmo subpackage: it applies to any
# wlroots compositor (sway, phosh/phoc). Under mutter (GNOME Mobile) it is
# simply ignored, so it is harmless there.
#
# There is no working GL on this device. The Adreno 642L sits behind KGSL
# (/dev/kgsl-3d0), not a DRM GPU node, and mesa has no KGSL backend for GL at
# all -- src/freedreno/drm/ contains only msm/ and virtio/. EGL therefore fails
# with "DRI2: failed to load driver", and Xorg segfaults outright in glamor
# init. Vulkan does work, through turnip's KGSL backend.
#
# ⚠ REQUIRES the local mesa aport (aports/mesa in the pmos-lunaa repo). Two
# things there are load-bearing:
#   1. -Dfreedreno-kmds=msm,kgsl,virtio -- Alpine builds msm,virtio, so
#      turnip's KGSL backend is not in the binary at all.
#   2. the turnip patch that reports the display DRM node's major/minor.
#      Without it turnip hides VK_EXT_physical_device_drm on KGSL and wlroots
#      refuses to select the GPU ("VK_EXT_physical_device_drm not supported").
#
# With stock Alpine mesa this setting leaves you with NO UI. The fallback is
# software rendering -- drop this file back to:
#     export WLR_RENDERER=pixman
export WLR_RENDERER=vulkan
