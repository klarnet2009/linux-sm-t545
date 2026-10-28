#!/system/bin/sh
# SM5705 charger (i2c-gpio "chg" bus = i2c-0, 0x49): there is no mainline charger driver yet. The bootloader leaves it in CHG_ON (CNTL 0x0c = 5,
# nCHGEN gpio134 low) but at 500 mA input / 500 mA charge, less than the tablet draws with the screen on. Registers as in the stock driver:
# VBUSCNTL 0x0d = (mA - 100) / 25, CHGCNTL2 0x10 = (mA - 100) / 50. AICL (CHGCNTL7 bit 2, left on) backs off when the source's VBUS sags.
# shortcut: one fixed 1.5 A for every source (no cable-type detection through the MUIC); a driver should pick 500 mA for a PC port.
log() { echo "t545-charger: $*" > /dev/kmsg; }
i2cset -f -y 0 0x49 0x0d 0x38 b
i2cset -f -y 0 0x49 0x10 0x1c b
log "input $(i2cget -f -y 0 0x49 0x0d) charge $(i2cget -f -y 0 0x49 0x10) mode $(i2cget -f -y 0 0x49 0x0c)"
