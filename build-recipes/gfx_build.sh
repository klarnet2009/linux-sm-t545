#!/bin/bash
cd /build/lineage
source build/envsetup.sh >/dev/null 2>&1
export TARGET_PRODUCT=treble_arm64_avN TARGET_RELEASE=bp2a TARGET_BUILD_VARIANT=userdebug ALLOW_MISSING_DEPENDENCIES=true
m -j8 android.hardware.graphics.allocator@4.0-service.minigbm_msm android.hardware.graphics.mapper@4.0-impl.minigbm_msm android.hardware.composer.hwc3-service.drm libEGL_angle libGLESv2_angle libGLESv1_CM_angle vulkan.pastel > /build2/gfx_build.full.log 2>&1
echo GFX_BUILD_EXIT=$? >> /build2/gfx_build.full.log
tail -40 /build2/gfx_build.full.log > /build2/gfx_build.log
echo GFX_BUILD_EXIT=$(tail -1 /build2/gfx_build.full.log) >> /build2/gfx_build.log
