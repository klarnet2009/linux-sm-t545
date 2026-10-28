#!/bin/sh
# SPDX-License-Identifier: Apache-2.0
# Builds the SM-T545 RROs on the build server (LineageOS host tools, test key): <dir> -> T545<Name>Overlay.apk
set -e
cd "$(dirname "$0")"
export PATH=/build/lineage/prebuilts/jdk/jdk21/linux-x86/bin:$PATH
O=/build2/lineage_out; B=$O/host/linux-x86/bin; KEY=/build/lineage/build/make/target/product/security/testkey
for m in lineagesdk:T545LineageSdkOverlay wifi:T545WifiOverlay framework:T545FrameworkOverlay; do
  d=${m%:*}; apk=${m#*:}.apk
  rm -rf build && mkdir build
  $B/aapt2 compile --dir $d/res -o build/res.zip
  $B/aapt2 link -o build/unsigned.apk --manifest $d/AndroidManifest.xml -I $O/target/product/tdgsi_arm64_a/system/framework/framework-res.apk build/res.zip
  $B/apksigner sign --key $KEY.pk8 --cert $KEY.x509.pem --out $apk build/unsigned.apk
  ls -l $apk
done
