#!/system/bin/sh
# SPDX-License-Identifier: Apache-2.0
# Logs every sys.usb.config / sys.usb.state change with the gadget's real state (zz_t545_km.rc), so that a lost adb link can be explained later.
G=/config/usb_gadget/g1
echo "$(date +%T) $1 config=$2 state=$3 UDC=$(cat $G/UDC 2>&1) id=$(cat $G/idVendor 2>&1):$(cat $G/idProduct 2>&1) f=$(ls $G/configs/b.1 2>&1 | tr '\n' ' ')" >> /cache/t545/usb.log
