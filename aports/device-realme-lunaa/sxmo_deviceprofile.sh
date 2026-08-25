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
