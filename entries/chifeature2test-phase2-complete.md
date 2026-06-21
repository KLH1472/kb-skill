# chifeature2test Phase 1-2 完成状态 — Phase 3 启动手册

> 类型：源码分析
> 置信度底线：本文档最低置信度为 ❓推测 的内容不可作为行动依据

## 问题背景
将 chifeature2test 的 TestBayerToYUV 用例从 Android.mk 移植到 CMake/Linux x86_64。Phase 1-2 已完成：Feature2 框架在 Linux 上运行，输出 24MB YUV 图像。下一步是 Phase 3：构建真实 CamX Core + CSL mock + DummyNode。

## 当前状态总结

### 测试结果
```
$ timeout 10 ./chifeature2test -t Feature2OfflineTest.TestBayerToYUV -f 1
[ INFO] Running test: Feature2OfflineTest.TestBayerToYUV
[ INFO] Image size to be saved is: 24668160
[ INFO] Saving image to file: ./testdata/output/TestBayerToYUV_W4656_H3496_0.yuv
[ PASS] Feature2 request completed successfully (state=Complete)
```

Feature2 状态机完整流程：Initialized → InputResourcePending → InputResourcePendingScheduled → ReadyToExecute → Executing → OutputNotificationPending → **Complete** (state=9)。图像保存成功，测试 PASS。

### 二进制信息
- chifeature2test: 49MB ELF x86-64
- libcamera_qcom.so: ~530KB (chi_stub)
- 输出 YUV: 24MB (4656x3496)

---

## 项目文件清单

### cmake 工程目录结构
```
CAMX_SAIPAN_LA.UM.8.13.R1_cmake/
  CMakeLists.txt                          # 顶层构建 (add_subdirectory)
  
  camera.qcom.so/
    CMakeLists.txt                        # chi_stub 共享库构建
    chi_stub.cpp                          # CHI ops mock: ChiEntry, 17个ChiOps, 
                                          #   buffer/fence/metadata ops, CamxMemAlloc
  
  chifeature2test/
    CMakeLists.txt                        # 主测试构建 (46个cpp)
    patched_srcs/                         # 原始源码的修改副本（不修改原始树）
      chifeature2testmain.cpp             # 移除了 RealTime/MFXR 测试注册
      chimetadatautil.cpp                 # 路径头文件搜索修正
      chimodule.cpp                       # 路径头文件搜索修正
      chithreadmanager.cpp                # std::deque placement new (portability fix)
      feature2buffermanager.cpp           # 路径头文件搜索修正
      feature2testcase.cpp                # +OutputErrorNotificationPending 处理, +超时保护
      genericbuffermanager.cpp            # 路径头文件搜索修正
    stubs/
      chiframework_stubs.cpp              # 384行：剩余 stub (Pipeline/Session/ExtensionModule等)
      chiframework_types.h                # 类型定义 (force-include)
      chxextensionmodule.h                # ExtensionModule stub 类声明
      chxpipeline.h                       # Pipeline stub 类声明
      chxsession.h                        # Session stub 类声明 (有 m_hSession 成员)
      chxusecase.h                        # Usecase stub
      chxusecaseutils.h                   # CHIBufferManager, UsecaseSelector, LightweightDoublyLinkedList
      chimodule.h                         # 库路径改为 ./lib/libcamera_qcom.so
      genericbuffermanager.h              # 路径覆盖 #ifdef CAMX_TEST_INPUT_PATH
      chimetadatautil.h                   # 路径覆盖 #ifdef CAMX_TEST_INPUT_PATH
      feature2buffermanager.h             # include 链修正
      xmllib_parser.h / xmllib_stub.cpp   # XML 解析 stub
      (其他头文件 stub)
  
  stubs/                                  # 共享 Android 系统库 stub（nativechitest + chifeature2test 共用）
    utils/RefBase.h, RefBase.cpp, StrongPointer.h  # 真实 AOSP RefBase
    ui/GraphicBuffer.h                              # LightRefBase + native_handle_t
    system/camera_metadata.h, window.h, graphics.h  # Android 系统类型
    hardware/camera3.h, camera_common.h, gralloc.h  # HAL 类型
    cutils/native_handle.h, trace.h                 # cutils
    log/log.h                                        # 日志重定向
  
  nativechitest/                          # 已移植的 nativechitest（Phase 1 产物）
    chiutils/
      chxutils.cpp                        # ← chifeature2test 编译此文件（真实 ChxUtils）
      chxperf.cpp                         # ← chifeature2test 编译此文件
      chxblmclient.cpp                    # ← chifeature2test 编译此文件
      chxmetadata.cpp                     # ← chifeature2test 编译此文件（真实 ChiMetadata/Manager）
      log_compat.cpp                      # 日志兼容层
    camera_metadata_port.cpp              # camera_metadata_t 实现
    camera_metadata_tag_info.cpp          # tag 信息表
    string_compat.cpp                     # strlcpy/strlcat
```

### 原始源码树 (CAMX_SAIPAN_LA.UM.8.13.R1) — 零修改
```
chi-cdk/test/chifeature2testframework/   # 测试框架源码（部分文件复制到 patched_srcs）
chi-cdk/test/chifeature2test/            # 测试入口
chi-cdk/core/chifeature2/                # Feature2 核心（直接编译，不修改）
chi-cdk/core/chiutils/                   # CHI 工具类（通过 nativechitest 的副本编译）
chi-cdk/core/lib/common/g_pipelines.h    # 生成的 pipeline 拓扑数据
chi-cdk/oem/qcom/feature2/              # OEM Feature2 描述符和实现
chi-cdk/api/                             # CHI API 头文件
camx/src/                                # CamX 核心（Phase 3 要编译的）
```

---

## 架构：真实 vs Stub

### 真实实现 [✅已确认]
| 组件 | 来源文件 | 方法数 |
|------|---------|--------|
| RefBase / sp<> / wp<> | stubs/utils/RefBase.cpp | ~30 |
| ChiMetadata | nativechitest/chiutils/chxmetadata.cpp | ~50 |
| ChiMetadataManager | nativechitest/chiutils/chxmetadata.cpp | ~95 |
| ChxUtils (全部) | nativechitest/chiutils/chxutils.cpp | ~143 |
| Mutex (pthread) | nativechitest/chiutils/chxutils.cpp | 5 |
| Condition (pthread_cond) | nativechitest/chiutils/chxutils.cpp | 5 |
| Semaphore | nativechitest/chiutils/chxutils.cpp | 5 |
| ChxPerf | nativechitest/chiutils/chxperf.cpp | ~10 |
| ChxBLMClient | nativechitest/chiutils/chxblmclient.cpp | ~5 |
| CHIBufferManager | chifeature2test/stubs/chxusecaseutils.h (inline, 用 g_chiBufferManagerOps) | ~15 |
| LightweightDoublyLinkedList | chifeature2test/stubs/chxusecaseutils.h (inline) | 8 |
| GraphicBuffer | stubs/ui/GraphicBuffer.h (LightRefBase, native_handle_t) | ~15 |

### 仍为 Stub [✅已确认]（Phase 3 替换目标）
| 组件 | 文件 | 用途 | Phase 3 替换方式 |
|------|------|------|-----------------|
| **ExtensionModule** | chiframework_stubs.cpp | CHI ops 网关，转发到 chi_stub | 用真实 CHI Override 层 |
| **Pipeline** | chiframework_stubs.cpp | CHI pipeline 包装 | 用真实 CamX Pipeline |
| **Session** | chiframework_stubs.cpp | CHI session 包装（半真实：Create/Destroy 转发到 chi_stub） | 用真实 CamX Session |
| **Usecase** | chiframework_stubs.cpp | Usecase 管理 | 不需要（测试不用） |
| **Feature** | chiframework_stubs.cpp | vtable anchor | 保留 |
| **OfflineLogger** | chiframework_stubs.cpp | 日志 | 保留 |
| **UsecaseSelector** | chxusecaseutils.h | CloneUsecase/PruneUsecase（已实现） | 保留 |

### 全局 Ops 表（连接 Feature2 ↔ chi_stub）
```
chiframework_stubs.cpp:
  CHIBUFFERMANAGEROPS g_chiBufferManagerOps  ← chi_stub.cpp ChiGetBufferManagerOps 填充
  CHIFENCEOPS         g_chiFenceOps          ← chi_stub.cpp ChiGetFenceOps 填充
  CHIMETADATAOPS      g_chiMetadataOps       ← chi_stub.cpp ChiGetMetadataOps 填充

  InitializeGlobalOps() 在 ExtensionModule::GetInstance() 首次调用时执行
  通过 ChiModule::GetInstance()->GetChiOps() 获取 ops
```

### Session 回调链（SubmitRequest → 结果回调）
```
Feature2 ProcessSubmitRequestMessage
  → ExtensionModule::SubmitRequest(pRequest)
    → ChiModule::GetChiOps()->pSubmitPipelineRequest(context, pRequest)
      → chi_stub.cpp ChiSubmitPipelineRequest
        → session->callbacks.ChiProcessCaptureResult(result, pPrivData)
          → Feature2Base::ProcessResultCallbackFromDriver
            → ProcessResult → ProcessBufferCallback
              → state → OutputNotificationPending
```

---

## 构建系统关键配置

### 编译定义
```cmake
_LINUX OS_LINUX OS_ANDROID ANDROID CAMX_ANDROID_API=28
strlcpy=g_strlcpy strlcat=g_strlcat
FEATURE_XMLLIB INVALID_INDEX=0xFFFFFFFF
CAMX_TEST_INPUT_PATH="./testdata/"
CAMX_TEST_OUTPUT_PATH="./testdata/output/"
```

### 编译选项
```cmake
-include ${CMAKE_CURRENT_SOURCE_DIR}/stubs/chiframework_types.h  # force-include
ENABLE_EXPORTS ON                                                 # dlopen 需要
```

### Include 搜索顺序（关键：stubs FIRST）
```
1. chifeature2test/stubs/         ← 优先级最高，stub 头文件在这
2. chi-cdk/test/chifeature2testframework/
3. chi-cdk/test/chifeature2test/
4. chi-cdk/core/chifeature2/
5. chi-cdk/core/chiframework/
6. chi-cdk/core/chiutils/
7. chi-cdk/api/common/
8. ... (其他 API/OEM/ext 路径)
9. stubs/                          ← 共享 Android 系统库 stub
10. nativechitest/chiutils/
```

### Patched 文件策略
原始源码树 (CAMX_SAIPAN_LA.UM.8.13.R1) **零修改**。需要修改的文件复制到 `patched_srcs/` 或 `stubs/`：
- **patched_srcs/*.cpp** — 编译时替代原始 .cpp（CMakeLists.txt 指向 patched 版本）
- **stubs/*.h** — 通过 include 搜索顺序覆盖原始头文件（gcc `#include "..."` 先搜同目录，stubs/ 通过 `-I` 搜索优先于原始目录）

---

## 已知问题

### 1. State 7 (OutputNotificationPending) 卡住 — ✅ 已修复
**根因**：chi_stub.cpp 的 `StubMetaGetPrivateData` 始终返回 NULL，而非存储的 private data（`ChiMetadata*`指针）。导致 `ChiMetadataManager::GetMetadataFromHandle()` 无法找到 metadata 对象 → `OnMetadataResult` 收到 NULL pMetadata → metadata port (YUV_Metadata_Out) 永远不被 notified → `AreOutputsReleased()` 永远 FALSE。

**修复**：chi_stub.cpp 一行改动：
```cpp
// 修复前：
*ppPrivateData = nullptr;
// 修复后：
StubMetadata* m = reinterpret_cast<StubMetadata*>(hMetaHandle);
*ppPrivateData = m ? m->pPrivateData : nullptr;
```

**结果**：测试状态机完整运行到 Complete (state=9)，输出 PASS。

### 2. 测试清理阶段析构挂起（仍存在）
**原因**：Feature2/ThreadManager/Session 析构器等待工作线程退出，但 chi_stub 环境下线程未正确初始化。进程在 PASS 后挂起直到 timeout 杀死。不影响测试正确性。

### 3. 测试数据为 dummy
- Bayer2Yuv_image_4656x3496_0.raw: 20MB 全零
- B2Y_Metadata_0.bin: 0 字节
- 统计文件 (.tint/.awbbg/.bhist): 0 字节
- 输出 YUV 内容无意义（全零输入 → 全零输出）

---

## Phase 3 计划：DummyNode 方案

### 目标架构
```
chifeature2test binary
  Feature2 框架 (REAL)
  ExtensionModule → CHI Override → CamX HAL  ← 需要实现
  CamX Core (REAL): Session, Pipeline, Topology, DRQ, Node base  ← ~90 文件
  DummyBPS / DummyIPE (STUB)  ← 立即 signal output fence
  CSL mock (~18 函数)  ← malloc/fake handle/signal fence
```

### CSL mock 需要的函数 [✅已确认]
| 函数 | Mock 行为 |
|------|----------|
| CSLInitialize | 返回 success |
| CSLOpen / CSLClose | fake session handle |
| CSLAcquireDevice / CSLReleaseDevice | fake device handle |
| CSLAlloc / CSLReleaseBuffer | malloc/free + fake handle |
| CSLSubmit | **异步** signal output fence（独立线程，避免死锁） |
| CSLStreamOn / CSLStreamOff | no-op |
| CSLLink / CSLUnlink | no-op |
| CSLOpenRequest | success |
| CSLCreatePrivateFence / CSLReleaseFence | fake fence + 回调注册 |
| CSLFenceAsyncWait | 保存回调，Submit 时触发 |
| CSLQueryCameraPlatform | fake platform info |
| CSLEnumerateDevices | fake device list |
| CSLRegisterMessageHandler | 保存回调 |

### DummyNode 行为
```cpp
class DummyNode : public Node {
    ExecuteProcessRequest(pData) {
        // 不做任何 IQ 处理
        // 对每个 output port，signal 其 fence
        for (auto& port : outputPorts) {
            CSLFenceSignal(port.fence, CamxResultSuccess);  // 异步
        }
    }
};
```

### CamX Core 最小编译集（预估）
- camx/src/core/: ~62 文件 (Session, Pipeline, Topology, Node, DRQ, HwContext, MetadataPool 等)
- camx/src/utils/: ~17 文件 (threading, atomic, memory)
- camx/src/osutils/: ~3 文件 (OS abstraction)
- camx/src/csl/: 替换为 mock_csl.cpp
- camx/src/hwl/titan17x/: ~5 文件 (HwFactory, platform context)
- 总计: ~90 文件

### 关键依赖链 [✅已确认]
```
Pipeline::ProcessRequest → Node::ProcessRequest → ExecuteProcessRequest (DummyNode override)
  → signal output fence → CSLFenceCallback → ProcessFenceCallback → ProcessRequestIdDone
  → Pipeline::NotifyNodeRequestIdDone → Session callback → Feature2 ProcessResultCallback
```

---

## 关键经验教训

### 1. 不合理的 Stub 模式
不涉及硬件的类（ChiMetadata, ChxUtils, Mutex, Condition, Semaphore, RefBase）应直接编译真实实现，而不是写空操作 stub。Stub 导致了：
- LightweightDoublyLinkedList 全空操作 → TBM 链条完全断裂
- ChiMetadataManager 用 operator new+memset 绕过构造 → 堆损坏
- Mutex/Condition 空操作 → 线程无法通信，状态机卡住
- sp<> 无引用计数 → 生命周期管理缺失

### 2. --allow-multiple-definition 是拐杖
隐藏了 stub 和真实实现的符号冲突。正确做法：每个符号只定义一次，要么 stub 要么真实。

### 3. gcc `#include "..."` 搜索顺序
`#include "file.h"` 先搜源文件所在目录，再搜 `-I` 路径。如果要覆盖原始头文件，需要把 .cpp 文件也复制到不同目录，使 `-I` 搜索能优先找到 stub 版本。

### 4. Memset 对含非 POD 成员的结构体
CamX 代码中有 `Memset(&struct_with_deque, 0, sizeof(...))` 的用法。在 bionic (Android) 上碰巧工作，但 libstdc++ (Linux) 上 std::deque 内部指针被清零会崩溃。修复：Memset 后用 placement new 重新构造 STL 容器。

---

## Git 提交历史 (12 commits + pending)
```
[pending] fix StubMetaGetPrivateData — test reaches Complete state
19dd4b4 replace ChxUtils/Mutex/Condition/Semaphore stubs with real implementations
baa5705 real Mutex/Condition, state machine loop fixes
fcd38dd remove --allow-multiple-definition, use real ChiMetadataManager
67760dd replace ChiMetadata stubs with real implementation
c89be83 fix Pipeline/Session GetSensorModeInfo
b97aeab integrate real RefBase, move patches to cmake tree
363bdf5 fix linked list, buffer allocation, GraphicBuffer handle
131b556 fix ChiMetadataManager::Get to return valid metadata
802cabe Phase 2 — connect Feature2 to chi_stub.cpp
5bfc1d8 fix Feature2 initialization stubs
1fd6451 fix compilation and runtime initialization
92de363 feature2 (initial chifeature2test CMakeLists.txt)
```
