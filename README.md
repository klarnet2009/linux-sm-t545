# Android 16 on mainline Linux 6.19 for the Samsung Galaxy Tab Active Pro (SM-T545): sources

Everything needed to understand and rebuild the images of the preview release (LKM935): kernel patches, board device tree, the stage-1 boot
stub, the initramfs init, the Android-side glue (boot scripts, rc files, SELinux policy additions, small HALs, overlays) and the build
recipes. The binary images and the install guide are in the release (`INSTALL.md`, `SHA256SUMS`).

**State of this repository: a dump of the author's build tree, cleaned of secrets, backups and binaries. It is not yet a turnkey build.**
Paths still say `/build2/...`, the recipes were written for one build server, and several inputs have to come from your own device or from
upstream (see "Not included").

## Layout

| Path | What |
|---|---|
| `kernel/BASE.txt`, `kernel/BASE_COMMIT` | Base: <https://gitlab.com/sdm670-mainline/linux> tag `sdm670-v6.19.10` |
| `kernel/patches/` | 14 patches on top of the base (`git am`), one per subsystem, with commit messages that explain the hardware reason |
| `kernel/config/t545.config` | The kernel `.config` used for the release (AOSP clang r547379, `LLVM=1`) |
| `boot/stage1/` | Stage-1 stub that makes Samsung's bootloader accept the boot image (it only takes a stock DTB) and then jumps into the 6.19 kernel; DTB patching, boot counter, quiet-boot logic |
| `boot/initramfs/init` | The initramfs init: mounts system, builds the SELinux policy overlay, overlays `/system/etc/init`, hands over to Android init |
| `boot/kexec/` | Bring-up helpers (kexec from the Samsung kernel, reboot-to-Download) |
| `android/scripts`, `android/rc` | Boot scripts and rc files (`prep.sh`: FRP block, `/data` format with FBE; `gfx.sh`; `modem.sh`; `postboot.sh`; ...) |
| `android/sepolicy` | SELinux additions (`*.cil`, `t545_file_contexts`) and the offline compile helpers |
| `android/vintf` | VINTF fragments of the services added by this build (KeyMint, gatekeeper, allocator, mapper, composer, vibrator, codec2) |
| `android/hal`, `android/rro` | Light and vibrator HALs, runtime resource overlays |
| `build-recipes/` | Scripts that built the userspace pieces (software KeyMint, Mesa/turnip, minigbm, hwc3-drm, gatekeeper, audio HAL) and the latest image build (`build_lkm935.sh`) |
| `tools/` | Small on-device probes |

## Building the kernel (outline)

```
git clone https://gitlab.com/sdm670-mainline/linux && cd linux && git checkout sdm670-v6.19.10
git am /path/to/kernel/patches/*.patch
cp /path/to/kernel/config/t545.config .config
export PATH=<AOSP prebuilts>/clang/host/linux-x86/clang-r547379/bin:$PATH
make ARCH=arm64 LLVM=1 olddefconfig && make ARCH=arm64 LLVM=1 -j"$(nproc)" Image
```

The boot image is then assembled by `boot/stage1/build_stage1.sh`, which needs the kernel `Image`, the DTB built from
`arch/arm64/boot/dts/qcom/sdm670-samsung-gtactivexl.dts`, an initramfs made from `boot/initramfs/init` plus the Android-side files, and the
**stock `dt0.dtb` taken from your own device's boot image**.

## Not included (on purpose)

* Samsung proprietary or Samsung-derived files: the stock DTB, the stock vendor VINTF `manifest.xml`, the stock `audio_policy_configuration.xml`,
  all firmware blobs. Take them from your own device or firmware.
* `ath10k` firmware and the wireless regulatory database: get them from `linux-firmware` / `wireless-regdb`.
* Compiled binaries (busybox, the keymint/rmtfs/tqftpserv/hexagonrpcd/BT HAL builds, APKs, kernel modules).
* The author's `adb_keys`: the release boot image authorises only the author's PC. Put your own public key in the initramfs.
* Sources of the camera stack (libcamera patches and the hardware-ISP driver `t545-icp`, an out-of-tree module), the Venus/Codec2 glue and the
  Mesa tree: not published yet.
* Build history: ~35 earlier `build_lkm*.sh` and the `apply_*.py` edit scripts that produced intermediate images.

## Licences

Every file carries an SPDX line; the license texts are in `LICENSES/`.

| Where | Licence | Why |
|---|---|---|
| `kernel/` | GPL-2.0 | Patches to Linux and the new kernel drivers (SPDX line of each file inside the patches) |
| `boot/` | GPL-2.0-only | The stage-1 stub embeds the VGA 8x16 font of the Linux kernel (`lib/fonts/font_8x16.c`), so the stub is a GPL-2.0 work; the kexec helpers already were |
| `android/`, `build-recipes/`, `tools/` | Apache-2.0 | Same licence as AOSP and LineageOS, which these files plug into |

A file that states its own SPDX line keeps it. Copyright 2026 klarnet2009. Authorship of third-party parts is kept in the patches (for example
the S5K3L6 binding comes from an RFC series). Not legal advice.

## Credits

Linux and the sdm670-mainline developers (Acayan, Purism and the Pixel 3a mainline work the board DTS grew from), LineageOS and the
TrebleDroid GSI work, Mesa (freedreno/turnip), AOSP.
