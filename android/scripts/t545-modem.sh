#!/system/bin/sh
# "on post-fs-data" (zz_t545_gfx.rc): boots the modem (MSS, mainline qcom_q6v5_mss), which runs the WCN3990 Wi-Fi firmware; ath10k_snoc
# (built in) then creates wlan0 as soon as the firmware's QMI service appears. Also links the Bluetooth firmware for hci_qca.
log() { echo "t545-modem: $*" > /dev/kmsg; }
F=/dev/t545fw
R=/data/vendor/t545rmtfs

# boot time: "modem.sh mss" is the t545_mss service, started at the end of the post-fs-data run below: post-fs-data waited ~7 s for
# the MSS boot (rmtfs registration, firmware load and authentication) and nothing in the boot needs Wi-Fi
# Starting the MSS twice in one boot (stop + start: "port failed halt", then the MBA load) resets the SoC, so start it once. A start that
# resets the SoC must not repeat on every boot: the marker survives the reset and skips the next start.
if [ "$1" = mss ]; then
  if [ -f $R/.starting ]; then
    log "previous modem start did not finish, skipped (rm $R/.starting to retry)"
    exit 0
  fi
  for r in /sys/class/remoteproc/remoteproc*; do
    [ "$(cat $r/name)" = 4080000.remoteproc ] || continue
    touch $R/.starting
    sync
    sleep 2   # rmtfs has to register its QMI service before the modem asks for it
    echo start > $r/state
    log "mss: $(cat $r/state)"
    [ "$(cat $r/state)" = running ] && rm $R/.starting
  done
  exit 0
fi

# firmware: MBA + MPSS from the "modem" partition, wlanmdsp.mbn (fetched by the modem over TFTP) and the board data from apnhlos,
# firmware-5.bin = linux-firmware's ath10k/WCN3990/hw1.0 feature file (no code), BT patch/NVM from /vendor/firmware
for f in /vendor/firmware-modem/image/*; do ln -sf $f $F/; done
ln -sf /vendor/firmware_mnt/image/wlanmdsp.mbn $F/
W=$F/ath10k/WCN3990/hw1.0
mkdir -p $F/qca $W
ln -sf /vendor/firmware/crbtfw21.tlv /vendor/firmware/crnv21.bin $F/qca/
cp /system/etc/init/t545/firmware-5.bin $W/
ln -sf /vendor/firmware_mnt/image/bdwlan.bin $W/board.bin
# the vendor Wi-Fi HAL "loads the driver" by writing ON to qcacld's /dev/wlan; ath10k is built in, a plain file is enough
touch /dev/wlan
chmod 666 /dev/wlan

# EFS: rmtfs serves file copies, read once from the partitions; the modem never writes modemst1/modemst2/fsg/fsc themselves
if [ ! -f $R/modem_fs1 ]; then
  rm -rf $R.new
  mkdir -p $R.new
  for p in modemst1:fs1 modemst2:fs2 fsg:fsg fsc:fsc; do
    dd if=/dev/block/by-name/${p%:*} of=$R.new/modem_${p#*:} bs=1M 2>/dev/null || { log "dd ${p%:*} failed, modem not started"; exit 0; }
  done
  mv $R.new $R
  log "EFS copies created"
fi
mkdir -p /data/vendor/tmp/tqftpserv
start t545_tqftpserv
start t545_rmtfs
start t545_mss
