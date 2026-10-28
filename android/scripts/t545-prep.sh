#!/system/bin/sh
# Runs from "on early-init" of zz_t545_km.rc, after first-stage init mounted /vendor and before servicemanager reads the VINTF manifest.
# boot time: the stock init.target.rc waits 5 s for the Qualcomm 4.9 path of the eMMC (mainline: soc@0), and qseecomd.rc
# waits 5 s for /dev/qseecom (no TrustZone driver; qseecomd is stubbed): a link and an unusable placeholder end both waits
# "prep.sh frp" runs from "on post-fs" of zz_t545_km.rc, once the vendor's mount_all mounted the persist partition: see 3. below
if [ "$1" = frp ]; then
  F=/mnt/vendor/persist/t545/frp.img
  if [ ! -s $F ]; then
    mkdir -p ${F%/*} && truncate -s 512K $F.new && mv $F.new $F && sync && echo "t545-prep: frp: created $F" > /dev/kmsg
  fi
  # loop6 holds the RAM placeholder since early-init (apexd's early-fs bootstrap takes every free loop device for an APEX); apexd's
  # next round is in post-fs-data, so the swap here does not race with it
  if losetup -d /dev/block/loop6 && losetup /dev/block/loop6 $F; then
    echo "t545-prep: frp: loop6 = $F" > /dev/kmsg
  else
    # keep (or put back) the RAM placeholder: the service needs a block device (no protection after a reset then)
    losetup /dev/block/loop6 /dev/t545tmp/frp.img 2>/dev/null
    echo "t545-prep: frp: $F unusable, RAM block on loop6" > /dev/kmsg
  fi
  exit 0
fi
mkdir -p /dev/block/platform/soc && ln -sf /dev/block/platform/soc@0/7c4000.mmc /dev/block/platform/soc/7c4000.mmc
[ -e /dev/qseecom ] || { touch /dev/qseecom && chmod 000 /dev/qseecom; }
D=/system/etc/init/t545
log() { echo "t545-prep: $*" > /dev/kmsg; }
blk() { # make sure /dev/block/$1 exists (ueventd does not create every node)
  for i in 1 2 3 4 5; do [ -b /dev/block/$1 ] && return; sleep 1; done
  MM=$(cat /sys/class/block/$1/dev); mknod /dev/block/$1 b ${MM%:*} ${MM#*:}
}

# 1. software KeyMint: declare it in the vendor VINTF manifest (servicemanager refuses undeclared VINTF-stable AIDL services) and drop the
# HIDL keymaster@4.0 declaration (keystore2 would wait for it forever: the vendor one needs TrustZone)
mkdir -p $D/manifest.d
cp /vendor/etc/vintf/manifest/* $D/manifest.d/
cp $D/vintf/*.xml $D/manifest.d/
# the Samsung AIDL power HAL (HyPer) hard-resets this tablet as soon as system_server uses it, and a declared but missing IPower/default makes
# system_server wait for it forever: drop the declaration, PowerManager then falls back to the HIDL power@1.0 HAL that is also declared
rm -f $D/manifest.d/power-samsung.xml
# Samsung's Codec2 store "default" (in the 32-bit media.codec, Android 11, ION-only c2.sec.* codecs) became CCodec's preferred store, which
# also maps buffer usage to a DMA-BUF heap: with it every Codec2 decode failed with "cannot allocate memory at all" (no ogg ringtones,
# lock/click sounds). Undeclared, CCodec keeps the platform mapping (/dev/dma_heap/system) and the c2.android.* software codecs.
rm -f $D/manifest.d/sec_c2_manifest_default.xml
# init runs us with umask 077; servicemanager/hwservicemanager read these as uid system
chmod 755 $D/manifest.d
chmod 644 $D/manifest.d/* $D/manifest.xml
mount -o bind $D/manifest.d /vendor/etc/vintf/manifest && log "vintf fragments bound: $(ls $D/manifest.d | wc -l) files"
mount -o bind $D/manifest.xml /vendor/etc/vintf/manifest.xml && log "vintf manifest.xml bound"

# 1b. RROs (rro/: lineage-sdk assist key = Active key, Wi-Fi softap without driver ACS): read-only overlayfs that adds it to /product/overlay before PackageManager scans
mkdir -p /dev/t545povl
cp $D/T545*Overlay.apk /dev/t545povl/ && chmod 755 /dev/t545povl && chmod 644 /dev/t545povl/*
restorecon -R /dev/t545povl
# the vendor file_contexts pin /dev/dri/renderD128 to graphics_device; apps reach the GPU through gpu_device
chcon u:object_r:gpu_device:s0 /dev/dri/renderD128
mount -t overlay overlay -o lowerdir=/dev/t545povl:/product/overlay /product/overlay && log "product overlay: +$(ls /dev/t545povl)"

# 2. /data: the real "userdata" partition, unencrypted, f2fs like the /data of a current Android (casefold, project quota, quota, compression,
# verity): vold can give every app's Android/data its project ID and /data/media is casefolded. Formatted by this script when the partition
# is not f2fs yet (25.10: the user chose to wipe the old ext4 /data, no backup). f2fs is recognised by its superblock magic, never by a failed
# mount: a mount problem must not wipe the data. Fallback if the partition cannot be found: ext4 on a loop device over an image in zram
# (RAM, fresh on every boot; apexd needs /sys/class/block/<dev>/mq/0/nr_tags, which zram itself lacks).
UD=""
for p in /sys/class/block/mmcblk0p*; do
  grep -q '^PARTNAME=userdata$' $p/uevent 2>/dev/null && UD=${p##*/}
done
# sanity: userdata of this tablet is ~51 GB (107 M sectors); anything very different means the name match is wrong -> never format it
if [ -n "$UD" ]; then
  SZ=$(cat /sys/class/block/$UD/size)
  if [ "$SZ" -lt 40000000 ] || [ "$SZ" -gt 400000000 ]; then log "refusing to touch $UD: unexpected size $SZ sectors"; UD=""; fi
fi
FST=ext4
OPTS="noatime,nosuid,nodev,noauto_da_alloc,data=ordered,errors=remount-ro"
FLAGS="latemount,wait,check"
if [ -n "$UD" ]; then
  blk $UD
  DATADEV=/dev/block/$UD
  # f2fs superblock magic 0xF2F52010 at byte 1024 (little endian)
  MAGIC=$(dd if=$DATADEV bs=1024 skip=1 count=1 2>/dev/null | od -An -tx1 -N4 | tr -d '[:space:]')
  if [ "$MAGIC" != "1020f5f2" ]; then
    log "userdata $DATADEV is not f2fs (magic $MAGIC): formatting it as f2fs ($(cat /sys/class/block/$UD/size) sectors)"
    # as fs_mgr_do_format would (-g android: encrypt, project_quota, extra_attr, verity), plus quota, casefold and compression
    /system/bin/make_f2fs -g android -O project_quota,extra_attr,quota,verity,encrypt -O casefold -C utf8 -O compression -w 4096 -b 4096 $DATADEV 2>&1 | grep -i -E "error|enable|total sectors|successful" | while read l; do log "make_f2fs userdata: $l"; done
    MAGIC=$(dd if=$DATADEV bs=1024 skip=1 count=1 2>/dev/null | od -An -tx1 -N4 | tr -d '[:space:]')
    log "userdata after make_f2fs: magic $MAGIC"
    FRESH=1
  else
    log "userdata $DATADEV is f2fs"
  fi
  # file-based encryption (25.10, the user agreed to one wipe): an f2fs /data without our marker predates FBE and is formatted once.
  # Only when the read-only mount works: a mount failure must never wipe the data.
  if [ "$MAGIC" = "1020f5f2" ] && mkdir -p /dev/t545ud && mount -t f2fs -o ro $DATADEV /dev/t545ud 2>/dev/null; then
    FBE=0; [ -e /dev/t545ud/.t545_fbe ] && FBE=1
    umount /dev/t545ud
    if [ $FBE = 0 ]; then
      if [ "$FRESH" != 1 ]; then
        log "userdata has no FBE marker: formatting it once for file-based encryption"
        /system/bin/make_f2fs -g android -O project_quota,extra_attr,quota,verity,encrypt -O casefold -C utf8 -O compression -w 4096 -b 4096 $DATADEV > /dev/null 2>&1
      fi
      mount -t f2fs $DATADEV /dev/t545ud && touch /dev/t545ud/.t545_fbe && sync && umount /dev/t545ud && log "userdata: FBE marker written"
    fi
  else
    log "userdata f2fs read-only mount check failed: not formatting"
  fi
  FST=f2fs
  # no fsync_mode=nobarrier (this tablet has had hard resets), no inlinecrypt (no ICE driver), no checkpoint=fs (no /metadata)
  OPTS="noatime,nosuid,nodev,discard,reserve_root=32768,resgid=1065,compress_extension=apk,compress_extension=so,compress_extension=vdex,compress_extension=odex,atgc,checkpoint_merge"
  FLAGS="latemount,wait,check,quota,reservedsize=128M,fscompress,fileencryption=aes-256-xts:aes-256-cts:v2"
else
  log "no userdata partition found, falling back to RAM /data"
  ZN=$(cat /sys/class/zram-control/hot_add)
  echo 3221225472 > /sys/block/zram$ZN/disksize
  blk zram$ZN
  /system/bin/mke2fs -F -q -t ext4 -m 0 -b 4096 -E nodiscard /dev/block/zram$ZN 2>&1 | while read l; do log "mke2fs zram: $l"; done
  mkdir -p /dev/t545ram
  mount -t ext4 /dev/block/zram$ZN /dev/t545ram || log "container mount failed"
  truncate -s 2500M /dev/t545ram/userdata.img
  blk loop7
  losetup /dev/block/loop7 /dev/t545ram/userdata.img || log "losetup failed"
  /system/bin/mke2fs -F -q -t ext4 -m 0 -b 4096 -E nodiscard /dev/block/loop7 2>&1 | while read l; do log "mke2fs loop7: $l"; done
  DATADEV=/dev/block/loop7
fi
log "data device: $(ls -l $DATADEV) mq: $(ls /sys/class/block/${DATADEV##*/}/mq 2>&1 | head -1)"
sed "s#^/dev/block/bootdevice/by-name/userdata[[:space:]].*#$DATADEV /data $FST $OPTS $FLAGS#" /vendor/etc/fstab.qcom > $D/fstab.qcom
chmod 644 $D/fstab.qcom
mount -o bind $D/fstab.qcom /vendor/etc/fstab.qcom && log "fstab: $(grep ' /data ' /vendor/etc/fstab.qcom | cut -c1-90)"

# 3. FRP: ro.frp.pst=/dev/block/persistent is a symlink to the REAL persistent partition (mmcblk0p42), which must never be used: its digest
# does not match, so PersistentDataBlockService would "format" it and clear the OEM-unlock byte the Samsung bootloader reads (backup:
# work/mainline/backup/frp_20261025). The link goes to loop6 first (vendor init's own "symlink" then fails with "File exists"), and
# "prep.sh frp" attaches loop6 at post-fs to a 512 KB file on the persist partition, which neither a /data format nor a boot image flash
# touches: the factory reset protection data survives them. Until then loop6 holds a RAM placeholder, or apexd's early-fs bootstrap takes
# it for an APEX. The loop device must be readable by uid system.
mkdir -p /dev/block /dev/t545tmp
mount -t tmpfs -o size=1m tmpfs /dev/t545tmp
truncate -s 512K /dev/t545tmp/frp.img
blk loop6
losetup /dev/block/loop6 /dev/t545tmp/frp.img && chown system:system /dev/block/loop6 && chmod 660 /dev/block/loop6 && ln -s /dev/block/loop6 /dev/block/persistent && log "frp link: $(ls -l /dev/block/persistent)"
