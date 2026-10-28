#!/system/bin/sh
# SM-T545: 3.5 mm jack -> audio route. The tinyalsa HAL has one PCM (FE MultiMedia1) and q6routing keeps one back end per front end, so the
# speaker (SEC_MI2S_RX -> CS35L41) and the headphones (INT0_MI2S_RX -> LPASS digital codec RX1/RX2 -> PM660L HPHL/HPHR) are exclusive:
# plug in -> headphones, plug out -> speaker. The kernel (sdm845.c, GPIO jack) powers/unpowers the "Headphone Jack" DAPM pin by itself.
# A stream that is already running keeps its old route until it is re-opened (AudioFlinger standby).
M="tinymix -D 0"
log() { echo "t545-jack: $*" > /dev/kmsg; }
route() {
  if [ "$1" = 1 ]; then
    $M "Digital RX1 MIX1 INP1" RX1; $M "Digital RX2 MIX1 INP1" RX2; $M "Analog RDAC2 MUX" RX2
    $M "Analog HPHL" Switch; $M "Analog HPHR" Switch
    $M "INT0_MI2S_RX Audio Mixer MultiMedia1" 1
    log "headphones"
  else
    $M "SEC_MI2S_RX Audio Mixer MultiMedia1" 1
    $M "Analog HPHL" ZERO; $M "Analog HPHR" ZERO; $M "Digital RX1 MIX1 INP1" ZERO; $M "Digital RX2 MIX1 INP1" ZERO
    log "speaker"
  fi
}
dev=
for n in /sys/class/input/input*; do
  [ "$(cat $n/name)" = "SM-T545 Headset Jack" ] && dev=/dev/input/$(basename $n/event*)
done
[ -n "$dev" ] || { log "no jack input device"; exit 1; }
sleep 15   # after t545-postboot.sh set the speaker route
route $(( 0x$(getevent -S $dev) >> 2 & 1 ))
getevent -l $dev | while read -r type code val; do
  [ "$code" = SW_HEADPHONE_INSERT ] && route $(( 0x$val & 1 ))
done
