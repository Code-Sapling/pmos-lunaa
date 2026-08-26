#!/bin/sh
# sxmo device profile for Realme GT Master Edition (RMX3363 / lunaa), SM7325.
# sxmo matches this filename against the device-tree compatible; without it you
# get "No deviceprofile found qcom,yupik-idp" and an unusably small UI.
export SXMO_MONITOR="DSI-1"

# 1080x2400 -> 360x800 logical px, same as the Fairphone 4 (SM7225).
export SXMO_SWAY_SCALE="3"

# No modem driver on this port. Without this, sxmo_hook_start.sh sees the
# ModemManager binary, starts sxmo_modemmonitor against a dead DBus service and
# takes a 120s wakelock to "let the modem warm up".
export SXMO_NO_MODEM=1

# Skip sxmo's intermediate "lock" state: unlock -> screenoff, nothing between.
#
# sxmo's default state machine is "unlock lock screenoff", and `lock` means
# screen ON with the touchscreen deliberately switched off
# (sxmo_hook_lock.sh: `sxmo_wm.sh inputevent touchscreen off`, which is
# `swaymsg "input type:touch events disabled"`). A power press moves one state
# back (sxmo_hook_inputhandler.sh -> `sxmo_state.sh click`), so waking from
# screenoff lands in `lock`: display, buttons, wifi and ssh all alive and touch
# dead until a second press. On a device with no lockscreen installed that is
# indistinguishable from broken touch, and it cost a debugging session.
#
# sxmo_state.sh already collapses to these two states by itself when
# `peanutbutter` (or `smlock`) is present, so this line is a no-op if a
# lockscreen is ever installed -- it just makes the two-state behaviour the
# default here rather than depending on which packages happen to be in the
# image. The tradeoff is that the screen then wakes straight into a live
# session: install peanutbutter if you want a real lockscreen instead.
export SXMO_STATES="unlock screenoff"
