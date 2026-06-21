# Android 风格日志系统整改

> 类型：设计决策
> 置信度底线：本文档所有内容为 ✅已确认（全部经过编译+压测验证）

## ❓ 问题背景

CamX/CHI 源码在 Android 上通过 liblog.so 输出日志，移植到 x86 Linux 后需要 stub 实现。同时本项目自有代码（CamXAdapter、DummyNode 等）使用 fprintf，格式不统一，缺少时间戳/PID/TID/文件名/行号。

## 💡 设计原则

1. **不修改原始源码** — stub 通过 include path 替换 Android 头文件，源码调用不变
2. **stub 实现 `__android_log_print`** — 源码需要此符号，x86 无 liblog.so，stub 以宏形式在预处理期替换（非链接期）
3. **XLOG 独立命名** — 本项目专用宏，不与 ALOGE/CAMX_LOG/CHX_LOG 冲突，自带 file:line:func()
4. **接口解耦，输出归一** — 多种接口层宏，统一汇聚到 `__camx_log_emit` 输出

## 💡 三层架构

```
接口层（互不耦合）:
  XLOGE/XLOGI/...  — 本项目专用，带 [level][CORE   ] file:line func()
  ALOGE/ALOGI/...  — Android API stub（stubs/log/log.h、camx_stubs/utils/Log.h 提供）
  CAMX_LOG_*       — CamX 框架宏，经 Log::LogSystem -> __android_log_write
  CHX_LOG_*        — CHI 框架宏，经 ALOGE -> __android_log_print

适配层:
  __android_log_print(prio, tag, fmt, ...)  — printf 风格，snprintf + __camx_log_emit
  __android_log_write(prio, tag, msg)       — 预格式化字符串，直接 __camx_log_emit

输出层（统一）:
  __camx_log_emit(prio, tag, msg)  — 唯一出口，fprintf(stderr)
  格式: MM-DD HH:MM:SS.ffffff  PID  TID LEVEL TAG: msg
```

## 💡 XLOG 宏设计

定义在 `camx_stubs/android/log.h`：

```c
#define XLOG_FILENAME (strrchr(__FILE__, '/') ? strrchr(__FILE__, '/') + 1 : __FILE__)

#define XLOGE(fmt, ...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, \
    "[ERROR][CORE   ] %s:%d %s() " fmt, XLOG_FILENAME, __LINE__, __func__, ##__VA_ARGS__)
```

使用方式：在源文件顶部设 LOG_TAG 后 include：
```c
#undef  LOG_TAG
#define LOG_TAG "Adpt"    // ≤8字符，尽可能4字符
#include <android/log.h>
XLOGI("message %d", 42);
// 输出: 06-21 12:00:00.000000 1234 5678 I Adpt    : [ INFO][CORE   ] myfile.cpp:10 Func() message 42
```

ALOGE 不能加 file:line，因为 CHX_LOG_ERROR（chxdebugprint.h:183）已在 format string 中拼入 file:line，会重复。

## 💡 修改文件清单

### Stub 文件（定义层）

| 文件 | 改动 |
|------|------|
| `camx_stubs/android/log.h` | `__camx_log_emit` + `__android_log_print/write` + LOG_TAG + XLOG_FILENAME + XLOGE/W/I/D/V |
| `camx_stubs/utils/Log.h` | 简化为 `#include <android/log.h>` + LOG_ALWAYS_FATAL_IF |
| `stubs/log/log.h` | 保留原有 ALOGE 定义（被 camx_stubs 的先加载优先级遮蔽时跳过） |

### 构建配置

| 文件 | 改动 |
|------|------|
| `chifeature2test/CMakeLists.txt` | +1行 `camx_stubs` include path |

### 我们的代码（fprintf → XLOG）

| 文件 | LOG_TAG | 方式 |
|------|---------|------|
| `camx_runtime_stubs.cpp` | "Adpt" | fprintf → XLOGI/XLOGE |
| `dummy_node.cpp` | "Node" | fprintf → XLOGD |
| `chiframework_stubs.cpp` | "Stub" | fprintf → XLOGI/XLOGW |

### patched_srcs（CF2_LOG/CHX_LOG → XLOG）

两种迁移方式并存：

| 文件 | LOG_TAG | 方式 | 调用数 |
|------|---------|------|--------|
| `feature2offlinetest.cpp` | "FOfl" | sed 逐行替换 | 44 |
| `feature2testcase.cpp` | "FTst" | sed 逐行替换 | 33 |
| `feature2buffermanager.cpp` | "FBuf" | sed 逐行替换 | 21 |
| `chimetadatautil.cpp` | "Meta" | sed 逐行替换 | 10 |
| `chifeature2testmain.cpp` | "F2Mn" | sed 逐行替换 | 3 |
| `chimodule.cpp` | "ChiM" | sed 逐行替换 | 16 |
| `chithreadmanager.cpp` | "ThMg" | sed 逐行替换 | 30 |
| `genericbuffermanager.cpp` | "GBuf" | sed 逐行替换 | 19 |
| `chifeature2base.cpp` | "F2Bs" | sed + `#undef/#define CHX_LOG XLOGI` 重定向 | 278 |
| `chxmetadata.cpp` | "ChMd" | `#undef/#define` 宏重定向（7 组 CHX_LOG → XLOG） | 28 |

sed 替换规则：
```
CF2_LOG_ENTRY() → XLOGV("ENTRY")
CF2_LOG_EXIT()  → XLOGV("EXIT")
CF2_LOG_ERROR(  → XLOGE(
CF2_LOG_DEBUG(  → XLOGD(
CF2_LOG_WARN(   → XLOGW(
CF2_LOG_INFO(   → XLOGI(
CHX_LOG_ERROR(  → XLOGE(
CHX_LOG_VERBOSE(→ XLOGV(
```

宏重定向（用于有别名宏的复杂文件，如 chxmetadata.cpp）：
```c
#undef  CHX_LOG_INFO
#define CHX_LOG_INFO    XLOGI
// CHX_LOG_META_DBG 定义为 CHX_LOG_INFO → 自动走 XLOGI
```

## ⚠️ 注意事项

1. **LOG_TAG 不大于 8 字符，尽可能 4 字符** — `__camx_log_emit` 用 `%-8s` 格式化 tag，超过 8 字符破坏对齐
2. **XLOG 已内含 `__func__`** — 不要在 XLOGV("ENTRY %s", __func__) 中再传 __func__，直接 XLOGV("ENTRY")
3. **sed -i 后可能需要 touch** — make 的时间戳检测可能在同一秒内无法区分新旧，导致不重编
4. **git stash 不清理 untracked 文件** — 新建的文件（如 camx_stubs/log/log.h）stash 后仍存在，可能遮蔽同路径旧文件
5. **CF2_LOG_* 展开为双重输出**（CF2Log.Log + CHX_LOG）— 这是替换为 XLOG 的根因，消除重复

## ⚠️ 待完成项

1. **两种迁移方式未统一** — 8 个文件用 sed 逐行替换，2 个文件用宏重定向。风格不一致但功能等价，当前决定保持现状
2. **stubs/log/log.h 未清理** — 原计划移至 camx_stubs/log/log.h 并删除原文件，但原始源码中 10 处 `#include <log/log.h>` 仍需此文件。可考虑后续在 camx_stubs/ 下创建 log/log.h 重定向
3. **camx_stubs/utils/Log.h 中 LOG_ALWAYS_FATAL_IF 仍用 fprintf** — 低优先级，极少触发

## 📍 关键代码位置

- `camx_stubs/android/log.h` — XLOG 宏定义、__camx_log_emit、__android_log_print/write
- `stubs/log/log.h` — ALOGE/ALOGI 原始 stub（被源码 `#include <log/log.h>` 引用）
- `camx_stubs/utils/Log.h` — ALOGE/ALOGI stub（被 CamX 源码 `#include <utils/Log.h>` 引用）
- `camxdebugprint.h:206` — CAMX_LOG 宏定义（→ Log::LogSystem → __android_log_write）
- `chxdebugprint.h:178` — CHX_LOG_ERROR 宏定义（→ ALOGE + ChiLog::LogSystem）
- `chxdebugprint.h:183` — CHX_LOG_ERROR 内部 ALOGE 调用（已拼入 file:line，不可在 ALOGE 层再加）
- `chifeature2log.h:41-67` — CF2_LOG_* 宏定义（双重输出：CF2Log.Log + CHX_LOG_*）
- `camxosutilslinuxembedded.cpp:470` — OsUtils::LogSystem（→ __android_log_write）

## 📝 Git 提交记录

```
08b4897 migrate all patched_srcs to XLOG — sed replacement + CHX_LOG macro redirect
6473261 align XLOG format with CamX source — tag='CamX', add [level][CORE   ] prefix + LOG_TAG ≤4 chars
3163915 migrate feature2testcase and feature2buffermanager to XLOG
ad6c60c add __func__ to XLOG macros
3aac2e3 replace CF2_LOG/CHX_LOG with XLOG in feature2offlinetest.cpp
5be3ceb unify project logging to XLOG macros — fprintf → XLOGI/XLOGE/XLOGD/XLOGW
d5900da add XLOG macros with file:line for project-local logging
7ef5bcf add Android logcat-style formatting to __android_log_write/print stubs — timestamp/PID/TID
```

合并 diff 保存在 `/tmp/opencode/logging_combined.diff`（4517 行，cd5fe87..HEAD）。
