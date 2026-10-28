#!/bin/bash
# SPDX-License-Identifier: Apache-2.0
cd /build/lineage
source build/envsetup.sh >/dev/null 2>&1
export TARGET_PRODUCT=treble_arm64_avN TARGET_RELEASE=bp2a TARGET_BUILD_VARIANT=userdebug ALLOW_MISSING_DEPENDENCIES=true
m -j8 android.hardware.gatekeeper@1.0-service.software > /build2/gk_build.full.log 2>&1
echo GK_EXIT=$? >> /build2/gk_build.full.log
