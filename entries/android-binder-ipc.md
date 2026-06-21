# Binder IPC 分层架构 — Demo → AOSP 源码映射

## 问题背景
通过编写 `triple2_*.cpp`（裸 ioctl 层）和 `sm_binder/srv_binder/cli_binder`（BBinder/BpBinder 层），完整实现了 Binder IPC 的三层调用栈。本文档以 demo 代码为入口，逐层映射到 AOSP `native/libs/binder/` 真实源码，标注行号、运行时变量值、设计意图。

## 一、分层架构全景

```
AOSP 层次                          Demo 文件              内核交互
────────────────────────────────────────────────────────────────────
IInterface / BnXxx / BpXxx        binder_class.h          (纯 C++ dispatch)
IBinder / BBinder / BpBinder      binder_class.h          通过 tr.cookie 找到 BBinder
IPCThreadState / ProcessState     binder_class.cpp         裸 ioctl(fd, WRITE_READ)
Parcel (序列化)                    binder_buf.h            生成 flat_binder_object
────────────────────────────────────────────────────────────────────
binder driver                     /dev/binderfs/triple     内核 binder.c
```

## 二、ProcessState — 进程级 binder 连接

### 职责
打开 `/dev/binderfs/<name>`，mmap 映射 1MB 接收缓冲区。进程级单例。

### Demo 代码
```cpp
// binder_class.cpp:10
ProcessState::ProcessState() {
    mFD = open("/dev/binderfs/triple", O_RDWR|O_CLOEXEC);
    mVM = mmap(NULL, 1024*1024, PROT_READ, MAP_PRIVATE, mFD, 0);
}
```

### AOSP 映射
| Demo | AOSP | 行号 | 差异 |
|------|------|------|------|
| `open("/dev/binderfs/triple")` | `open_driver(driver)` | `ProcessState.cpp:558` | AOSP 额外做 `BINDER_VERSION`、`BINDER_SET_MAX_THREADS`、spam detection 三个 ioctl |
| `mmap(1MB)` | `mmap(BINDER_VM_SIZE)` | `ProcessState.cpp:611` | AOSP: `BINDER_VM_SIZE = 1MB - 2*PAGE_SIZE`，用 `MAP_NORESERVE` |
| `static ProcessState gProc` | `sp<ProcessState> gProcess` | `ProcessState.cpp:67` | AOSP 用 `sp<>` 智能指针，我们是裸指针 |

### 运行时值 (server 进程)
```
fd=3            ← open() 返回的 fd
mVM=0x5caf40a5d000  ← mmap 起始地址
```

## 三、IPCThreadState — ioctl 核心调度器

### 职责
封装所有 `ioctl(fd, BINDER_WRITE_READ)` 调用。负责：
- **发送**：`transact()` → `writeTransactionData()` + `waitForResponse()` → `talkWithDriver()`
- **接收**：`joinThreadPool()` → `talkWithDriver()` + `executeCommand()`
- **回复**：`sendReply()` → `writeTransactionData(BC_REPLY)` + `talkWithDriver()`

### 核心数据结构
```cpp
// binder_class.h:83
class IPCThreadState {
    Parcel mIn;     // 读缓冲区 (BR_ 命令)
    Parcel mOut;    // 写缓冲区 (BC_ 命令)
    BBinder *mContextObj;  // 上下文管理器 (handle 0 投递目标)
};
```

### 3.1 talkWithDriver — 唯一 ioctl 入口

```
Demo (binder_class.cpp:29)              AOSP (IPCThreadState.cpp:1165)
──────────────────────────────────────────────────────────────────
bwr.write_size   = mOut.cursor;         同
bwr.write_buffer  = mOut.data;          同
bwr.read_size    = {0, sizeof(mIn)};    同
bwr.read_buffer  = mIn.data;            同
ioctl(fd, BINDER_WRITE_READ, &bwr);     同
mOut.cursor = 0;  ← 写入后清零          mOut.setDataSize(0);  (AOSP 等价)
mIn.cursor  = bwr.read_consumed;        同
```

**运行时 trace** (server 注册阶段)：
```
ioctl: write_size=68 read_size=4096
  → write consumed=68 (BC_TRANSACTION 全部发送)
  → read  consumed=48 (BR_TRANSACTION_COMPLETE 8B + BR_REPLY 8B + txn 64B + data 4B...)
```

### 3.2 transact — 发送 BC_TRANSACTION

```
Demo (binder_class.cpp:87)              AOSP (IPCThreadState.cpp:854)
──────────────────────────────────────────────────────────────────
memcpy(mOut, bc_txn_t{cmd, txn}, 68B)   mOut.writeInt32(BC_TRANSACTION)
                                         mOut.write(&tr, sizeof(tr))
return waitForResponse(reply);           return waitForResponse(reply)
```

两版逻辑一致：把 `[BC_TRANSACTION, binder_transaction_data]` 写入 mOut，然后等回复。

### 3.3 waitForResponse — 等待 BR_REPLY

```
Demo (binder_class.cpp:48)                 AOSP (IPCThreadState.cpp:1060)
────────────────────────────────────────────────────────────────────
while (1) {                               while (1) {
    talkWithDriver(true);                     talkWithDriver();
    while (readPos < cursor) {                cmd = mIn.readInt32();
        cmd = readInt32();                    switch (cmd) {
        switch (cmd) {                        case BR_REPLY:    → read txn → ipcSetDataReference → goto finish
        case BR_REPLY:    → read txn → copy  case BR_DEAD_REPLY:  → DEAD_OBJECT
        case BR_DEAD:     → return -1         case BR_FAILED_REPLY:→ FAILED_TRANSACTION
        case BR_TRANSACTION_COMPLETE: break;  case BR_TRANSACTION_COMPLETE:
        case BR_RELEASE/DECREFS: skip 16B          continue;
        default: executeCommand(cmd);         default: executeCommand(cmd);
    }}}                                       }}}
```

**关键差异**：
- Demo 用 `memcpy` 把驱动 buffer 拷到 Parcel（一次拷贝），AOSP 用 `ipcSetDataReference` 直接引用驱动 buffer（零拷贝）
- Demo 必须手动跳过 `BR_RELEASE`/`BR_DECREFS` 的 16 字节 payload，否则读指针错位

### 3.4 executeCommand — 分发 BR_TRANSACTION

```
Demo (binder_class.cpp:105)              AOSP (IPCThreadState.cpp:1407)
──────────────────────────────────────────────────────────────────
case BR_TRANSACTION:                      case BR_TRANSACTION:
  memcpy(&tr, mIn..., sizeof(tr));          mIn.read(&tr, sizeof(tr));
  // copy driver buffer to Parcel            buffer.ipcSetDataReference(...); ← 零拷贝!
  data.setData(tmp);
                                             // AOSP 从 flat_binder_object 提取 cookie
  BBinder *target = tr.target.ptr            if (tr.target.ptr) {
    ? (BBinder*)tr.cookie   ← 本地对象      reinterpret_cast<BBinder*>(tr.cookie)
    : mContextObj;          ← handle 0       ->transact(tr.code, buffer, &reply, tr.flags);
                                             } else {
  target->onTransact(...);                     the_context_object->transact(...);
  if (!oneway) sendReply(reply);             }
```

**运行时 trace** (SM 收到 server 的注册请求)：
```
BR_TRANSACTION: code=1, target.ptr=0x0, cookie=0x0, flags=0
  → target.ptr=0 → 走 mContextObj (ServiceManager)
  → ServiceManager::onTransact(code=1 "CMD_REGISTER", data=[UpperService...][fbo])
     → fbo->handle=1 (内核翻译后的值)
     → SM 存储 "UpperService" → handle 1
  → sendReply(reply): BC_REPLY{data=[uint32_t 1]}
```

**运行时 trace** (Server 收到 client 的 upper 请求)：
```
BR_TRANSACTION: code=100, target.ptr=0x5caf40a5e020, cookie=0x5caf40a5e020
  → target.ptr≠0 → 走 cookie (BBinder* = BnStringService 实例)
  → BnStringService::onTransact(code=100 "UPPER", data="Hello Binder")
     → 转换大写 → "HELLO BINDER"
  → sendReply(reply): BC_REPLY{data="HELLO BINDER\0"}
```

### 3.5 joinThreadPool — 服务端主循环

```
Demo (binder_class.cpp:138)              AOSP (IPCThreadState.cpp:778)
──────────────────────────────────────────────────────────────────
mOut ← BC_ENTER_LOOPER                    mOut.writeInt32(BC_ENTER_LOOPER)
talkWithDriver(false);                    // 在 do-while 第一轮自动 flush
while (1) {                               do {
    talkWithDriver(true);                     result = getAndExecuteCommand();
    while (readPos < cursor) {                // 内部: talkWithDriver + executeCommand
        executeCommand(readInt32());          processPendingDerefs();
    }                                     } while (result != -ECONNREFUSED);
}                                         mOut.writeInt32(BC_EXIT_LOOPER);
```

## 四、Parcel / BinderBuf — 序列化

### 职责
管理数据缓冲区 + flat_binder_object 偏移数组。写模式：append 到本地 buffer。读模式：从驱动 buffer 解包。

### Demo 实现
```cpp
// binder_buf.h:7 — 极简版
struct BinderBuf {
    uint8_t  data[4096];       // 数据负载
    uint64_t offsets[64];      // flat_binder_object 偏移数组
    int      n_offsets;        // offset 数量
    int      cursor;           // 写指针
};
```

### AOSP 映射
| Demo | AOSP | 行号 | 说明 |
|------|------|------|------|
| `write_binder(ptr)` | `flattenBinder()` → `writeObject()` | `Parcel.cpp:253, 1775` | `binder=ptr`, `cookie=ptr` → kernel 翻译为 HANDLE |
| `write_translated_handle(h)` | 同上，BINDER_TYPE_HANDLE 分支 | `Parcel.cpp:296` | 发送已知 handle 让 kernel 翻译给接收方 |
| `writeString(s)` | `writeString16()` | `Parcel.cpp` | Demo 用原始 char*，AOSP 用 UTF-16 |
| 读回复 | `readStrongBinder()` → `unflattenBinder()` | `Parcel.cpp:2341, 344` | BINDER_TYPE_BINDER → 从 cookie 重建 BBinder；BINDER_TYPE_HANDLE → `getStrongProxyForHandle()` |
| `ipcSetDataReference()` | 同名 | `Parcel.cpp:2726` | 零拷贝包装驱动 buffer |

### 运行时数据布局 (server 注册 SM)
```
BinderBuf 内容:
  data[0..13]:  "UpperService\0"
  data[14..15]: [padding]
  data[16..39]: flat_binder_object {
    hdr.type   = BINDER_TYPE_BINDER (0x73622a42)
    flags      = 0
    binder     = 0x5caf40a5e020  ← BnStringService 实例地址
    cookie     = 0x5caf40a5e020  ← 同上 (kernel 存到 binder_node)
  }
  cursor       = 40
  offsets[0]   = 16
  n_offsets    = 1
```

### 内核翻译后 (SM 收到的数据)
```
data[16..39]: flat_binder_object {
  hdr.type   = BINDER_TYPE_HANDLE (0x73682a42)  ← 内核改了
  flags      = 0
  binder     = 0                                    ← 内核清零
  handle     = 1                                    ← 内核分配的 handle
  cookie     = 0                                    ← 内核清零 (cookie 存在 binder_node)
}
```

## 五、IBinder / BBinder / BpBinder — 对象模型

### 职责
- **IBinder**：抽象接口，提供 `localBinder()` / `handle()` 查询
- **BBinder**：服务端对象。`onTransact()` 被子类覆写。内核在 BR_TRANSACTION 的 `tr.cookie` 中传递其指针
- **BpBinder**：客户端代理。持有 handle 号，`transact()` 发送 BC_TRANSACTION

### Demo 实现
```cpp
// binder_class.h:43-63
class IBinder {
public:
    virtual BBinder* localBinder() { return nullptr; }
    virtual int32_t  handle()     { return 0; }
};

class BBinder : public IBinder {
public:
    BBinder* localBinder() override { return this; }
    virtual int32_t onTransact(uint32_t code, const Parcel &data, Parcel *reply) { return 0; }
};

class BpBinder : public IBinder {
public:
    int32_t mHandle;
    BpBinder(int32_t h) : mHandle(h) {}
    int32_t handle() override { return mHandle; }
    int32_t transact(uint32_t code, const Parcel &data, Parcel *reply);
};
```

### AOSP 映射
| Demo | AOSP | 行号 |
|------|------|------|
| `IBinder` | `IBinder` (抽象类，带引用计数) | `include/binder/IBinder.h:58` |
| `BBinder` | `BBinder` (继承 `IBinder` + `RefBase`) | `include/binder/Binder.h:31` |
| `BBinder::onTransact()` | `BBinder::onTransact()` (virtual) | `Binder.cpp:767` |
| `BBinder::transact()` | `BBinder::transact()` (final, 拦截内置码) | `Binder.cpp:374` |
| `BpBinder` | `BpBinder` (继承 `IBinder` + `RefBase`) | `include/binder/BpBinder.h:43` |
| `BpBinder::transact()` | `BpBinder::transact()` → `IPCThreadState::self()->transact()` | `BpBinder.cpp:402` |

**关键差异**：AOSP 的 `BBinder::transact()` 是 `final`——它拦截 `PING_TRANSACTION` 等内置码，只有 `default` 才调 `onTransact()`。Demo 直接暴露 `onTransact()`，没有内置码拦截。

### 运行时对象关系 (server 进程)
```
BnStringService (extends BBinder)
  ↓ 地址 0x5caf40a5e020
  ↓ addService() 写入 flat_binder_object{binder=cookie=0x5caf40a5e020}
  ↓ kernel 在 SM 进程中创建 binder_node{ptr=cookie=0x5caf40a5e020}
  ↓
client 查询 SM → getService 返回 handle=1
  ↓ BpStringService(handle=1)
  ↓ transact(code=UPPER, data="Hello Binder")
  ↓ kernel 查找 handle=1 → binder_node → 投递 BR_TRANSACTION 到 server
  ↓ tr.cookie=0x5caf40a5e020 (BBinder*)
  ↓ executeCommand: target = (BBinder*)tr.cookie → onTransact(code=100, ...)
```

## 六、BpServiceManager — 服务发现

### 职责
对 handle 0（上下文管理器）封装 `getService(name)` 和 `addService(name, BBinder*)`。

### Demo 实现
```cpp
// binder_class.cpp:182
int32_t BpServiceManager::getService(const char *name) {
    Parcel data, reply;
    data.writeString(name);
    IPCThreadState::self()->transact(0, CMD_GET_SERVICE, data, &reply);
    // reply 中有 flat_binder_object，内核已翻译 handle
    struct flat_binder_object *fbo = (void*)(reply.data + *reply.offsets);
    return fbo->handle;
}

void BpServiceManager::addService(const char *name, BBinder *service) {
    Parcel data;
    data.writeString(name);
    data.write_binder((binder_uintptr_t)service);  // BINDER_TYPE_BINDER
    IPCThreadState::self()->transact(0, CMD_REGISTER, data, nullptr);
}
```

### 运行时值 (server addService)
```
transact(handle=0, code=1, data={name="UpperService", fbo{type=BINDER, ptr=BBinder*}})
→ kernel 路由到 SM (handle 0 = context manager)
→ SM 收到 BR_TRANSACTION: code=1, target.ptr=0, cookie=0 → mContextObj
→ ServiceManager::onTransact(code=1):
    fbo 已被 kernel 翻译: type=HANDLE, handle=1
    SM 存储: g_services[0] = {"UpperService", 1}
    reply.writeInt32(1)   ← 回复 server "注册成功，你在 SM 空间的 handle=1"
→ server 收到 BR_REPLY: data=[4 bytes: 1]
```

## 七、IInterface / BnXxx / BpXxx — 接口模式

### 职责
定义业务接口，自动生成服务端桩 (Bn) 和客户端代理 (Bp)。Demo 简化了 AIDL 编译器做的事情。

### Demo 实现
```cpp
// binder_class.h:112-130
class IStringService {
public:
    static const uint32_t UPPER = 100;
    static const uint32_t LOWER = 101;
};

class BnStringService : public BBinder {
public:
    int32_t onTransact(uint32_t code, const Parcel &data, Parcel *reply) override {
        // code 100 → upper, code 101 → lower
    }
};

class BpStringService : public BpBinder {
public:
    BpStringService(int32_t handle) : BpBinder(handle) {}
    const char* upper(const char *s) {
        Parcel data, reply;
        data.writeString(s);
        transact(UPPER, data, &reply);
        return (const char*)reply.data;
    }
};
```

### AOSP 映射
AOSP 用 AIDL 编译器从 `.aidl` 文件生成三个类：
```
IStringService.aidl
  → IStringService.java/cpp     (接口声明)
  → BnStringService.cpp         (服务端桩: onTransact 里 switch(code))
  → BpStringService.cpp         (客户端代理: upper() 里 transact())
```
Demo 手动实现了等价逻辑，省去了 AIDL 编译步骤。

## 八、flat_binder_object — 跨进程对象翻译

### 翻译方向
```
Server (BINDER_TYPE_BINDER)         kernel                     SM (BINDER_TYPE_HANDLE)
  fbo.binder = BBinder*    ──→  binder_new_node(proc, fbo)  →  fbo.handle = ref->desc (1,2,3…)
  fbo.cookie = BBinder*    ──→  node->cookie = BBinder*     →  fbo.cookie = 0 (已存 kernel)
                                 binder_inc_ref_for_node()
                                 fbo.type = BINDER_TYPE_HANDLE
                                 fbo.handle = ref->desc

SM (BINDER_TYPE_HANDLE)           kernel                     Client (BINDER_TYPE_HANDLE)
  fbo.handle = 1          ──→  binder_get_node_from_ref()  →  fbo.handle = client-ref->desc
                                 binder_inc_ref_for_node()    (kernel 翻译成 client 空间的 handle)
```

### 内核源码
| 操作 | 函数 | 位置 |
|------|------|------|
| BINDER_TYPE_BINDER → 目标进程建 ref | `binder_translate_binder()` | `binder.c:2220` |
| BINDER_TYPE_HANDLE → 跨进程翻译 | `binder_translate_handle()` | `binder.c:2273` |
| offsets 遍历循环 | `binder_transaction()` | `binder.c:3459` |

## 九、引用计数 — BR_ACQUIRE/RELEASE/INCREFS/DECREFS

### 背景
`flat_binder_object` 传递时，内核为接收方创建 `binder_ref`（引用），需要发送方和接收方协调引用计数。AOSP 用 `RefBase`（强引用/弱引用）管理。

### Demo 处理
```cpp
// binder_class.cpp:119
case BR_RELEASE:
case BR_DECREFS:
    // binder_ptr_cookie = 16 bytes (ptr + cookie)
    mIn.setReadPos(mIn.readPos() + sizeof(binder_uintptr_t) * 2);
    break;
```
Demo **只消费 payload，不做实际引用计数**。对单线程、短生命周期 demo 足够。生产代码必须正确处理才能避免内存泄漏。

### AOSP 处理
`IPCThreadState.cpp:1327+` 的 `executeCommand` 中：
- `BR_ACQUIRE` → `incStrong()` → `BC_ACQUIRE_DONE`
- `BR_RELEASE` → `decStrong()` + `BC_FREE_BUFFER`
- `BR_INCREFS` → `incWeak()` → `BC_INCREFS_DONE`
- `BR_DECREFS` → `decWeak()` + `BC_FREE_BUFFER`

## 十、关键 Bug 记录

### 10.1 waitForResponse 缺少内层 while
**现象**：server 在 `addService` 后永远收不到 `BR_REPLY`  
**根因**：`BR_TRANSACTION_COMPLETE` 和 `BR_REPLY` 在同一 ioctl buffer。先读到 COMPLETE → `break` → 外层循环 → 下一次 `talkWithDriver` **覆盖 mIn** → `BR_REPLY` 丢失  
**修复**：加 `while (readPos < cursor)` 消费整个 buffer

### 10.2 mOut.cursor 未清零
**现象**：client 反复收到 `BR_FAILED_REPLY`  
**根因**：`talkWithDriver` 后设 `mOut.cursor = write_consumed`（=68），第二次 ioctl 又把同一个 BC_TRANSACTION 发出去  
**修复**：改为 `mOut.cursor = 0`

### 10.3 BR_RELEASE/BR_DECREFS payload 未消费
**现象**：SM 死循环打印 `executeCommand cmd=0x720c`  
**根因**：`flat_binder_object` 触发内核引用计数指令。这些指令带 16 字节 payload，`default: break` 不消费 → 读指针不前进 → 死循环  
**修复**：显式跳 16 字节

### 10.4 写入 driver mmap buffer (PROT_READ)
**现象**：segfault  
**根因**：`fbo->cookie = mContextObj` 写到 `tr.data.ptr.buffer`（PROT_READ 映射）  
**修复**：删除该行。数据应从 Parcel copy 中读，不写 driver buffer

## 十一、文件清单

| 文件 | 行数 | 角色 |
|------|------|------|
| `binder_class.h` | 133 | 类声明：Parcel, ProcessState, IPCThreadState, BBinder, BpBinder, BpServiceManager, IStringService/Bn/Bp |
| `binder_class.cpp` | 203 | 实现：ioctl 循环、transact、executeCommand、sendReply |
| `binder_buf.h` | 60 | BinderBuf：数据缓冲 + flat_binder_object 偏移管理 |
| `sm_binder.cpp` | 65 | ServiceManager extends BBinder，CMD_REGISTER/CMD_GET_SERVICE 分发 |
| `srv_binder.cpp` | 16 | 创建 BnStringService → addService → joinThreadPool |
| `cli_binder.cpp` | 22 | getService → BpStringService → upper/lower |

## 十二、AOSP 关键源码索引

| 文件 | 关键方法 | 行号 |
|------|----------|------|
| `IPCThreadState.cpp` | `transact()` | 854 |
| | `waitForResponse()` | 1060 |
| | `talkWithDriver()` | 1165 |
| | `writeTransactionData()` | 1284 |
| | `executeCommand()` (BR_TRANSACTION) | 1407 |
| | `sendReply()` | 1050 |
| | `joinThreadPool()` | 778 |
| `ProcessState.cpp` | `open_driver()` | 558 |
| | `ProcessState()` constructor (mmap) | 592 |
| | `getStrongProxyForHandle()` | 333 |
| `Binder.cpp` | `BBinder::transact()` (intercepts + calls onTransact) | 374 |
| | `BBinder::onTransact()` | 767 |
| `BpBinder.cpp` | `BpBinder::transact()` → IPCThreadState | 366 |
| `Parcel.cpp` | `flattenBinder()` (writeStrongBinder) | 253 |
| | `unflattenBinder()` (readStrongBinder) | 344 |
| | `writeObject()` | 1775 |
| | `ipcSetDataReference()` (zero-copy wrap) | 2726 |
| `binder.c` (kernel) | `binder_translate_binder()` | 2220 |
| | `binder_translate_handle()` | 2273 |
| | offsets 遍历循环 | 3459 |
| | `BR_NOOP` 头部注入 | 4739 |
| | `BR_INCREFS/ACQUIRE` 生成 | 4913/4918 |
| | `BR_TRANSACTION vs BR_REPLY` 判定 | 5031-5048 |
| | `binder_enqueue_deferred_thread_work_ilocked` | 462-468 |

---

## 补充（2026-06-10）：addService 日志 → 内核/用户态逐行映射

### 完整流程（6 行日志 → 7 个内核函数）

```
日志行           内核生成位置       用户态分发
─────────────────────────────────────────────────────────────
[A] BR_NOOP      binder.c:4740     executeCommand → break
[B] BR_INCREFS   binder.c:4913     executeCommand → skip 16B
[C] BR_ACQUIRE   binder.c:4918     executeCommand → skip 16B
[D] BR_COMPLETE  binder.c:4843     waitForResponse → break (switch only)
[E] SM LOG       —                 SM::onTransact (sm_binder.cpp:27)
[F] BR_NOOP      binder.c:4740     executeCommand → break
[G] BR_REPLY     binder.c:5047     waitForResponse → return 0
[H] Server LOG   —                 回到 addService → srv_binder.cpp:14
```

### 内核关键机制

**BR_NOOP 强制注入**：每次 `binder_thread_read` 入口，`*consumed == 0` 时无条件写入 4 字节 BR_NOOP（`binder.c:4739-4743`）。这不是"idle 轮询"——它是每次 read 的固定头部标记。

**BINDER_WORK_NODE 的生成**：`binder_translate_binder()`（`binder.c:2220`）处理 `flat_binder_object` 时，`binder_inc_ref_for_node()` → `binder_inc_node_nilocked()`（`binder.c:843-855`）。首次加 ref 时 `!node->has_strong_ref && !node->has_weak_ref`，将 `BINDER_WORK_NODE` 队的到发送线程的 `thread->todo`。后续 `binder_thread_read` 消费时，一个 BINDER_WORK_NODE 生成两条 BR_ 命令：BR_INCREFS（弱引用就绪） + BR_ACQUIRE（强引用就绪）。

**TRANSACTION_COMPLETE 延迟投递**：`binder.c:3774` 用 `binder_enqueue_deferred_thread_work_ilocked()` 而非 `binder_enqueue_thread_work_ilocked()`。区别：前者不设置 `process_todo` 标志（`binder.c:462-468`），但 work item 仍写入 `thread->todo` 链表。在**同一个 ioctl** 的 read 阶段，`binder_thread_read` 直接检查 `thread->todo` 链表（`binder.c:4789`），与 `process_todo` 无关——因此延迟投递在第一次 ioctl 就被消费了。`process_todo` 仅影响后续 ioctl 的休眠/唤醒决策。

**BR_TRANSACTION vs BR_REPLY 判定**（`binder.c:5031-5048`）：

| 条件 | cmd | target.ptr | cookie | 场景 |
|------|-----|------------|--------|------|
| `t->buffer->target_node != NULL` | BR_TRANSACTION | `node->ptr` | `node->cookie` | 新事务（SM 收到注册请求） |
| `t->buffer->target_node == NULL` | BR_REPLY | 0 | 0 | 回复（Server 收到 SM 的 BC_REPLY） |

**第 1 次 ioctl read buffer 布局**（bwr.read_consumed = 48）：

| 偏移 | 内容 | 内核行号 |
|------|------|----------|
| 0 | `[uint32] BR_NOOP` | binder.c:4740 |
| 4 | `[uint32] BR_INCREFS` | binder.c:4913 |
| 8 | `[16B] node_ptr + node_cookie` | payload |
| 24 | `[uint32] BR_ACQUIRE` | binder.c:4918 |
| 28 | `[16B] node_ptr + node_cookie` | payload |
| 44 | `[uint32] BR_TRANSACTION_COMPLETE` | binder.c:4843 |

**第 2 次 ioctl**：write_size=0，内核进入 `binder_thread_read` → BR_NOOP 头部 → `thread->todo` 为空 → `binder_wait_for_work` → **阻塞**。SM 的 `BC_REPLY` 到达后唤醒 → 投递 `BINDER_WORK_TRANSACTION`（reply） → 生成 `BR_REPLY + binder_transaction_data`。

### SM 侧并发处理

SM 进程的 `binder_thread_read` 收到 `BINDER_WORK_TRANSACTION` → `cmd = BR_TRANSACTION`（`binder.c:5043`，`trd->target.ptr=0` 因为是 context manager）→ 用户态 `executeCommand` → `target = mContextObj` → `ServiceManager::onTransact(code=1)` → 存储 `{"UpperService", handle=1}` → `sendReply` → `BC_REPLY` → 内核唤醒 Server 进程。
