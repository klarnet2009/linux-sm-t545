#!/system/bin/sh
# Venus video codec (started as t545_venus from "on post-fs", after gfx.sh set firmware_class.path to /dev/t545fw).
# The driver is modules in /cache/t545/venus (built against the running kernel), so it can be replaced without a new boot image;
# loading venus-core boots the Samsung-signed firmware (venus.mdt + .b00..b04 from the modem partition, PAS id 9) through TZ.
# /cache/t545/venus/disable = skip it. The Codec2 HAL t545_media_c2 (IComponentStore/default1, declared in the VINTF) is started
# in every case: without Venus nodes it registers an empty store, so media clients waiting for "default1" never hang.
log() { echo "t545-venus: $*" > /dev/kmsg; }
V=/cache/t545/venus
C=/cache/t545/c2
# media_codecs_c2.xml (first of /product/etc /odm/etc /vendor/etc /system/etc wins; none ships one): lower-only overlay
if [ -f $C/etc/media_codecs_c2.xml ]; then
  chmod 644 $C/etc/*.xml
  mount -t overlay overlay -o lowerdir=$C/etc:/system/product/etc /system/product/etc && log "codec list: $(ls $C/etc | tr '\n' ' ')"
fi
if [ -f $V/disable ]; then
  log "disabled by $V/disable"
elif [ -f $V/venus-core.ko ]; then
  for f in /vendor/firmware_mnt/image/venus.*; do ln -sf $f /dev/t545fw/; done
  for m in videobuf2-dma-contig v4l2-mem2mem venus-core venus-dec venus-enc; do
    insmod $V/$m.ko || log "insmod $m failed"
  done
  for i in 1 2 3 4 5 6 7 8 9 10; do
    n=$(grep -l enus /sys/class/video4linux/video*/name 2>/dev/null | wc -l)
    [ $n -ge 2 ] && break
    sleep 0.5
  done
  for d in $(grep -l enus /sys/class/video4linux/video*/name 2>/dev/null); do
    d=${d%/name}; d=${d##*/}
    chmod 666 /dev/$d
    log "/dev/$d: $(cat /sys/class/video4linux/$d/name)"
  done
else
  log "no modules in $V"
fi
start t545_media_c2
