#!/system/bin/sh
# SPDX-License-Identifier: Apache-2.0
# Health sampler for post-mortems of the random resets: temperatures, memory pressure, battery, CPU frequencies every 30 s into
# /cache/t545/health.txt (trimmed at 1 MB). The kernel log of a reset survives in ramoops (/sys/fs/pstore/console-ramoops-0); the old
# 4 s period with a 300-line dmesg dump per cycle cost ~1 % of a CPU and ~0.5 GB/day of eMMC writes (LKM933).
D=/cache/t545
while :; do
  {
    echo "== $(date +%T) up $(cut -d' ' -f1 /proc/uptime)"
    for z in /sys/class/thermal/thermal_zone*; do echo "$(cat $z/type) $(cat $z/temp)"; done | tr '\n' ';'
    echo
    head -3 /proc/meminfo | tr '\n' ' '; cat /proc/loadavg
    cat /proc/pressure/memory | head -1
    echo "bat $(cat /sys/class/power_supply/battery/capacity) $(cat /sys/class/power_supply/battery/temp) $(cat /sys/class/power_supply/battery/voltage_now) $(cat /sys/class/power_supply/battery/current_now)"
    echo "freq $(cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_cur_freq) $(cat /sys/devices/system/cpu/cpu4/cpufreq/scaling_cur_freq) $(cat /sys/devices/system/cpu/cpu6/cpufreq/scaling_cur_freq)"
    # lost adb links: gadget state and VBUS seen by the charger
    echo "usb $(cat /sys/class/udc/a600000.dwc3/state) vbus $(cat /sys/class/power_supply/sm5705-usb/online)/$(cat /sys/class/power_supply/sm5705-ac/online) bat $(cat /sys/class/power_supply/battery/status)"
  } >> $D/health.txt 2>&1
  [ "$(stat -c %s $D/health.txt 2>/dev/null || echo 0)" -gt 1048576 ] && tail -c 400000 $D/health.txt > $D/health.old && mv $D/health.old $D/health.txt
  sleep 30
done
