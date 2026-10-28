// SPDX-License-Identifier: Apache-2.0
// SM-T545 vibrator, AIDL android.hardware.vibrator.IVibrator V3 (the Android 16 framework has no HIDL vibrator support any more).
// A coin DC motor driven on/off by the kernel timed_output node of drivers/misc/t545-vibrator.c: no amplitude, no effects, no composition.
// The framework falls back to plain on/off waveforms for everything it cannot do with the capabilities reported here (none).
#include <aidl/android/hardware/vibrator/BnVibrator.h>
#include <android/binder_manager.h>
#include <android/binder_process.h>
#include <android/log.h>
#include <fcntl.h>
#include <unistd.h>

using namespace aidl::android::hardware::vibrator;
using ndk::ScopedAStatus;

static const char* kEnable = "/sys/class/timed_output/vibrator/enable";

static ScopedAStatus unsupported() { return ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION); }

static ScopedAStatus writeEnable(int ms) {
    int fd = open(kEnable, O_WRONLY | O_CLOEXEC);
    if (fd < 0) {
        __android_log_print(ANDROID_LOG_ERROR, "t545-vibrator", "open %s failed", kEnable);
        return ScopedAStatus::fromServiceSpecificError(1);
    }
    int n = dprintf(fd, "%d", ms);
    close(fd);
    return n > 0 ? ScopedAStatus::ok() : ScopedAStatus::fromServiceSpecificError(2);
}

class Vibrator : public BnVibrator {
  public:
    ScopedAStatus getCapabilities(int32_t* c) override { *c = 0; return ScopedAStatus::ok(); }
    ScopedAStatus off() override { return writeEnable(0); }
    ScopedAStatus on(int32_t ms, const std::shared_ptr<IVibratorCallback>&) override { return writeEnable(ms); }
    ScopedAStatus perform(Effect, EffectStrength, const std::shared_ptr<IVibratorCallback>&, int32_t*) override { return unsupported(); }
    ScopedAStatus performVendorEffect(const VendorEffect&, const std::shared_ptr<IVibratorCallback>&) override { return unsupported(); }
    ScopedAStatus getSupportedEffects(std::vector<Effect>* e) override { e->clear(); return ScopedAStatus::ok(); }
    ScopedAStatus setAmplitude(float) override { return unsupported(); }
    ScopedAStatus setExternalControl(bool) override { return unsupported(); }
    ScopedAStatus getCompositionDelayMax(int32_t*) override { return unsupported(); }
    ScopedAStatus getCompositionSizeMax(int32_t*) override { return unsupported(); }
    ScopedAStatus getSupportedPrimitives(std::vector<CompositePrimitive>* p) override { p->clear(); return ScopedAStatus::ok(); }
    ScopedAStatus getPrimitiveDuration(CompositePrimitive, int32_t*) override { return unsupported(); }
    ScopedAStatus compose(const std::vector<CompositeEffect>&, const std::shared_ptr<IVibratorCallback>&) override { return unsupported(); }
    ScopedAStatus getSupportedAlwaysOnEffects(std::vector<Effect>* e) override { e->clear(); return ScopedAStatus::ok(); }
    ScopedAStatus alwaysOnEnable(int32_t, Effect, EffectStrength) override { return unsupported(); }
    ScopedAStatus alwaysOnDisable(int32_t) override { return unsupported(); }
    ScopedAStatus getResonantFrequency(float*) override { return unsupported(); }
    ScopedAStatus getQFactor(float*) override { return unsupported(); }
    ScopedAStatus getFrequencyResolution(float*) override { return unsupported(); }
    ScopedAStatus getFrequencyMinimum(float*) override { return unsupported(); }
    ScopedAStatus getBandwidthAmplitudeMap(std::vector<float>*) override { return unsupported(); }
    ScopedAStatus getPwlePrimitiveDurationMax(int32_t*) override { return unsupported(); }
    ScopedAStatus getPwleCompositionSizeMax(int32_t*) override { return unsupported(); }
    ScopedAStatus getSupportedBraking(std::vector<Braking>* b) override { b->clear(); return ScopedAStatus::ok(); }
    ScopedAStatus composePwle(const std::vector<PrimitivePwle>&, const std::shared_ptr<IVibratorCallback>&) override { return unsupported(); }
    ScopedAStatus getFrequencyToOutputAccelerationMap(std::vector<FrequencyAccelerationMapEntry>*) override { return unsupported(); }
    ScopedAStatus getPwleV2PrimitiveDurationMaxMillis(int32_t*) override { return unsupported(); }
    ScopedAStatus getPwleV2PrimitiveDurationMinMillis(int32_t*) override { return unsupported(); }
    ScopedAStatus getPwleV2CompositionSizeMax(int32_t*) override { return unsupported(); }
    ScopedAStatus composePwleV2(const CompositePwleV2&, const std::shared_ptr<IVibratorCallback>&) override { return unsupported(); }
};

int main() {
    ABinderProcess_setThreadPoolMaxThreadCount(0);
    auto vib = ndk::SharedRefBase::make<Vibrator>();
    binder_status_t st = AServiceManager_addService(vib->asBinder().get(), (std::string(Vibrator::descriptor) + "/default").c_str());
    if (st != STATUS_OK) {
        __android_log_print(ANDROID_LOG_ERROR, "t545-vibrator", "addService failed: %d", st);
        return 1;
    }
    ABinderProcess_joinThreadPool();
    return 1;
}
