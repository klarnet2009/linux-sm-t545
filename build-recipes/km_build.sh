#!/bin/bash
cd /build/lineage
source build/envsetup.sh >/dev/null 2>&1
export TARGET_PRODUCT=treble_arm64_avN TARGET_RELEASE=bp2a TARGET_BUILD_VARIANT=userdebug ALLOW_MISSING_DEPENDENCIES=true
m -j8 android.hardware.security.keymint-service 2>&1 | tail -150
echo KM_BUILD_EXIT=${PIPESTATUS[0]}
