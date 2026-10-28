#!/system/bin/sh
# SPDX-License-Identifier: Apache-2.0
# "on post-fs": /cache is mounted. Put the payload libraries in front of /vendor/lib64 with a read-only overlay (lower layers only):
# vulkan.pastel (SwiftShader) and the minigbm mapper in hw/, ANGLE in egl/, libdmabufheap/libdrm/libminigbm_gralloc_msm.
log() { echo "t545-gfx: $*" > /dev/kmsg; }
P=/cache/t545
# boot time: "gfx.sh adsp" is the t545_adsp service, started by the post-fs run below once the firmware links and the hexagonrpcd
# tree are in place, so that post-fs does not wait ~1.4 s for the ADSP firmware load and authentication
if [ "$1" = adsp ]; then
  for r in /sys/class/remoteproc/remoteproc*; do
    case "$(cat $r/name 2>/dev/null)" in *62400000*|adsp*)
      [ "$(cat $r/state)" = running ] || echo start > $r/state
      log "adsp ($r): $(cat $r/state)";;
    esac
  done
  start t545_hrpcd
  # Speakers: route MultiMedia1 (card 0 device 0, what the tinyalsa HAL opens) to SECONDARY_MI2S_RX -> two CS35L41, amp gain 14/20.
  # The card appears once the ADSP is up.
  for i in $(seq 1 60); do [ -e /dev/snd/controlC0 ] && break; sleep 1; done
  tinymix -D 0 "SEC_MI2S_RX Audio Mixer MultiMedia1" 1
  tinymix -D 0 "SpkL Analog PCM Volume" 14
  tinymix -D 0 "SpkR Analog PCM Volume" 14
  log "speaker route: $(tinymix -D 0 'SEC_MI2S_RX Audio Mixer MultiMedia1' 2>&1)"
  exit 0
fi
if [ -d $P/vlib ]; then
  mount -t overlay overlay -o lowerdir=$P/vlib:/vendor/lib64 /vendor/lib64 && log "overlay on /vendor/lib64: $(ls $P/vlib | tr '\n' ' ')"
  # 32-bit payload (the vendor audio service is 32-bit): audio.primary.qcom.so = tinyalsa HAL, found before Qualcomm's audio.primary.sdm710.so
  [ -d $P/vlib32 ] && mount -t overlay overlay -o lowerdir=$P/vlib32:/vendor/lib /vendor/lib && log "overlay on /vendor/lib: $(ls $P/vlib32 | tr '
' ' ')"
else
  log "no $P/vlib payload"
fi
ls -l /dev/dri 2>&1 | while read l; do log "dri: $l"; done
# Adreno 615 (msm/adreno): SQE + GMU microcode live in /vendor/firmware, the Samsung-signed zap shader in /vendor/firmware_mnt/image;
# firmware_class searches one extra directory only, so collect them in a tmpfs one. The kernel loads the GPU on the first open of a DRM node.
mkdir -p /dev/t545fw
cp /vendor/firmware/a630_sqe.fw /vendor/firmware/a630_gmu.bin /vendor/firmware_mnt/image/a615_zap.* /dev/t545fw/ 2>/dev/null
echo -n /dev/t545fw > /sys/module/firmware_class/parameters/path
log "gpu firmware: $(ls /dev/t545fw | tr '\n' ' ')"
# ADSP (audio + sensor hub): Samsung-signed adsp.mdt/.bNN on the modem vfat partition. The remoteproc probed while no firmware was
# reachable (auto-boot from the initramfs failed), so link the image into the firmware directory and start it by hand.
for f in /vendor/firmware_mnt/image/adsp.*; do ln -sf $f /dev/t545fw/; done
restorecon -R /dev/t545fw
# Sensor hub on the ADSP: it reads its registry/config through FastRPC from the HLOS. The stock sscrpcd speaks the downstream adsprpc
# ABI, so hexagonrpcd (linux-msm/hexagonrpc, NDK build) serves them instead from a tree of links to the stock files.
H=/dev/t545hrpc
mkdir -p $H/sensors $H/dsp
ln -sf /mnt/vendor/persist/sensors/registry/registry $H/sensors/registry
# Sensor configs: the stock ones plus the files in /cache/t545/sensors_cfg. Those rotate the ICM-4x6xx/AK0991x axes by 180 degrees
# (x/y negated in ".orient"): stock Android rotates the panel by 180 in SurfaceFlinger, this display path does not. The hub takes the
# orientation from these JSON files on every start (it then tries to write it to the registry, which hexagonrpcd refuses).
mkdir -p $H/cfg
cp /vendor/etc/sensors/config/* $H/cfg/
cp /cache/t545/sensors_cfg/*.json $H/cfg/ 2>/dev/null
chmod 644 $H/cfg/*
ln -sfn $H/cfg $H/sensors/config
ln -sf /vendor/etc/sensors/sns_reg_config $H/sensors/sns_reg.conf
ln -sf /vendor/lib/rfsa/adsp $H/dsp/adsp
# ADSP start, FastRPC server and speaker route: t545_adsp (see the top)
start t545_adsp
# GL driver: "angle" (ANGLE on the payload's Vulkan ICD) unless /cache/t545/egl names another libEGL_<name>.so, e.g. "mesa" (freedreno);
# switching back is "echo angle > /cache/t545/egl" + reboot, without a new boot image
EGL=$(cat $P/egl 2>/dev/null)
[ -n "$EGL" ] && setprop persist.graphics.egl "$EGL"
log "egl driver: $(getprop persist.graphics.egl)"

# Camera: libcamera Android HAL (/cache/t545/vlib/hw/camera.libcamera.so, configs in /cache/t545/etc/libcamera) served by the AOSP
# 64-bit legacy provider (Android 11 build, needs the vendor linker namespace, hence the path of the stock 32-bit Samsung provider it
# replaces); the HAL runs as cameraserver and opens the CAMSS/sensor nodes, which ueventd leaves root-only.
CP=/vendor/bin/hw/vendor.samsung.hardware.camera.provider@4.0-service
[ -f $P/vbin/android.hardware.camera.provider@2.5-service_64 ] && mount -o bind $P/vbin/android.hardware.camera.provider@2.5-service_64 $CP && log "camera provider: libcamera"
chmod 666 /dev/media* /dev/video* /dev/v4l-subdev* 2>/dev/null
# the HAL's configs are in /cache/t545/etc (init.rc leaves /cache 0770 system:cache): let cameraserver traverse it
chmod o+x /cache /cache/t545
# torch for the camera HAL (quick-settings flashlight, set_torch_mode): the SM5705 flash LED of sm5705-chg-t545
T=/sys/class/leds/white:torch/brightness
chown cameraserver $T && chmod 664 $T && log "torch: $(ls -l $T)"

# Audio policy for the tinyalsa HAL: one plain PRIMARY output to the speaker (the stock Qualcomm file has offload/deep-buffer ports
# this HAL cannot open). Must be in place before audioserver starts (class main).
chmod 644 /system/etc/init/t545/audio_policy_configuration.xml
mount -o bind /system/etc/init/t545/audio_policy_configuration.xml /vendor/etc/audio_policy_configuration.xml && log "audio policy: t545"
