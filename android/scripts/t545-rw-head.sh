# SPDX-License-Identifier: Apache-2.0
# t545 (initramfs init): this copy of phh's rw-system.sh runs from vndk.rc instead of /system/bin/rw-system.sh, without the
# /sys-wide find for a Focaltech node. Boot-time measurements: an execution trace with timestamps when /cache/t545/rwtrace exists.
[ -f /cache/t545/rwtrace ] && { PS4='+$EPOCHREALTIME '; exec 2>/cache/t545/rwtrace.log; set -x; }
# boot time: the ~130 "getprop | grep" checks below cost ~25 ms each (two dynamic executables per pipeline, ~2 s in all). Read-only
# vendor props that this script never sets are read once, and grep is the initramfs' static busybox: ~10 ms per check.
t545_fp="$(getprop ro.vendor.build.fingerprint)"
t545_hw="$(getprop ro.hardware)"
t545_bp="$(getprop ro.board.platform)"
t545_vpd="$(getprop ro.vendor.product.device)"
t545_pvm="$(getprop ro.product.vendor.manufacturer)"
getprop() {
	if [ $# = 1 ]; then
		case "$1" in
		ro.vendor.build.fingerprint) print -r -- "$t545_fp"; return 0;;
		ro.hardware) print -r -- "$t545_hw"; return 0;;
		ro.board.platform) print -r -- "$t545_bp"; return 0;;
		ro.vendor.product.device) print -r -- "$t545_vpd"; return 0;;
		ro.product.vendor.manufacturer) print -r -- "$t545_pvm"; return 0;;
		esac
	fi
	command getprop "$@"
}
if [ -x /system/etc/init/t545/busybox ]; then
	grep() { /system/etc/init/t545/busybox grep "$@"; }
fi
