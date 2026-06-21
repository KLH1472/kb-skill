# CamX Native Test 测试框架分析

## 问题背景
用户想通过测试代码学习 CAMX，需要理解 CHI-CDK 测试框架的架构、编译依赖、执行流程和断言机制。

## 分析结论

### 1. 测试程序概览

chi-cdk/test/ 下有三个独立的可执行测试程序：

| 可执行文件 | 源码目录 | 用途 |
|-----------|---------|------|
| `nativechitest` | `test/nativetest/nativechitest/` | **核心集成测试**（18 个 cpp），测试 session/pipeline/node/buffer/metadata/flush 等 |
| `chifeature2test` | `test/chifeature2testframework/` | Feature2 框架测试（DAG pipeline 编排） |
| `chiofflinepostproctest` | `test/chiofflinepostproctest/` | 离线后处理（JPEG 编码）测试 |

另有 `f2player` 是 Feature2 播放器（用于 bitra 处理）。

### 2. nativechitest 编译依赖

#### 直接编译进二进制的 .cpp（18 个）
列于 `nativechitest/common/build/android/Android.mk` 第 19~38 行：
```
binary_comp_test.cpp      camera_manager_test.cpp     camera_module_test.cpp
chibuffermanager.cpp      chimetadatautil.cpp         chimetadata_test.cpp
chimodule.cpp             chipipeline.cpp             chipipelineutils.cpp
chisession.cpp            chitestcase.cpp             chitests.cpp
custom_node_test.cpp      flush_test.cpp              mixed_pipeline_test.cpp
offline_pipeline_test.cpp pip_camera_test.cpp         recipe_test.cpp
realtime_pipeline_test.cpp
```

#### 静态链接库（预编译为 .a）
- **`libnativetestutils`**（8 个 cpp）：buffermanager, cmdlineparser, camera3stream, camera3metadata, commonutilslinux/win32, nativetestlog, nativetest
- **`libchiutils`**（4 个 cpp）：chxblmclient, chxutils, chxperf, chxmetadata

#### 动态链接库（Android 系统库）
```
libutils, libc++, libcutils, libhardware, libcamera_metadata,
liblog, libui, libhidlbase, libhidltransport, libqdMetaData, libsync
```

**关键：没有链接 camx 主框架的任何 .so。** camx HAL 通过 dlopen 在运行时动态加载。

### 3. 运行时如何连接 camx HAL

`chimodule.cpp` 是关键桥梁：

```cpp
// 1. 加载 camx HAL 动态库
hLibrary = dlopen("/vendor/lib64/hw/camera.qcom.so", RTLD_NOW);

// 2. 查找 CHI 入口符号
pChiHalOpen = reinterpret_cast<PFNCHIENTRY>(dlsym(hLibrary, "ChiEntry"));
m_pCameraModule = reinterpret_cast<camera_module_t*>(dlsym(hLibrary, "HMI"));

// 3. 调用 ChiEntry 填充 ChiOps 函数指针表
(*pChiHalOpen)(&m_chiOps);

// 4. 通过 ChiOps 调用所有 camx 功能
m_chiOps.pOpenContext()
m_chiOps.pCreateSession()
m_chiOps.pSubmitPipelineRequest()
// ...
```

调用链：
```
nativechitest → dlopen("camera.qcom.so") → dlsym("ChiEntry")
  → ChiEntry(&m_chiOps) → 通过函数指针表调用 camx HAL 全部能力
```

### 4. 测试框架架构

| 层 | 文件 | 职责 |
|----|------|------|
| 测试入口 | `chitests.cpp:315` | main() → RunNativeChiTest() → RunTests() |
| 测试基类 | `chitestcase.cpp` | Setup/Teardown，buffer 管理，result 处理线程，**含 NT_ASSERT/NT_EXPECT** |
| 工具类 | `chisession.cpp` / `chipipeline.cpp` / `chibuffermanager.cpp` | 纯 API 封装，**无断言**，通过 CDKResult 返回值报错 |
| 测试实现 | `chimetadata_test.cpp` / `realtime_pipeline_test.cpp` / `offline_pipeline_test.cpp` / etc. | 实际测试逻辑，**大量 NT_ASSERT/NT_EXPECT** |

`chisession.cpp` 不是测试用例本身——它是封装 CHI Session 创建/销毁/Flush 的工具类。

### 5. 断言宏体系

定义在 `nativetestutils/nativetest.h:165-182`：

```cpp
// 断言失败 → 打印错误 + return（终止当前测试）
#define NT_ASSERT(_condition, _errorfmt, ...)
    if (false == Check(GetTestObject(), _condition, __FILENAME__, __LINE__, #_condition, _errorfmt, ##__VA_ARGS__))
        return

// 断言失败 → 打印错误但继续执行（类似 gtest EXPECT）
#define NT_EXPECT(_condition, _errorfmt, ...)
    Check(GetTestObject(), _condition, __FILENAME__, __LINE__, #_condition, _errorfmt, ##__VA_ARGS__)

// 无条件失败
#define NT_FAIL(_errorfmt, ...)
    if (false == Check(this, false, __FILENAME__, __LINE__, "FAIL", _errorfmt, ##__VA_ARGS__))
        return
```

不是标准 C 的 `assert()`，而是自定义的 `NT_ASSERT`/`NT_EXPECT`。`Check()` 函数统计测试通过/失败计数。

### 6. 测试用例注册与执行

测试用例通过 `NATIVETEST_TEST` 宏注册：
```cpp
NATIVETEST_TEST(RealtimePipelineTest, TestIFEFull) { TestRealtimePipeline(TestIFEFull); }
NATIVETEST_TEST(ChiMetadataTest, TestCreateMetadata) { TestCreateMetadata(); }
```

执行链：
```
main() → RunNativeChiTest() → 解析命令行 → RunTests()
  → 逐个执行 NATIVETEST_TEST 注册的测试用例
```

`RunTests()` 是 `extern` 声明，实现在 `nativetestutils/nativetest.cpp`。

## 关键代码位置

- `chi-cdk/test/nativetest/nativechitest/chimodule.cpp:232` — `dlopen("camera.qcom.so")` 加载 camx HAL
- `chi-cdk/test/nativetest/nativechitest/chimodule.cpp:238` — `dlsym("ChiEntry")` 获取 CHI 入口
- `chi-cdk/test/nativetest/nativechitest/chimodule.cpp:273` — `(*pChiHalOpen)(&m_chiOps)` 填充函数指针表
- `chi-cdk/test/nativetest/nativechitest/chimodule.h:61` — 硬编码库路径 `/vendor/lib64/hw/camera.qcom.so`
- `chi-cdk/test/nativetest/nativechitest/chitests.cpp:315` — main() 入口
- `chi-cdk/test/nativetest/nativechitest/chitests.cpp:281` — `RunNativeChiTest()` 解析命令行并调用 RunTests()
- `chi-cdk/test/nativetest/nativetestutils/nativetest.h:165` — `NT_ASSERT` 宏定义
- `chi-cdk/test/nativetest/nativetestutils/nativetest.h:172` — `NT_EXPECT` 宏定义
- `chi-cdk/test/nativetest/nativechitest/common/build/android/Android.mk:19-38` — 18 个直接编译的 .cpp
- `chi-cdk/test/nativetest/nativechitest/common/build/android/Android.mk:93-94` — 静态链接 libnativetestutils + libchiutils
- `chi-cdk/test/nativetest/nativechitest/common/build/android/Android.mk:125` — LOCAL_MODULE := nativechitest
- `chi-cdk/test/nativetest/Android.mk:8-14` — NATIVETEST_SHARED_LIBRARIES 定义
- `chi-cdk/test/nativetest/nativetestutils/common/build/android/Android.mk:19-27` — libnativetestutils 的 8 个 cpp
- `chi-cdk/core/chiutils/common/build/android/Android.mk:25-29` — libchiutils 的 4 个 cpp

## 相关概念

- **CHI (Camera Hardware Interface)** — OEM 可扩展的相机框架层
- **ChiModule** — 单例，封装 dlopen/dlsym 和 ChiOps 函数指针管理
- **ChiOps** — 函数指针结构体，运行时代理所有 camx HAL 调用
- **ChiEntry** — camx HAL 库的导出函数，填充 ChiOps 表
- **NT_ASSERT vs NT_EXPECT** — 类似 gtest 的 ASSERT vs EXPECT：前者失败 return，后者失败继续
- **Realtime Pipeline vs Offline Pipeline** — 实时（sensor 输入）vs 离线（文件输入）测试
- **Testgen** — 测试数据生成模式（不依赖实际 sensor 硬件）

## 备注

- 测试框架本身与 camx HAL 通过 CHI API 接口完全解耦，因此没有编译环境也能阅读学习
- nativechitest 的编译需要 Android 设备上的 camx HAL 运行时才能执行
- `chimodule.h` 中库路径区分 32/64 位：`m_libPath = "/vendor/lib64/hw/camera.qcom.so"` 或 `"/vendor/lib/hw/camera.qcom.so"`
- `CAMX_C_LIBS` 在 `chi-cdk/core/build/android/common.mk:92` 中定义为空，说明 camx 主框架完全不参与 nativechitest 编译
