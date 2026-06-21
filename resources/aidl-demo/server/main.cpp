// server/main.cpp — 人类写
// Server 进程入口：创建服务 → 注册到 ServiceManager → 进 Binder 线程池

#include <binder/IPCThreadState.h>
#include <binder/IServiceManager.h>
#include <binder/ProcessState.h>
#include <utils/Log.h>
#include <cstdio>

#include "StringService.h"

using android::sp;
using android::String16;
using android::defaultServiceManager;
using android::ProcessState;
using android::IPCThreadState;
using com::example::binder::StringService;

int main() {
    setvbuf(stdout, NULL, _IONBF, 0);

    // ① 初始化 Binder 设施
    ProcessState::self()->startThreadPool();

    // ② 创建服务实例
    sp<StringService> svc = new StringService();

    // ③ 注册到 ServiceManager（名字与 .aidl 生成的 DESCRIPTOR 一致）
    sp<IServiceManager> sm = defaultServiceManager();
    status_t ret = sm->addService(
            String16("com.example.binder.IStringService"), svc);
    if (ret != android::OK) {
        fprintf(stderr, "addService failed: %d\n", ret);
        return 1;
    }
    printf("Server: registered 'com.example.binder.IStringService'\n");

    // ④ 进入 Binder 线程池主循环
    IPCThreadState::self()->joinThreadPool();
    return 0;
}
