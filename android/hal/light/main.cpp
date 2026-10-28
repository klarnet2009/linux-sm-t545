// SM-T545 lights, AIDL android.hardware.light.ILights V2: only the panel backlight (HX8279 PWM, /sys/class/backlight/panel).
// The stock Samsung lights HAL registers the same instance but reports no lights on this kernel, so the framework had no
// backlight and the brightness slider did nothing.
#include <aidl/android/hardware/light/BnLights.h>
#include <android/binder_manager.h>
#include <android/binder_process.h>
#include <android/log.h>
#include <condition_variable>
#include <fcntl.h>
#include <mutex>
#include <thread>
#include <unistd.h>

using namespace aidl::android::hardware::light;
using ndk::ScopedAStatus;

static const char* kBrightness = "/sys/class/backlight/panel/brightness";

// One backlight write is two DSI commands to the HX8279 (~67 ms in video mode). The framework animates brightness every
// frame and waits for setLightState, so the ramp stuttered: the call only stores the level, a writer thread sends the
// newest one (older ones are dropped).
class Lights : public BnLights {
  public:
    Lights() { std::thread([this] { writer(); }).detach(); }

    ScopedAStatus setLightState(int32_t id, const HwLightState& state) override {
        if (id != 0)
            return ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
        // the framework sends the backlight level as a grey ARGB colour
        uint32_t c = static_cast<uint32_t>(state.color);
        int level = (77 * ((c >> 16) & 0xff) + 150 * ((c >> 8) & 0xff) + 29 * (c & 0xff)) >> 8;
        {
            std::lock_guard<std::mutex> lock(mMutex);
            mWanted = level;
        }
        mCond.notify_one();
        return ScopedAStatus::ok();
    }
    ScopedAStatus getLights(std::vector<HwLight>* lights) override {
        lights->clear();
        HwLight bl;
        bl.id = 0;
        bl.ordinal = 0;
        bl.type = LightType::BACKLIGHT;
        lights->push_back(bl);
        return ScopedAStatus::ok();
    }

  private:
    void writer() {
        int written = -1;
        for (;;) {
            int level;
            {
                std::unique_lock<std::mutex> lock(mMutex);
                mCond.wait(lock, [&] { return mWanted != written; });
                level = mWanted;
            }
            int fd = open(kBrightness, O_WRONLY | O_CLOEXEC);
            if (fd < 0) {
                __android_log_print(ANDROID_LOG_ERROR, "t545-lights", "open %s failed", kBrightness);
                std::lock_guard<std::mutex> lock(mMutex);
                written = mWanted;	/* wait for the next change instead of spinning */
                continue;
            }
            dprintf(fd, "%d", level);
            close(fd);
            written = level;
        }
    }

    std::mutex mMutex;
    std::condition_variable mCond;
    int mWanted = -1;
};

int main() {
    ABinderProcess_setThreadPoolMaxThreadCount(0);
    auto lights = ndk::SharedRefBase::make<Lights>();
    binder_status_t st = AServiceManager_addService(lights->asBinder().get(), (std::string(Lights::descriptor) + "/default").c_str());
    if (st != STATUS_OK) {
        __android_log_print(ANDROID_LOG_ERROR, "t545-lights", "addService failed: %d", st);
        return 1;
    }
    ABinderProcess_joinThreadPool();
    return 1;
}
