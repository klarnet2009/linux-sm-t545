#!/bin/sh
# SPDX-License-Identifier: Apache-2.0
# NDK build of the SM-T545 AIDL vibrator HAL (aidl generated from the frozen V3 interface of the Lineage tree)
set -e
cd "$(dirname "$0")"
AIDL=/build2/lineage_out/host/linux-x86/bin/aidl
V=/build/lineage/hardware/interfaces/vibrator/aidl/aidl_api/android.hardware.vibrator/3
rm -rf gen; mkdir -p gen/include gen/src
$AIDL --lang=ndk --min_sdk_version=35 --structured --stability=vintf --version=3 --hash=$(cat $V/.hash) -I $V -I /build/lineage/frameworks/native/aidl/binder -o gen/src -h gen/include $(find $V/android -name "*.aidl")
CXX=/build2/mesa/android-ndk-r27c/toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android35-clang++
$CXX -std=c++20 -O2 -Wall -Igen/include -I/build/lineage/frameworks/native/libs/binder/ndk/include_cpp -I/build/lineage/frameworks/native/libs/binder/ndk/include_ndk -I/build/lineage/frameworks/native/libs/binder/ndk/include_platform -o t545-vibrator-service main.cpp $(find gen/src -name "*.cpp") -Ldevlibs -lbinder_ndk -llog -Wl,--allow-shlib-undefined
ls -l t545-vibrator-service
