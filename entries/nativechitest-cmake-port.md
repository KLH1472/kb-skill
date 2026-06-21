# nativechitest CMake 移植分析

## 问题背景
用户缺乏 Android 编译环境和测试机器，需要将 nativechitest 从 Android.mk 移植到 CMakeLists.txt 构建，使其能在 Linux x86_64 上编译运行。相机 HAL 库 (camera.qcom.so) 也需要重写为 stub。

## 分析结论

### 移植架构

```
CAMX_SAIPAN_LA.UM.8.13.R1_cmake/
├── CMakeLists.txt                    # 顶层构建 (add_subdirectory)
├── stubs/                            # 14 个 Android 系统库 stub 头文件
│   ├── log/log.h                     # ALOGE/ALOGV 等日志宏
│   ├── hardware/camera3.h            # camera3类型 (stream, request, result)
│   ├── hardware/camera_common.h      # camera_module_t, camera_info_t
│   ├── hardware/gralloc.h            # GRALLOC_USAGE_* 标志
│   ├── hardware/gralloc1.h           # gralloc1 类型
│   ├── hardware/power.h              # power hints
│   ├── system/camera_metadata.h      # camera_metadata_t, tag/entry类型, API函数
│   ├── system/camera_vendor_tags.h   # vendor_tag_ops_t
│   ├── system/graphics.h             # HAL_PIXEL_FORMAT_*, dataspace
│   ├── system/window.h               # ANativeWindowBuffer_t
│   ├── ui/GraphicBuffer.h            # android::GraphicBuffer + USAGE_常量
│   ├── ui/GraphicBufferMapper.h      # android::GraphicBufferMapper
│   ├── sync/sync.h                   # sync_merge/sync_wait
│   ├── cutils/trace.h                # ATRACE_* 宏
│   ├── cutils/native_handle.h        # native_handle_t
│   ├── utils/StrongPointer.h         # android::sp<T> / wp<T>
│   ├── camera_metadata_hidden.h      # internal metadata types
│   ├── gralloc_priv.h                # PRIVATE_USAGE_*
│   ├── camera3.h                     # 便捷转发 <hardware/camera3.h>
│   └── string_utils.h               # g_strlcpy/g_strlcat
├── camera.qcom.so/                   # Stub HAL 共享库
│   ├── CMakeLists.txt
│   └── chi_stub.cpp                  # ChiEntry, HMI, 17个ChiOps mock实现
│       ├── pOpenContext/CloseContext  → 返回非空context handle
│       ├── pGetNumCameras/pGetCameraInfo → 硬编码1个假camera (5344x4016)
│       ├── pEnumerateSensorModes     → 返回3个sensor mode
│       ├── pCreateSession/pDestroySession → 返回非空session handle
│       ├── pActivatePipeline         → CDKResultSuccess
│       ├── pSubmitPipelineRequest    → 同步回调capture result + shutter message
│       ├── pFlushSession             → CDKResultSuccess
│       ├── pGetFenceOps/pTagOps/pMetadataOps/pGetBufferManagerOps → 填充空函数表
│       └── pGetSettings              → 返回null
└── nativechitest/                    # 测试可执行文件
    ├── CMakeLists.txt                # 31个cpp + 2个compat cpp
    ├── nativechitest/                # 19个test cpp (从源树复制)
    ├── nativetestutils/              # 8个util cpp
    ├── chiutils/                     # 4个chiutil cpp + log_compat + extension_stub
    └── string_compat.cpp             # g_strlcpy/g_strlcat实现
```

### 编译依赖策略

| 类别 | 方案 |
|------|------|
| CAMX API 头文件 (chi.h, camxcdktypes.h等) | 指向原源码树路径，不复制 |
| Android 系统库 | 全部 stub/mock 实现（最小化：只实现实际调用的函数） |
| camera.qcom.so | dlopen 加载的 stub 共享库，mock 1个假 camera |
| 编译定义 | `-DOS_ANDROID -DANDROID -DCAMX_ANDROID_API=28 -Dstrlcpy=g_strlcpy` |
| 运行时库路径 | 修改 chimodule.h 指向 ./lib/libcamera_qcom.so |

### 修复的源码问题

移植过程中发现并修复的问题：

1. **缺少 `#include <algorithm>`** — chimodule.cpp, chitestcase.cpp, chxmetadata.cpp 等使用了 `std::sort`, `std::find` 但未包含头文件
2. **缺少 `#include <cstring>`** — chxdebugprint.h 使用了 `strrchr`
3. **`#error Unsupported operation`** — cdkutils.h 需要 `ANDROID` 宏才能走 `clock_gettime` 路径
4. **`strlcpy` 不兼容** — 添加编译定义 `-Dstrlcpy=g_strlcpy` 和 compat 实现
5. **ExtensionModule/UsecaseSelector** — 提供 extension_stub.cpp 实现所需 stub 方法
6. **`camera_metadata_entry.data.i32[0]`** — 需要 `i32[1]` 数组形式（Android内部定义）
7. **`camera3.h` 直接引用** — mixed_pipeline_test.cpp 使用 `#include "camera3.h"` (不带路径)

### 运行结果

nativechitest 在 Linux x86_64 上成功编译并运行：

- **二进制大小**: 15.7MB (nativechitest) + 50KB (libcamera_qcom.so)
- **测试框架**: main() → RunNativeChiTest() → RunTests() 正常工作
- **ChiModule 初始化**: dlopen 成功加载 stub，枚举出1个假 camera (3 sensor modes)
- **Metadata测试**: TestCreateMetadata PASSED, TestSetTag PASSED（部分PASS，部分因 stub 返回空数据 FAIL）
- **BinaryCompatibility测试**: x86_64 与 aarch64 struct 大小差异导致 FAIL（预期行为）
- **Pipeline测试**: 由于 stub 返回空 metadata/result，部分测试可能 FAIL

## 关键代码位置

- `CAMX_SAIPAN_LA.UM.8.13.R1_cmake/CMakeLists.txt` — 顶层构建脚本
- `CAMX_SAIPAN_LA.UM.8.13.R1_cmake/camera.qcom.so/chi_stub.cpp` — Stub HAL (ChiEntry + 17 ChiOps)
- `CAMX_SAIPAN_LA.UM.8.13.R1_cmake/nativechitest/CMakeLists.txt` — 测试可执行文件构建
- `CAMX_SAIPAN_LA.UM.8.13.R1_cmake/stubs/system/camera_metadata.h` — camera_metadata API stub (最复杂的stub)
- `CAMX_SAIPAN_LA.UM.8.13.R1_cmake/stubs/ui/GraphicBuffer.h` — GraphicBuffer stub
- `CAMX_SAIPAN_LA.UM.8.13.R1_cmake/stubs/hardware/camera3.h` — camera3类型stub
- `CAMX_SAIPAN_LA.UM.8.13.R1_cmake/nativechitest/chiutils/extension_stub.cpp` — ExtensionModule/UsecaseSelector stub
- `CAMX_SAIPAN_LA.UM.8.13.R1_cmake/nativechitest/chiutils/log_compat.cpp` — ChiLog/g_enableChxLogs stub
- `CAMX_SAIPAN_LA.UM.8.13.R1_cmake/nativechitest/nativechitest/chimodule.h:61` — 库路径改为 `./lib/libcamera_qcom.so`

## 相关概念

- **ChiOps 函数指针表** — CHICONTEXTOPS 结构体，17个函数指针，通过 ChiEntry() 填充
- **dlopen/dlsym** — 运行时动态加载相机 HAL，无需编译时链接
- **CHICALLBACKS** — 回调结构体 (ChiProcessCaptureResult, ChiNotify, ChiProcessPartialCaptureResult)
- **camera_metadata_entry_t** — Android metadata条目类型，含 typed data union (i32/f/i64/d/r/u8)
- **Android.mk vs CMakeLists.txt** — 静态/动态库声明到 target_link_libraries 的映射
- **pSubmitPipelineRequest 回调** — stub 在函数内同步回调 capture result，测试框架的 ProcessCaptureResults 线程消费

## 备注

- 移植到 Linux 后，struct 大小与 ARM64 不同（x86_64 vs aarch64），BinaryCompatibilityTest 预期失败
- camera.qcom.so stub 可进一步完善：支持真实的 metadata 存储/查询逻辑，提高 metadata 测试通过率
- 需 root 权限才能挂载 `/vendor` 路径；通过修改 chimodule.h 的库路径绕过
- ImageRootPath 硬编码为 `/data/vendor/camera/nativechitest/`，需修改为本地路径才能 dump 图像
- `test_pipelines.h` 大量使用指定初始化器（designated initializers），GCC 默认支持
- chifeature2test 的移植可复用本项目的所有 stub 和构建策略
