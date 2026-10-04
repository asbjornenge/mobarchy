/*
 * mobarchy-sensormanager: Android's sensor service for HALs, in the Android container.
 *
 * HALs (the Pixel camera HAL: the gyro, for stabilisation) reach the sensors through
 * android.frameworks.sensorservice.ISensorManager/default. On Android, system_server hosts
 * it: Android's own implementation (libsensorserviceaidl's SensorManagerAidl), in front of
 * sensorservice. The container runs no system_server, so this hosts that same
 * implementation, unchanged; sensorservice (/system/bin/sensorservice) runs as its own
 * service.
 *
 * SensorManagerAidl isn't NDK API, so it is reached the way the platform's own code links
 * it: its constructor and BnSensorManager::createBinder, by their (stable, C++) symbols.
 */
#include <android/binder_ibinder.h>
#include <android/log.h>

#include <dlfcn.h>
#include <stdlib.h>

#define LOG(...) __android_log_print(ANDROID_LOG_INFO, "mobarchy-sensormanager", __VA_ARGS__)
#define ERR(...) __android_log_print(ANDROID_LOG_ERROR, "mobarchy-sensormanager", __VA_ARGS__)

#define INSTANCE "android.frameworks.sensorservice.ISensorManager/default"

/* SensorManagerAidl is about 0xa0 bytes (Android 17: its constructor writes up to 0x98);
 * this leaves room for it to grow */
#define OBJECT_SIZE 1024

// ndk::SpAIBinder: a single AIBinder pointer, but not trivially copyable, so the ABI
// returns it through memory (x8), as createBinder() does. Never released: the service
// lives as long as the process.
struct SpAIBinder {
    AIBinder *binder;
    ~SpAIBinder() {}
};

typedef void (*Constructor)(void *self, void *java_vm);
typedef SpAIBinder (*CreateBinder)(void *self);

template <typename T>
static T symbol(void *lib, const char *name)
{
    T f = reinterpret_cast<T>(dlsym(lib, name));
    if (!f)
        ERR("%s: %s", name, dlerror());
    return f;
}

int main()
{
    void *impl = dlopen("libsensorserviceaidl.so", RTLD_NOW);
    void *aidl = dlopen("android.frameworks.sensorservice-V1-ndk.so", RTLD_NOW);
    void *ndk = dlopen("libbinder_ndk.so", RTLD_NOW);
    if (!impl || !aidl || !ndk) {
        ERR("dlopen: %s", dlerror());
        return 1;
    }
    auto construct = symbol<Constructor>(impl,
        "_ZN7android10frameworks13sensorservice14implementation17SensorManagerAidlC1EP7_JavaVM");
    auto create_binder = symbol<CreateBinder>(aidl,
        "_ZN4aidl7android10frameworks13sensorservice15BnSensorManager12createBinderEv");
    // (not in the NDK's stubs: platform API, in the same library)
    auto add_service = symbol<binder_status_t (*)(AIBinder *, const char *)>(ndk,
        "AServiceManager_addService");
    auto set_max_threads = symbol<bool (*)(uint32_t)>(ndk,
        "ABinderProcess_setThreadPoolMaxThreadCount");
    auto start_thread_pool = symbol<void (*)()>(ndk, "ABinderProcess_startThreadPool");
    auto join_thread_pool = symbol<void (*)()>(ndk, "ABinderProcess_joinThreadPool");
    if (!construct || !create_binder || !add_service || !set_max_threads || !start_thread_pool
        || !join_thread_pool)
        return 1;

    // No Java VM: SensorManagerAidl's looper thread then runs as a plain thread (it logs
    // "No Java VM" for each event queue, which is harmless).
    void *manager = calloc(1, OBJECT_SIZE);
    construct(manager, nullptr);
    SpAIBinder binder = create_binder(manager);
    if (!binder.binder) {
        ERR("no binder for the sensor manager");
        return 1;
    }

    // HALs call in from several threads at once (an event queue per sensor reader)
    set_max_threads(8);
    start_thread_pool();
    binder_status_t status = add_service(binder.binder, INSTANCE);
    if (status != STATUS_OK) {
        ERR("could not register " INSTANCE ": %d", status);
        return 1;
    }
    LOG("registered " INSTANCE);
    join_thread_pool();
    return 0;
}
