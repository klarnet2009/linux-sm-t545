#!/bin/bash
# SPDX-License-Identifier: Apache-2.0
cd /build/lineage
source build/envsetup.sh >/dev/null 2>&1
export TARGET_PRODUCT=treble_arm64_avN TARGET_RELEASE=bp2a TARGET_BUILD_VARIANT=userdebug ALLOW_MISSING_DEPENDENCIES=true
m -j8 android.hardware.graphics.allocator@4.0-service.minigbm_msm android.hardware.graphics.mapper@4.0-impl.minigbm_msm > /build2/gfx3_build.full.log 2>&1
echo GFX3_EXIT=$? >> /build2/gfx3_build.full.log
