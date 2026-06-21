# AIDL Demo — 完整 Binder IPC 工程（AOSP 风格）

基于 `IStringService.aidl` → 编译器生成桩/代理 → Server/Client 三个独立 target 的完整 demo。

## 目录结构

```
aidl_demo/
├── aidl/                          # target 1: aidl_interface → com.example.binder-cpp 库
│   ├── Android.bp
│   └── com/example/binder/
│       └── IStringService.aidl    ← 接口定义
│
├── server/                        # target 2: cc_binary "myserver"
│   ├── Android.bp
│   ├── main.cpp                   ← 注册服务 + joinThreadPool
│   ├── StringService.h            ← 继承 BnStringService
│   └── StringService.cpp          ← 纯业务逻辑（转大小写）
│
├── client/                        # target 3: cc_binary "myclient"
│   ├── Android.bp
│   └── main.cpp                   ← waitForService + 调用方法
│
└── gen/                           # AIDL 编译器输出（编译时生成，不入 git）
    ├── include/com/example/binder/
    │   ├── IStringService.h       接口（Client + Server 都 include）
    │   ├── BnStringService.h      桩（只有 Server include）
    │   └── BpStringService.h      代理声明（只有 IStringService.cpp include）
    └── com/example/binder/
        └── IStringService.cpp     全实现（编译进 com.example.binder-cpp 库）
```

## Android.bp（3 个独立 target）

```bp
// aidl/Android.bp — target 1: 定义接口，自动产生 com.example.binder-cpp 库
aidl_interface {
    name: "com.example.binder",
    srcs: ["com/example/binder/*.aidl"],
    unstable: true,
}

// server/Android.bp — target 2: Server 进程
cc_binary {
    name: "myserver",
    srcs: ["main.cpp", "StringService.cpp"],
    shared_libs: [
        "com.example.binder-cpp",   // aidl_interface 自动产生的库
        "libbinder",                // BBinder / IPCThreadState / ProcessState
        "libutils",                 // String16 / sp<>
    ],
}

// client/Android.bp — target 3: Client 进程
cc_binary {
    name: "myclient",
    srcs: ["main.cpp"],
    shared_libs: [
        "com.example.binder-cpp",   // 与 Server 同一个库
        "libbinder",
        "libutils",
    ],
}
```

## 编译产物

```
IStringService.aidl
    │ AIDL 编译器
    ├── IStringService.h
    ├── BpStringService.h
    ├── BnStringService.h
    └── IStringService.cpp ──→ com.example.binder-cpp 库
                                    ├── myserver  链接
                                    └── myclient  链接
```

### .aidl → 生成文件对照

```
.aidl                               生成文件
──────────────────────────────────────────────────────────────────────
package com.example.binder;         IStringService.h
                                    class IStringService : public IInterface {
interface IStringService {               DECLARE_META_INTERFACE(StringService);
    String upper(String input);          virtual String16 upper(...) = 0;
    String lower(String input);          virtual String16 lower(...) = 0;
}                                   };

                                    BpStringService.h
                                    class BpStringService
                                        : public BpInterface<IStringService> {
                                        String16 upper(...) override;  // 声明
                                        String16 lower(...) override;
                                    };

                                    BnStringService.h
                                    class BnStringService
                                        : public BnInterface<IStringService> {
                                        static constexpr
                                            TRANSACTION_upper = ...;
                                        static constexpr
                                            TRANSACTION_lower = ...;
                                        status_t onTransact(...) override;
                                    };

                                    IStringService.cpp (唯一 .cpp)
                                    IMPLEMENT_META_INTERFACE(StringService,
                                        "com.example.binder.IStringService");

                                    // Bp 方法体 — 拆包解包 + transact
                                    String16 BpStringService::upper(input) {
                                        data.writeInterfaceToken(...);
                                        data.writeString16(input);
                                        remote()->transact(TRANSACTION_upper, ...);
                                        reply.readString16(&result);
                                        return result;
                                    }

                                    // Bn::onTransact — 拆包 → 调业务 → 打包
                                    status_t BnStringService::onTransact(...) {
                                        switch (code) {
                                        case TRANSACTION_upper:
                                            data.readString16(&input);
                                            result = this->upper(input); // ★
                                            reply->writeString16(result);
                                        }
                                    }
```

## 你写什么 vs 编译器写什么

| 文件 | 谁写 | 内容 |
|------|------|------|
| `IStringService.aidl` | 你 | 3 行接口定义 |
| `IStringService.h` | AIDL | 纯虚接口 + DESCRIPTOR + Default 类 |
| `BnStringService.h` | AIDL | 桩 + `static constexpr` 事务码 + onTransact 声明 + Delegator |
| `BpStringService.h` | AIDL | 代理方法声明（无事务码） |
| `IStringService.cpp` | AIDL | IMPLEMENT_META_INTERFACE + Bp 方法体 + Bn::onTransact 方法体 |
| `StringService.h` | 你 | 继承 `BnStringService`，声明 `upper`/`lower` |
| `StringService.cpp` | 你 | `upper`/`lower` 业务逻辑（零 Binder 痕迹） |
| `server/main.cpp` | 你 | `addService` + `joinThreadPool` |
| `client/main.cpp` | 你 | `waitForService` + `svc->upper("hello")` |
| `Android.bp` × 3 | 你 | 各 target 编译规则 |

### 你写的代码

```cpp
// StringService.h — 声明（继承 AIDL 生成的桩）
class StringService : public BnStringService {
public:
    String16 upper(const String16& input) override;
    String16 lower(const String16& input) override;
};

// StringService.cpp — 纯业务逻辑（零 Binder 痕迹）
String16 StringService::upper(const String16& input) {
    for (char16_t& c : buf)
        if (c >= 'a' && c <= 'z') c -= 32;
    return out;
}

// server/main.cpp — 注册服务 + 进线程池
ProcessState::self()->startThreadPool();
sp<StringService> svc = new StringService();
sm->addService(String16("com.example.binder.IStringService"), svc);
IPCThreadState::self()->joinThreadPool();

// client/main.cpp — 查服务 + 调用方法
ProcessState::self()->startThreadPool();
sp<IStringService> svc = waitForService<IStringService>(
        IStringService::descriptor);
String16 result = svc->upper(String16("hello"));
```

## 谁 include 什么

| | Server | Client | IStringService.cpp |
|--|--------|--------|--------------------|
| `IStringService.h` | ✅ 方法签名 | ✅ 接口类型 | ✅ |
| `BnStringService.h` | ✅ 继承用 | — | ✅ 拿事务码 |
| `BpStringService.h` | — | — | ✅ 唯一处 |

Server 通过 `BnStringService.h` 继承桩，Client 只通过 `IStringService.h` 拿到接口指针——`BpStringService` 的存在对两者完全透明。

## 调用链

```
Client: svc->upper("hello")
    ↓ 虚函数分派
BpStringService::upper()    ← IStringService.cpp
    data.writeString16(input)
    remote()->transact(UPPER, ...)
    ↓ BC_TRANSACTION → Kernel → BR_TRANSACTION
BnStringService::onTransact() ← IStringService.cpp
    data.readString16(&input)
    result = this->upper(input)  ← 虚函数分派
    ↓
StringService::upper()       ← StringService.cpp（你的业务逻辑）
    reply->writeString16(result)
    ↓ BC_REPLY → Kernel → BR_REPLY
BpStringService::upper() 续
    reply.readString16(&result)
    return result
```

## 相关代码

- 裸 ioctl 层 demo: `~/code/binder/{sm_binder,srv_binder,cli_binder}.cpp`
- BinderBuf/Parcel 封装: `~/code/binder/binder_buf.h`
- 面试 TOP10 详解: `~/code/binder/binder-interview-top10.md`
- Binder IPC 知识库: `~/.config/opencode/skills/camx-kb/entries/android-binder-ipc.md`
- AIDL 编译器 golden output: `~/code/aidl/tests/golden_output/`
