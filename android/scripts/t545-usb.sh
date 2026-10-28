#!/system/bin/sh
# Own USB gadget handling for adbd (debugging). The stock rc flow loops on this kernel: its "sys.usb.ffs.ready=1" action re-runs on every
# FunctionFS re-init of adbd, rebinds the UDC, the host sees a disconnect, adbd re-inits FunctionFS, ... so sys.usb.config is kept at
# "t545" (no stock action matches) and the gadget is bound here, once adbd has written its descriptors (ep1 exists).
# Started by zz_t545_km.rc "on property:init.svc.adbd=running" (so also after "adb root" restarted adbd).
G=/config/usb_gadget/g1
log() { echo "t545-usb: $*" > /dev/kmsg; echo "$(date +%T) $*" >> /cache/t545/usb.log; }
mkdir /dev/t545usb.lock 2>/dev/null || exit 0
log "usb.sh started, ep0: $(ls /dev/usb-ffs/adb 2>&1 | tr '
' ' ')"
for i in $(seq 1 60); do
  [ -d $G/functions/ffs.adb ] && [ -e /dev/usb-ffs/adb/ep0 ] && break
  sleep 1
done
echo "" > $G/UDC 2>/dev/null
echo 0x18d1 > $G/idVendor
echo 0x4ee7 > $G/idProduct
rm -f $G/configs/b.1/f1 $G/configs/b.1/f2 $G/configs/b.1/f3 $G/configs/b.1/f4 $G/configs/b.1/f5 $G/configs/b.1/f6 $G/configs/b.1/f7 $G/configs/b.1/f8 $G/configs/b.1/f9
echo adb > $G/configs/b.1/strings/0x409/configuration
ln -s $G/functions/ffs.adb $G/configs/b.1/f1
log "waiting for ep1, ep: $(ls /dev/usb-ffs/adb 2>&1 | tr '
' ' ')"
for i in $(seq 1 40); do
  [ -e /dev/usb-ffs/adb/ep1 ] && break
  sleep 1
done
sleep 1
for i in 1 2 3 4 5 6 7 8 9 10; do
  echo a600000.dwc3 > $G/UDC 2>/dev/null
  [ -n "$(cat $G/UDC 2>/dev/null)" ] && break
  echo "" > $G/UDC 2>/dev/null
  sleep 2
done
log "gadget bound: UDC=$(cat $G/UDC 2>&1) ep1=$(ls /dev/usb-ffs/adb 2>&1 | tr '\n' ' ')"
rmdir /dev/t545usb.lock
