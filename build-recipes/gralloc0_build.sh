#!/bin/bash
cd /build/lineage
source build/envsetup.sh >/dev/null 2>&1
export TARGET_PRODUCT=treble_arm64_avN TARGET_RELEASE=bp2a TARGET_BUILD_VARIANT=userdebug ALLOW_MISSING_DEPENDENCIES=true
m -j8 gralloc.minigbm_msm > /build2/gralloc0_build.full.log 2>&1
echo GRALLOC0_EXIT=$? >> /build2/gralloc0_build.full.log
