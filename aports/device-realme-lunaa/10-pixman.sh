# Mesa has no usable driver for this Adreno: it is behind KGSL (/dev/kgsl-3d0),
# not a DRM GPU node, and Alpine's Turnip is built without the KGSL backend.
# EGL therefore fails ("DRI2: failed to load driver"), zink finds no Vulkan ICD,
# and wlroots' GLES2 and Vulkan renderers both fail. Xorg segfaults outright in
# glamor init. wlroots' pixman software renderer is the supported fallback.
export WLR_RENDERER=pixman
