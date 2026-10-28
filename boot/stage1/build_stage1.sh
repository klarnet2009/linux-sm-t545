#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-only
# stage-1 chain-loader boot image for SM-T545:
#   kernel part of boot.img = gzip( stub | Linux Image (mainline) | our DTB | initramfs ) + the STOCK dt0.dtb
# usage: build_stage1.sh <name>
set -e
N=${1:-S1}
W=/build2/mainline
S=$W/stage1
L=$W/linux
export PATH=/build/lineage/prebuilts/clang/host/linux-x86/clang-r547379/bin:$PATH
cd $S

# initramfs
rm -rf $W/initramfs; mkdir -p $W/initramfs/bin $W/initramfs/dev $W/initramfs/proc $W/initramfs/sys $W/initramfs/tmp
cp $W/bbx/usr/bin/busybox $W/initramfs/bin/busybox
cp $W/init $W/initramfs/init
clang --target=aarch64-linux-gnu -O2 -static -nostdlib -ffreestanding -fno-stack-protector -fno-pic -fuse-ld=lld -Wl,-e,_start -Wl,--build-id=none -o $W/initramfs/bin/reboot2 $W/reboot2.c
cp -a $W/km $W/initramfs/km
chmod +x $W/initramfs/init $W/initramfs/bin/busybox
(cd $W/initramfs && find . | cpio -o -H newc --quiet | gzip -9 > $W/initramfs.cpio.gz)
RD=$W/initramfs.cpio.gz
RDSZ=$(stat -c%s $RD)

# device tree (initrd range depends on the initramfs size)
printf '/ {\n\tchosen {\n\t\tlinux,initrd-start = /bits/ 64 <0x%x>;\n\t\tlinux,initrd-end = /bits/ 64 <0x%x>;\n\t};\n};\n' $((${RDBASE:-0x84b00000})) $((${RDBASE:-0x84b00000} + RDSZ)) \
  > $L/arch/arm64/boot/dts/qcom/t545-initrd.dtsi
if [ "$PROT" = static ]; then cp $W/t545-gcc-static.dtsi $L/arch/arm64/boot/dts/qcom/t545-gcc-protected.dtsi; else
python3 - > $L/arch/arm64/boot/dts/qcom/t545-gcc-protected.dtsi <<'PYEOF'
import re
h = open("/build2/mainline/linux/include/dt-bindings/clock/qcom,gcc-sdm845.h").read()
n = max(int(m.group(2)) for m in re.finditer(r"#define (GCC_\w+)\s+(\d+)", h)) + 1
print("/* generated: every gcc clock index; the chain loader decides which stay protected */")
print("&gcc {\n\tprotected-clocks = <%s>;\n};" % " ".join(str(i) for i in range(n)))
PYEOF
fi
cp $W/sdm670-samsung-gtactivexl.dts $W/t545-usb.dtsi $W/t545-emmc.dtsi $L/arch/arm64/boot/dts/qcom/
cp $W/${MEMDTSI:-t545-memory.dtsi} $L/arch/arm64/boot/dts/qcom/t545-memory.dtsi
if [ -n "$BOOTARGS_EXTRA" ]; then sed -i "s|rdinit=/init|rdinit=/init $BOOTARGS_EXTRA|" $L/arch/arm64/boot/dts/qcom/sdm670-samsung-gtactivexl.dts; fi
cp $W/${DISABLE:-t545-disable.dtsi} $L/arch/arm64/boot/dts/qcom/t545-disable.dtsi
grep -q gtactivexl $L/arch/arm64/boot/dts/qcom/Makefile || printf 'dtb-$(CONFIG_ARCH_QCOM)\t+= sdm670-samsung-gtactivexl.dtb\n' >> $L/arch/arm64/boot/dts/qcom/Makefile
(cd $L && make ARCH=arm64 LLVM=1 -j8 qcom/sdm670-samsung-gtactivexl.dtb 2>&1 | tail -15)
DTB=$L/arch/arm64/boot/dts/qcom/sdm670-samsung-gtactivexl.dtb
KIMG=$L/arch/arm64/boot/Image
ls -l $KIMG $DTB $RD
python3 groups.py $DTB > groups.h
head -4 groups.h

# stub
python3 $W/stage0/gen_font.py >/dev/null
DEFS=$(python3 layout.py $KIMG $DTB $RD)
echo "defs: $DEFS"
CF="--target=aarch64-none-elf -O1 -ffreestanding -fno-builtin -fno-stack-protector -fno-pic -mgeneral-regs-only -mstrict-align -fno-asynchronous-unwind-tables -fno-unwind-tables -Wall -I. -DBUILD_ID=$N -DBISECT_ID=$(date +%s)u $EXTRA_CF"
clang $CF $DEFS -c stage1.c -o stage1.o
ld.lld -T $W/stage0/stage0.ld -o stage1.elf stage1.o
llvm-objcopy -O binary stage1.elf stub.bin
ls -l stub.bin
python3 layout.py --assemble stub.bin $KIMG $DTB $RD payload.bin
gzip -9 -n -c payload.bin > payload.gz
ls -l payload.gz
cat payload.gz $W/stage0/ok_dtb0.dtb > Image.gz-dtb-$N

python3 /build/los18src/system/tools/mkbootimg/mkbootimg.py --kernel Image.gz-dtb-$N --cmdline "$(cat $W/stage0/cmdline_ok.txt)" \
  --base 0x0 --kernel_offset 0x8000 --ramdisk_offset 0x0 --second_offset 0x0 --tags_offset 0x1e00000 --pagesize 4096 --header_version 1 \
  --os_version 11.0.0 --os_patch_level 2023-10 --board SRPSE21A004 -o boot_$N.img
truncate -s 64M boot_$N.img
rm -rf tar_$N; mkdir tar_$N; cp boot_$N.img tar_$N/boot.img
(cd tar_$N && tar --format=gnu -cf ../BOOT_$N.tar boot.img)
ls -l BOOT_$N.tar
sha256sum BOOT_$N.tar
echo STAGE1_DONE
