#!/bin/bash
# SPDX-License-Identifier: Apache-2.0
# Offline replica of Android init's split-policy compile (system/core/init/selinux.cpp) for the SM-T545 mainline setup,
# from the cil files copied off the tablet (dev/), plus extra cil files given as arguments (our rules).
# usage: compile.sh OUT [extra.cil ...]
set -e
D=/build2/policy/mainline/dev; OUT=$1; shift
SECILC=/build2/lineage_out/host/linux-x86/bin/secilc
V=$(cat $D/vendor/plat_sepolicy_vers.txt)
ARGS=("${PLAT:-$D/system/plat_sepolicy.cil}" -m -M true -G -N -c ${POLICYVERS:-35} "$D/system/mapping/$V.cil" "$D/system/mapping/$V.compat.cil"
      "$D/system_ext/system_ext_sepolicy.cil" "$D/system_ext/mapping/$V.cil" "$D/system_ext/mapping/$V.compat.cil"
      "$D/product/product_sepolicy.cil" "$D/product/mapping/$V.cil"
      "$D/vendor/plat_pub_versioned.cil" "$D/vendor/vendor_sepolicy.cil" "$@" ${SECILC_EXTRA} -o "$OUT" -f /dev/null)
for f in "${ARGS[@]}"; do case "$f" in /*.cil) [ -f "$f" ] || { echo "missing $f"; exit 2; };; esac; done
$SECILC "${ARGS[@]}"
ls -la "$OUT" | awk '{print $5}'
