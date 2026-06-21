# RefBase 移植 — 从 AOSP 到 CMake 独立项目

## 问题背景
BufferQueueCore 依赖于 `utils/RefBase.h`（`class BufferQueueCore : public virtual RefBase`）和 `utils/StrongPointer.h`（所有 `sp<>` / `wp<>` 智能指针）。AOSP 本地只检出 `native/` 子树，RefBase 位于 `system/core/libutils/`，未包含。需要原样移植 RefBase 体系到独立 CMake 项目。

## 分析结论

### 1. 移植内容

从 AOSP 移植了以下核心文件：

| 文件 | 行数 | 内容 |
|------|------|------|
| `include/utils/RefBase.h` | ~140 | `RefBase` 类声明、`LightRefBase<T>` 模板、`weakref_type` 内部类 |
| `include/utils/StrongPointer.h` | ~550 | `sp<T>` 强指针模板、`wp<T>` 弱指针模板、全部构造函数/赋值/比较运算符 |
| `include/utils/TypeHelpers.h` | ~75 | 基本类型 traits（Vector 等容器依赖） |
| `src/utils/RefBase.cpp` | ~340 | `weakref_impl` 实现、incStrong/decStrong/incWeak/decWeak/attemptIncStrong |

### 2. 架构与关键设计

#### 2.1 RefBase 引用计数模型

每个 RefBase 对象在构造时创建 `weakref_impl` 对象，其中包含三个关键原子变量：

| 字段 | 类型 | 初始值 | 含义 |
|------|------|--------|------|
| `mStrong` | `std::atomic<int32_t>` | `INITIAL_STRONG_VALUE` (1<<28) | 强引用计数，首次 incStrong 后从哨兵值调整为 1 |
| `mWeak` | `std::atomic<int32_t>` | 0 | 所有引用计数（强+弱），每个 `sp<>` 贡献 1，每个 `wp<>` 贡献 1 |
| `mFlags` | `std::atomic<int32_t>` | 0 | 对象生命周期模式标志 |

`INITIAL_STRONG_VALUE` 哨兵值的作用：区分"从未有过强引用"和"强引用已归零"两种状态。这对于 `wp<>::promote()` 判断是否可以安全晋升至关重要。

#### 2.2 两种生命周期模式

| 模式 | 标志值 | 行为 |
|------|--------|------|
| `OBJECT_LIFETIME_STRONG` | 0（默认） | mStrong 归零时 `decStrong` 直接 `delete this` |
| `OBJECT_LIFETIME_WEAK` | 1 | mStrong 归零时不删除对象，仅在 mWeak 归零时由 `decWeak` 删除 |

#### 2.3 指针模板

**`sp<T>`（强指针）**：
- 构造/赋值时调用 `incStrong()`，析构时调用 `decStrong()`
- 每个 `sp<>` 同时贡献 mStrong+1 和 mWeak+1
- 支持 copy、move、polymorphic 转换（`sp<Derived>` → `sp<Base>`）

**`wp<T>`（弱指针）**：
- 构造/赋值时仅调用 `createWeak()` → `incWeak()`，析构时调用 `decWeak()`
- `promote()` 通过 `attemptIncStrong()` 尝试晋升为 `sp<>`，失败返回 nullptr
- 不阻止对象析构（OBJECT_LIFETIME_STRONG 模式）

#### 2.4 `LightRefBase<T>`（轻量级引用计数）

- 仅支持强引用，不支持 `wp<>`
- 无 `weakref_impl` 开销，仅在 T 内部维护一个 `std::atomic<int32_t> mCount`
- `decStrong` 归零时直接 `delete static_cast<const T*>(this)`
- 适用于简单场景（如 AOSP 的 `Fence` 类）

### 3. 移植中遇到的 Bug

#### Bug 1: `wp<>` 从 `sp<>` 赋值时双重 `incWeak`

**症状**：`wpDead = spObj` 后，对象销毁时引用计数残留，导致 `promote()` 访问已释放内存。

**根因**：`wp<T>::operator=(const sp<T>&)` 中先调用 `createWeak(this)`（内部已调用 `incWeak()`），然后又显式调用 `newRefs->incWeak(this)`，导致 mWeak 增量 ×2。

**修复**：移除显式的 `newRefs->incWeak(this)` 调用，依赖 `createWeak()` 的单次 `incWeak()`。

**影响文件**：`StrongPointer.h` 中 `operator=(const sp<T>&)` 和 `operator=(const sp<U>&)`。

#### Bug 2: `wp<>::promote()` 在 `OBJECT_LIFETIME_STRONG` 下访问已释放的 `mBase`

**症状**：对象已被 `decStrong` 删除后，`wpDead.promote()` 通过 `impl->mBase->onIncStrongAttempted()` 访问悬空指针，导致 segfault。

**根因**：`attemptIncStrong()` 中当 `mStrong <= 0` 时，未检查 `OBJECT_LIFETIME_STRONG` 标志就直接访问 `mBase`。

**修复**：在访问 `mBase` 前检查 `mFlags`，如果是 `OBJECT_LIFETIME_STRONG` 且 `mStrong <= 0`，直接返回 false（对象已不存在）。

**影响文件**：`RefBase.cpp` 中 `attemptIncStrong()`。

#### Bug 3: `RefBase` 析构函数在 `OBJECT_LIFETIME_WEAK` 下 double-free `mRefs`

**症状**：`decWeak` 调用 `delete impl->mBase` 触发 `RefBase::~RefBase()`，析构函数又调用 `delete mRefs`，然后 `decWeak` 继续执行 `delete impl`，导致 double-free。

**根因**：`decWeak` 已将 mWeak 递减至 0，然后调用 `delete impl->mBase`。析构函数读取 `mWeak == 0` 后尝试 `delete mRefs`（即 `delete impl`）。

**修复**：析构函数中增加 `OBJECT_LIFETIME_WEAK` 早期返回，让 `decWeak` 全权处理 `mRefs` 的清理。

**影响文件**：`RefBase.cpp` 中 `RefBase::~RefBase()`。

### 4. 测试覆盖

20 个测试用例全部通过：

| # | 测试 | 验证 |
|---|------|------|
| 1 | `test_sp_basic` | sp 基本创建/销毁，getStrongCount |
| 2 | `test_sp_copy` | sp 拷贝共享同一对象 |
| 3 | `test_sp_move` | sp 移动语义 |
| 4 | `test_sp_clear` | sp::clear() 释放引用 |
| 5 | `test_sp_null` | 空指针 sp 操作 |
| 6 | `test_wp_basic` | wp 创建/销毁，对象存活/死亡时的 promote |
| 7 | `test_wp_from_raw` | 从裸指针创建 wp 并 promote |
| 8 | `test_wp_copy` | wp 拷贝 |
| 9 | `test_onFirstRef` | onFirstRef 回调时机 |
| 10 | `test_onLastStrongRef` | onLastStrongRef 回调时机 |
| 11 | `test_lifetime_weak` | OBJECT_LIFETIME_WEAK 模式 |
| 12 | `test_light_ref_base` | LightRefBase 基本操作 |
| 13 | `test_virtual_refbase` | virtual RefBase 继承 |
| 14 | `test_thread_safety` | 8 线程并发 sp 操作 |
| 15 | `test_wp_thread_safety` | 8 线程并发 wp promote |
| 16 | `test_getStrongCount` | getStrongCount 随引用变化 |
| 17 | `test_sp_polymorphic` | 多态 sp 转换 |
| 18 | `test_force_inc` | force_set / forceIncStrong |
| 19 | `test_onLastWeakRef` | onLastWeakRef 回调（OBJECT_LIFETIME_WEAK） |
| 20 | `test_sp_comparison` | sp 相等/不等/Null 比较 |

### 5. 与 AOSP 原版的差异

| 项目 | AOSP 原版 | 本移植 |
|------|----------|--------|
| `extendObjectLifetime` | `protected` | `public`（方便测试调用） |
| DEBUG_REFS 引用追踪 | 完整实现（链表追踪每个 inc/dec 的调用者地址） | 空实现（no-op） |
| `RefBase::onFirstRef` | 在 `incStrong` 中从 `weakref_impl` 回调 `refs->mBase->onFirstRef()` | 相同 |
| `ALOG_ASSERT` | Android log 系统 | `assert()`（Release 下为 no-op） |
| `ALOGD` | Android log 系统 | `fprintf(stderr, ...)` |
| Threads.h / Mutex | Android 线程库 | 使用标准 `<atomic>` + `std::thread` |

## 关键代码位置

- `include/utils/RefBase.h:58-110` — `RefBase` 类声明（公开接口 + 保护接口）
- `include/utils/RefBase.h:139-167` — `LightRefBase<T>` 模板（轻量级引用计数）
- `include/utils/StrongPointer.h:38-87` — `sp<T>` 类声明
- `include/utils/StrongPointer.h:130-206` — `wp<T>` 类声明
- `include/utils/StrongPointer.h:214-310` — `sp<T>` 全部成员函数实现
- `include/utils/StrongPointer.h:312-487` — `wp<T>` 全部成员函数实现
- `include/utils/StrongPointer.h:489-545` — 自由比较运算符（`==`, `!=` for `sp<>`）
- `src/utils/RefBase.cpp:41-75` — `weakref_impl` 类定义（引用计数存储）
- `src/utils/RefBase.cpp:90-103` — `RefBase::~RefBase()` 析构函数（含生命周期处理）
- `src/utils/RefBase.cpp:122-142` — `RefBase::incStrong()` 首次引用检测 + onFirstRef
- `src/utils/RefBase.cpp:144-165` — `RefBase::decStrong()` 最后引用检测 + delete this
- `src/utils/RefBase.cpp:262-279` — `weakref_type::decWeak()` RECOMMENDED 归零时的生命周期分发
- `src/utils/RefBase.cpp:284-367` — `weakref_type::attemptIncStrong()` wp 晋升 + 生命周期检查
- `tests/RefBase_test.cpp:1` — 全部 20 个测试用例

## 项目结构

```
BufferQueue/
├── CMakeLists.txt                      # 顶层 CMake（C++17, 添加子目录）
├── include/utils/
│   ├── RefBase.h                       # RefBase + LightRefBase 声明
│   ├── StrongPointer.h                 # sp<> + wp<> 模板（header-only 实现）
│   └── TypeHelpers.h                   # 类型 trait 定义
├── src/utils/
│   └── RefBase.cpp                     # weakref_impl + 引用计数逻辑实现
├── tests/
│   ├── CMakeLists.txt                  # 测试构建文件
│   └── RefBase_test.cpp                # 20 个测试用例
└── build/                              # CMake 构建输出目录
```

## 编译

```bash
cd BufferQueue && mkdir -p build && cd build
cmake ..
make
./tests/RefBase_test    # 运行测试
ctest                   # 或通过 ctest
```

## 备注

- 本移植保持 AOSP API 完全兼容：`sp<>`, `wp<>`, `RefBase`, `LightRefBase`, `weakref_type` 接口与 AOSP 一致
- `RefBase` 析构函数为 `protected`，防止栈分配和在非 `sp<>` 上下文中直接 `delete`
- `LightRefBase<T>` 的 `decStrong` 直接 `delete this`，要求 T 的析构函数对 `LightRefBase<T>` 可见（public 或 friend）
- 线程安全：所有引用计数操作使用 `std::atomic` 的 `memory_order_release`/`acquire` 保证 happens-before
- 未移植 `wp<>::operator <` 等容器排序运算符（非必要），已存在的可以保留
