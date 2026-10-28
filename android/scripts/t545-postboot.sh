#!/system/bin/sh
# Runs once after sys.boot_completed (the RAM-backed /data is fresh on every boot, so this has to be repeated): make the first screen usable.
# Setup wizard and the GApps search/TTS pieces crash or eat the little RAM left (3.5 GB, /data in zram), touch/pointer overlays show whether input works.
log() { echo "t545-postboot: $*" > /dev/kmsg; }
# Android is up: zero the stub's boot counter (scratch DRAM word 1), so that the next restart starts Android again instead of the
# debug init (which a count >= 2 selects: "the previous boot died")
devmem 0xa0f00004 4 0	# toybox: width in bytes
settings put system show_touches 0
settings put system pointer_location 0
settings put global device_provisioned 1
settings put secure user_setup_complete 1
# (animations were switched off while SwiftShader rendered on the CPU; with the Adreno GPU they stay at the default 1x)
# SurfaceFlinger's content heuristic settled on a 30 Hz render rate (its own slow frames looked like 30 fps content): pin 60 Hz
settings put system min_refresh_rate 60.0
settings put system peak_refresh_rate 60.0
settings put global stay_on_while_plugged_in 7
settings put system screen_off_timeout 1800000
for p in com.google.android.setupwizard com.google.android.tts com.google.android.googlequicksearchbox com.google.android.apps.nexuslauncher; do
  pm disable-user --user 0 $p > /dev/null 2>&1
done
# Speakers: route MultiMedia1 (card 0 device 0, opened by the tinyalsa HAL audio.primary.qcom) to SECONDARY_MI2S_RX -> two CS35L41.
# (a background job of gfx.sh does not survive: init kills the process group of an exec'd command when it ends)
tinymix -D 0 "SEC_MI2S_RX Audio Mixer MultiMedia1" 1
tinymix -D 0 "SpkL Analog PCM Volume" 14
tinymix -D 0 "SpkR Analog PCM Volume" 14
log "speaker route: $(tinymix -D 0 'SEC_MI2S_RX Audio Mixer MultiMedia1' 2>&1)"
# Built-in main mic (stock mixer_paths "main-mic"): AMIC1 -> ADC1 -> DEC1 -> INT3 MI2S TX -> MultiMedia2 (pcm C0D2, the HAL's capture
# device; capture on MultiMedia1 would drop the speaker route). TX1 digital gain +18 dB (84 = 0 dB): at 0 dB a tone from the speakers
# right next to the mic peaked at -30 dBFS.
tinymix -D 0 "MultiMedia2 Mixer INT3_MI2S_TX" 1
tinymix -D 0 "Digital DEC1 MUX" ADC1
tinymix -D 0 "Analog ADC1 Volume" 4
tinymix -D 0 "Digital TX1 Digital Volume" 102
log "mic route: $(tinymix -D 0 'MultiMedia2 Mixer INT3_MI2S_TX' 2>&1)"
# Aperture lays out in landscape only without the Android 16 large-screen resizability override (per-app compat state lives in /data)
am compat disable UNIVERSAL_RESIZABLE_BY_DEFAULT org.lineageos.aperture > /dev/null 2>&1
# From the lock screen Aperture asks for PORTRAIT; the large-screen ignoreOrientationRequest letterboxed it in landscape.
am compat enable OVERRIDE_RESPECT_REQUESTED_ORIENTATION org.lineageos.aperture > /dev/null 2>&1
# A CPU stuck with interrupts off (seen once in s2idle) only gives an RCU stall here, no lockup detector: panic, so
# that pstore keeps the stacks and the tablet reboots instead of hanging for hours
echo 1 > /proc/sys/kernel/panic_on_rcu_stall
# Samsung services that only restart in a loop here and flood the kernel log: cass (needs VaultKeeper/TEE, exits every 15 s) and
# factory.ssc (factory sensor test; the zz_t545_stubs.rc stub loses to the vendor init.qcom.rc definition)
stop cass
stop factory_ssc
# the QTI perf HAL waits for vendor.display.config@2.0 every second (msm-only, no such HAL on mainline): 2 kernel log lines per second
stop perf-hal-2-2
log "aperture compat set, cass $(getprop init.svc.cass), factory_ssc $(getprop init.svc.factory_ssc), perf-hal $(getprop init.svc.perf-hal-2-2)"
# Video recording: MediaCodec input surfaces otherwise live in the 32-bit Samsung media.codec (OMX), whose stock Qualcomm gralloc
# cannot allocate here ("createGraphicBuffer failed" for every frame -> no video at all). -1 = GraphicBufferSource in the app.
setprop debug.stagefright.c2inputsurface -1
# Games: Android 16 ignores apps' orientation requests on large screens (sw >= 600dp, here 800dp) and the display follows the sensor,
# so a landscape game turned to portrait shrank into a letterboxed strip. Respect fixed orientations like the stock firmware (kept in
# display_settings.xml). Rotation mapping stays standard: keys at the bottom = ROTATION_270 = REVERSE_LANDSCAPE, which Unity requests there.
wm set-ignore-orientation-request false
log "ignoreOrientationRequest: $(wm get-ignore-orientation-request 2>&1)"
# Background work on the six silver cores like the stock post_boot (sdm710 branch); the vendor init.target.rc opens the cpusets to 0-7
echo 0-5 > /dev/cpuset/background/cpus
echo 0-5 > /dev/cpuset/system-background/cpus
echo 0-5 > /dev/cpuset/restricted/cpus
log "cpusets: background $(cat /dev/cpuset/background/cpus) system-background $(cat /dev/cpuset/system-background/cpus) restricted $(cat /dev/cpuset/restricted/cpus)"
# RT tasks (SurfaceFlinger, RenderEngine, hwc, input, FastMixer, audio HAL) boosted their cluster to the top step on every wakeup
# (default 1024). 512 (A/B 26.10, two rounds each): ~45 mA less on battery while scrolling with audio playing (~5 %), median frame
# 19-20 ms like 1024 (0 gave 22-24 ms), FastMixer underruns unchanged, idle current unchanged
echo 512 > /proc/sys/kernel/sched_util_clamp_min_rt_default
log "rt uclamp min default: $(cat /proc/sys/kernel/sched_util_clamp_min_rt_default)"
am kill-all
input keyevent KEYCODE_HOME
log "done"
