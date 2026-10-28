#!/bin/bash
# SPDX-License-Identifier: Apache-2.0
# LKM935 = LKM935 (RT clamp 512 in postboot.sh) + rules5 in t545_rules4.cil (vendor_init firmware dir on empty /data, allocator memfd ioctl 0x7704 xperm)
set -e
export PATH=/build/lineage/prebuilts/clang/host/linux-x86/clang-r547379/bin:$PATH
cd /build2/mainline/linux && make ARCH=arm64 LLVM=1 -j16 Image
cd /build2/mainline/stage1 && PROT=static DISABLE=t545-disable.k.dtsi BOOTARGS_EXTRA="t545.dl=0 t545.android=1 hung_task_panic=1 panic=1 firmware_class.path=/km/regdb androidboot.wificountrycode=LV androidboot.hardware=qcom androidboot.boot_devices=soc@0/7c4000.mmc androidboot.bootdevice=7c4000.mmc androidboot.selinux=enforcing androidboot.verifiedbootstate=orange androidboot.veritymode=disabled printk.devkmsg=on fbcon=vc:2-6 logo.nologo vt.global_cursor_default=0 log_buf_len=8M cpufreq.default_governor=performance transparent_hugepage=madvise" EXTRA_CF="-DFORCE_A=0x77ffffu -DFORCE_B=0x77ffffu" ./build_stage1.sh LKM935
ls -la BOOT_LKM935.tar; sha256sum BOOT_LKM935.tar
