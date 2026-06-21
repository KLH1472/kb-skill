// client/main.cpp — 人类写
// Client 进程入口：查 ServiceManager → 拿到接口指针 → 调用方法

#include <binder/IServiceManager.h>
#include <binder/ProcessState.h>
#include <utils/Log.h>
#include <cstdio>

#include <com/example/binder/IStringService.h>   // AIDL 生成的接口

using android::sp;
using android::String16;
using android::String8;
using android::ProcessState;
using com::example::binder::IStringService;

int main() {
    setvbuf(stdout, NULL, _IONBF, 0);

    // ① 初始化 Binder 设施
    ProcessState::self()->startThreadPool();

    // ② 查 ServiceManager，拿到接口指针
    sp<IStringService> svc = android::waitForService<IStringService>(
            IStringService::descriptor);
    if (!svc) {
        fprintf(stderr, "Client: service not found\n");
        return 1;
    }

    // ③ 像调本地方法一样调用 — 看不见 Bp/Parcel/transact
    String16 result = svc->upper(String16("Hello Binder"));
    printf("upper: %s\n", String8(result).string());

    result = svc->lower(String16("Hello Binder"));
    printf("lower: %s\n", String8(result).string());

    return 0;
}
