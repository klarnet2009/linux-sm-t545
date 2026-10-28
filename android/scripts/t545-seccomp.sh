#!/system/bin/sh
# SPDX-License-Identifier: Apache-2.0
# Runs from "on post-fs-data" of zz_t545_km.rc: the APEXes are mounted (init.rc waits for apexd.status=activated in its own post-fs-data
# action, which runs first) and class main (media.extractor, media.swcodec) has not started yet.
# ashmem is memfd on this kernel (sys.use_memfd): the media extractor/codec seccomp policies lack memfd_create/ftruncate/fcntl, so the
# services died with SIGSYS on every file. Fixed after boot only, SystemUI's SoundPool had already failed to load the lock/unlock sounds.
log() { echo "t545-seccomp: $*" > /dev/kmsg; }
mkdir -p /dev/t545seccomp
for S in /apex/com.android.media/etc/seccomp_policy/mediaextractor.policy /apex/com.android.media.swcodec/etc/seccomp_policy/mediaswcodec.policy; do
  C=/dev/t545seccomp/${S##*/}
  cp $S $C && printf '%s\n' "memfd_create: 1" "ftruncate: 1" "fcntl: 1" >> $C && chmod 644 $C && mount -o bind $C $S
done
log "media seccomp: $(grep -c memfd_create /apex/com.android.media/etc/seccomp_policy/mediaextractor.policy)"
