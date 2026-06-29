# 项目入口文档 — CamX Feature2 离线测试 x86 移植

> 类型：设计决策
> 最后更新：2026-06-29
> Git HEAD：`03f14d4`（ImageSensorModuleData mock，原版 camxnode.cpp + camxpipeline.cpp）

---

## 1. 最初目标

将高通 CamX/CHI Camera HAL 中的 **Feature2 离线测试**（`chi-cdk/test/chifeature2testcase`）从 Android ARM 环境移植到 **x86 Linux（WSL2）**，使其能在无摄像头硬件、无 Android 系统的 PC 上编译运行。

**核心价值**：在 PC 上快速验证 Feature2 状态机逻辑、Pipeline 调度、Node 依赖链，无需刷机上板。

**最终形态**：一个 CMake 工程，链接 CamX Core 真实代码 + CSL Mock + DummyNode，运行 Feature2OfflineTest 的 5 个用例全部 PASS。

---

## 2. 当前进展

### 2.1 里程碑

| 阶段 | 状态 | 关键成果 |
|------|------|---------|
| Phase 1: 编译框架搭建 | ✅ 完成 | CMake 工程、stub 头文件、include path 替换 |
| Phase 2: CHI-CDK 层打通 | ✅ 完成 | chi_stub.cpp 转发到 CamX、Feature2Base 状态机跑通 |
| Phase 3: CamX Core 层打通 | ✅ 完成 | 真实 Pipeline/Session/Node/DRQ/MetaBuffer 链路端到端 |
| DummyNode 依赖演示 | ✅ 完成 | BPS fence→property→IPE 完整 DRQ 调度链 |
| 全量用例移植 | ✅ 完成 | 5 个用例 × 10 轮 = 50/50 稳定 |
| 系统审计 | ✅ 完成 | HIGH 全部修复，MEDIUM 记录在案 |
| 日志系统整改 | ✅ 完成 | XLOG 宏 + Android 格式统一输出 |
| **下一步：camxchisession.cpp patch 消除** | 🔜 | 最后一个 patched_src 的根因修复（Session 构造器 = default）|

### 2.2 五个测试用例

| 用例 | Pipeline 拓扑 | 状态 |
|------|-------------|------|
| TestBayerToYUV | BPS → IPE（ZSLSnapshotYUV） | ✅ PASS |
| TestBPS | BPS only | ✅ PASS |
| TestIPE | IPE only | ✅ PASS |
| TestYUVToJpeg | IPE → JPEG（ZSLSnapshotJPEG） | ✅ PASS |
| TestMultiStage | Stage1(BPS→IPE) → Stage2(IPE→JPEG) | ✅ PASS |

### 2.3 已完成的审计（KB: `phase3-systematic-audit`）

审计了 4 类文件共 ~1900 行：

| 文件 | HIGH 问题 | 修复状态 |
|------|----------|---------|
| chi_stub.cpp (~850行) | 5 | 修复4，1删除 |
| chiframework_stubs.cpp (~400行) | 4 | 全部修复 |
| camx_runtime_stubs.cpp (~650行) | 5 | 修复2，3降级 |
| patched_srcs (9个文件) | 1 | 已修复 |

### 2.4 残留 Stub / Workaround（KB: `phase3-workaround-inventory`）

**2026-06-29 更新：W1/W2/W4/W5/W7 已全部消除，W3 通过完整 ImageSensorModuleData mock 绕过 union 误读（pSensorModeInfo 非 NULL）。camxnode.cpp 和 camxpipeline.cpp 均使用原版。**

### 2.5 最终 patched_srcs 清单（3 个文件）

| 文件 | 目的 |
|------|------|
| `camxhwenvironment.cpp` | W2: GetCameraInfo dummy + W7: GetImageSensorModuleData dummy |
| `camxatomic.cpp` | 原始 typo 修复 |
| `camxchisession.cpp` | failed Init → CAMX_DELETE（Session 构造器不初始化 m_pFlushLock） |

---

## 3. 如何构建与压测

### 3.1 构建

```bash
cd build
cmake .. && make -j$(nproc) camera_qcom chifeature2test
```

产物：`build/lib/libcamera_qcom.so`，`build/chifeature2test/chifeature2test`

> 生成源文件（g_sensor/g_parser）使用显式文件列表 + `GENERATED` 属性，clean build 一步到位。
> 测试数据构建时自动生成，无需手动处理。

### 3.2 运行单个用例

```bash
cd build
./chifeature2test/chifeature2test -t Feature2OfflineTest.TestBayerToYUV -f 1
```

`-t` 指定用例名，`-f 1` 表示帧数。可选用例：
- `Feature2OfflineTest.TestBayerToYUV`
- `Feature2OfflineTest.TestBPS`
- `Feature2OfflineTest.TestIPE`
- `Feature2OfflineTest.TestYUVToJpeg`
- `Feature2OfflineTest.TestMultiStage`

### 3.3 压测（稳定性验证）

```bash
cd build
for i in $(seq 1 10); do
  echo "=== Run $i ==="
  ./chifeature2test/chifeature2test -f 1 2>&1 | tail -10
  echo "EXIT=$?"
done
```

**PASS 标准**：
- 每个用例输出 `[ PASS] TestXxx`
- 无 SEGV、ASAN 报错、hang
- 进程退出码 = 0

### 3.4 ASAN 构建（可选）

```bash
mkdir -p build_asan && cd build_asan
cmake .. -DCMAKE_CXX_FLAGS="-fsanitize=address -fno-omit-frame-pointer" \
         -DCMAKE_C_FLAGS="-fsanitize=address -fno-omit-frame-pointer" \
         -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address"
make -j$(nproc)
```

### 3.5 测试数据

构建时由 `add_custom_command` 自动生成，文件存在即跳过。首次 generate 约 ~90MB：

| 文件 | 大小 | 用途 |
|------|------|------|
| Bayer2Yuv_image_4656x3496_0.raw | ~20MB | TestBPS 输入 |
| bps-idealraw-input.raw | ~20MB | TestBayerToYUV / TestMultiStage 输入 |
| IPE_image_4656x3496_0.p010 | ~47MB | TestIPE 输入 |
| ipe-unittest-input_full_vga_colorbar-1.yuv | ~450KB | TestYUVToJpeg 输入 |
| B2Y_Metadata_0.bin | 4KB | 元数据 |

---

## 4. 代码结构

### 4.1 只读源码树（不可修改）

通过 git status 确认：工作区干净（无 modified），仅有 `build_asan/` 和 `compile_commands.json` 为 untracked。

原始 CamX/CHI 源码位于**同级目录**：

```
../CAMX_SAIPAN_LA.UM.8.13.R1/          ← 只读，CMakeLists.txt 中 CAMX_ROOT 指向此处
├── camx/src/core/                      ← CamX Core（Pipeline, Session, Node, DRQ, MetaBuffer...）
├── camx/src/csl/                       ← Camera Service Layer（被 csl_mock.cpp 替代）
├── camx/src/hwl/titan17x/             ← HW 层（vendor tags 来源）
├── chi-cdk/core/chiframework/         ← CHI 框架（ExtensionModule, ChiMetadata...）
├── chi-cdk/core/chifeature2/          ← Feature2 状态机（Base, Generic, RequestObject）
├── chi-cdk/test/chifeature2testcase/  ← 原始测试代码（被 patched_srcs 覆盖编译）
└── chi-cdk/api/                       ← CHI API 头文件
```

### 4.2 本项目工程（我们的代码）

```
CAMX_SAIPAN_LA.UM.8.13.R1_cmake/       ← 工作区根目录
├── CMakeLists.txt                      ← 顶层 CMake（定义 CAMX_ROOT、全局编译选项）
│
├── camera.qcom.so/                     ← CamX 运行时 + CSL Mock + DummyNode
│   ├── CMakeLists.txt                  ← 构建 libcamera.qcom.so（静态库）
│   ├── camx_runtime_stubs.cpp          ← CamX 运行时 stub（CSL jump table、静态设置、格式工具等）
│   ├── chi_stub.cpp                    ← CHI API 实现（ChiEntry → CamX 转发）
│   ├── csl_mock.cpp                    ← CSL Mock（fence 信号、设备获取、内存分配）
│   ├── dummy_node.cpp                  ← DummyNode（替代真实 BPS/IPE/JPEG HW node）
│   ├── hw_vendor_tags.cpp              ← 86 个 vendor tag section 定义
│   └── validate_metadata_tags.cpp      ← 编译期 metadata tag 校验
│
├── chifeature2test/                    ← Feature2 离线测试
│   ├── CMakeLists.txt                  ← 构建 chifeature2test 可执行文件
│   ├── patched_srcs/                   ← 源码补丁（覆盖 chi-cdk 原始文件编译）
│   │   │                                  chxmetadata.cpp 已统一→直接用原始源
│   │   ├── chifeature2base.cpp         ← Feature2 状态机核心（最大文件，278 处日志迁移）
│   │   ├── feature2offlinetest.cpp     ← 测试入口 + 5 个用例定义
│   │   ├── feature2testcase.cpp        ← 测试框架（PASS/FAIL 判定、状态机等待）
│   │   ├── feature2buffermanager.cpp   ← Buffer 管理
│   │   ├── genericbuffermanager.cpp    ← 通用 buffer 管理
│   │   ├── chifeature2testmain.cpp     ← main() 入口
│   │   ├── chimodule.cpp               ← CHI 模块初始化
│   │   ├── chithreadmanager.cpp        ← 线程管理
│   │   └── chimetadatautil.cpp         ← Metadata 工具
│   └── stubs/                          ← CHI 框架 stub
│       └── chiframework_stubs.cpp      ← Pipeline/Session/Fence 转发
│
├── camx_patched_srcs/                  ← CamX Core 源码补丁（3 个文件，最小改动）
│   ├── camxatomic.cpp                  ← 原始代码 typo 修复
│   ├── camxchisession.cpp              ← failed Init → CAMX_DELETE
│   └── camxhwenvironment.cpp           ← W2+W7 mock: GetCameraInfo + GetImageSensorModuleData
│
├── stubs/                              ← Android 系统头文件 stub（被 include path 替换）
│   ├── hardware/                       ← camera3.h, hardware.h
│   ├── system/                         ← camera_metadata*.h
│   ├── log/                            ← log.h（ALOGE/ALOGI stub）
│   ├── cutils/                         ← properties, trace
│   ├── ui/, sync/, utils/              ← GraphicBuffer, Fence, String8 等
│   └── camera_metadata_hidden.h, gralloc_priv.h, string_utils.h
│
├── camx_stubs/                         ← CamX 内部头文件 stub
│   ├── android/log.h                   ← XLOG 宏 + __android_log_print/write 实现
│   ├── utils/Log.h                     ← ALOGE stub（被 CamX #include <utils/Log.h> 引用）
│   ├── camxmem.h                       ← 内存分配 stub
│   ├── camxexternalsensor.h            ← 空 sensor stub
│   ├── camxifestripinginterface.h      ← IFE striping 空 stub
│   ├── camxstatsdebugdatatypes.h       ← Stats debug 空 stub
│   ├── ipdefs.h                        ← IP 定义空 stub
│   ├── qdMetaData.h                    ← QD metadata 空 stub
│   ├── tuningsetmanager.h              ← Tuning 空 stub
│   └── cutils/, utils/                 ← Android cutils/utils 子集
│
├── build/                              ← 构建输出
│   ├── chifeature2test/chifeature2test ← 可执行文件
│   └── testdata/                       ← 测试用 dummy 输入文件
│
└── tools/                              ← 辅助工具
```

### 4.3 关键入口点

| 入口 | 文件 | 说明 |
|------|------|------|
| main() | `chifeature2test/patched_srcs/chifeature2testmain.cpp` | 测试程序入口 |
| 用例定义 | `chifeature2test/patched_srcs/feature2offlinetest.cpp` | 5 个测试用例 + Pipeline descriptor |
| 测试框架 | `chifeature2test/patched_srcs/feature2testcase.cpp` | PASS/FAIL 判定、状态机等待循环 |
| Feature2 状态机 | `chifeature2test/patched_srcs/chifeature2base.cpp` | 核心状态机（RequestObject 生命周期） |
| CHI API 入口 | `camera.qcom.so/chi_stub.cpp` | ChiEntry → CamX 转发 |
| CSL Mock | `camera.qcom.so/csl_mock.cpp` | Fence、设备、内存的 mock 实现 |
| DummyNode | `camera.qcom.so/dummy_node.cpp` | 替代 BPS/IPE/JPEG 的 mock HW node |

---

## 5. KB 历史文档列表

按时间倒序，按主题分组：

### 日志系统
| ID | 标题 | 类型 |
|----|------|------|
| `android-log-system-redesign` | Android 风格日志系统整改 — XLOG 宏 + 三层架构 + CHX_LOG 统一 | 设计决策 |

### Feature2 架构学习
| ID | 标题 | 类型 |
|----|------|------|
| `feature2-why-feature2` | Feature2 解决了什么问题 — Pipeline 能力边界与 Feature2 价值 | 源码分析 |
| `feature2-fro-state-machine` | FRO 十状态状态机 — 以 TestBayerToYUV 为例的完整追踪 | 源码分析 |
| `feature2-testbayertoyuv-e2e` | TestBayerToYUV 端到端时序 — Feature2 完整请求生命周期 | 源码分析 |
| `feature2-onp-buffer-release` | ONP Buffer 释放机制 — 三标志位 + Graph 路由 + TBM 引用计数 | 源码分析 |
| `feature2-processrequest-pump` | ProcessRequest 泵模型 — 谁在推动 FRO 状态前进 | 源码分析 |
| `two-feature-graph-feasibility` | 两 Feature Graph 测试用例可行性评估 | 设计决策 |
| `feature2-b2y-state-by-state` | TestBayerToYUV 逐状态数据流 — 10 个 FRO 状态全追踪 | 源码分析 |

### Phase 3 核心（DummyNode E2E → 全量测试）
| ID | 标题 | 类型 |
|----|------|------|
| `dummynode-vs-camxnode-architecture` | DummyNode vs CamX Node 架构评估 | 源码分析 |
| `camxnode-patches-w2-w5-analysis` | camxnode.cpp / camxpipeline.cpp patches — W1-W7 根因与修复方案 | 源码分析 |
| `mpm-portability-risk-analysis` | MemPoolMgr (MPM) 移植风险系统调查 | 源码分析 |
| `cmake-generated-sources-pattern` | CMake 生成源文件构建模式 — GLOB vs 显式列表 | 配置规则 |
| `chimetadatamanager-three-impls` | ChiMetadataManager 三实现对比 | 源码分析 |
| `drq-dependency-mechanism` | DRQ 依赖机制 — 四种类型 + 三种注册模式 | 源码分析 |
| `drq-four-dependency-types` | DRQ 四种依赖类型设计原理 — 不可替代性与组合使用 | 源码分析 |
| `chifence-dependency-flow` | ChiFence 依赖机制深度分析 — DRQ 三路径 + 自依赖 demo | 源码分析 |
| `drq-bps-ipe-dependency-registration` | BPS/IPE DRQ 依赖注册 — seq=0/1 两阶段 + IsTagPresentInPublishList gate | 源码分析 |
| `drq-bps-dependency-satisfaction` | BPS 依赖满足 — Session 预 signal + DRQ fence skip → 零等待 | 源码分析 |
| `eis-chifence-usage` | EIS ChiFence 使用全景 — 唯一 Node 级真实用例 (EISv2/v3 + NCS) | 源码分析 |
| `eis-algorithm-principles` | EIS 电子防抖原理 — 陀螺仪数据流 + Margin/Warp + v2/v3 对比 | 源码分析 |
| `camx-node-catalog` | CamX 全量 Node 目录 — HWL/SWL/CHI 三层节点分类与功能 | 源码分析 |
| `phase3-systematic-audit` | Phase 3 系统审计结果 — 残留 stub/workaround 全量盘点 | 源码分析 |
| `phase3-workaround-inventory` | Phase 3 已知 Hack/Workaround 清单（含清理记录） | 配置规则 |
| `phase3-test-issues-analysis` | 阶段性测试问题全量分析 | 调试记录 |
| `pass-criteria-alignment` | TestBayerToYUV PASS 标准对齐调查 | 源码分析 |
| `batch-crash-investigation` | Batch Crash 调查 — CAMERA_METADATA_ENUM_VAL 宏错误 | 调试记录 |
| `tcache-heap-corruption-investigation` | tcache/heap corruption 深度调查 | 调试记录 |
| `prunable-variant-investigation` | PrunableVariant 裁剪机制 | 源码分析 |
| `feature2-test-porting-plan` | Feature2OfflineTest 全量用例移植计划 | 设计决策 |
| `testbayertoyuv-stub-alignment` | TestBayerToYUV 执行路径 — real vs stub 全量对照 | 源码分析 |

### Phase 2 / 架构分析
| ID | 标题 | 类型 |
|----|------|------|
| `step-c-chi-forwarding` | Step C — CHI 转发策略 | 设计决策 |
| `camx-feature2-csl-mock-plan` | CamX 状态机、测试框架全景与 CSL Mock 方案 | 源码分析 |
| `camx-core-header-probe` | Phase 3 可行性探测 — CamX Core 头文件编译 | 源码分析 |
| `chifeature2test-phase2-complete` | Phase 1-2 完成状态 — Phase 3 启动手册 | 源码分析 |
| `testbayertoyuv-port-analysis` | TestBayerToYUV 移植分析 — 完整调用链与 stub 策略 | 源码分析 |
| `chimetadata-feature2-ops-flow` | ChiMetadata 类与 Feature2 元数据操作全景 | 源码分析 |
| `custom-chinode-implementation` | 自定义 ChiNode 实现全景分析 | 源码分析 |
| `format-negotiation` | 格式协商流程 | 源码分析 |
| `nativechi-test-framework` | CamX Native Test 测试框架分析 | 源码分析 |
| `nativechitest-cmake-port` | nativechitest CMake 移植分析 | 源码分析 |

### 构建与迁移
| ID | 标题 | 类型 |
|----|------|------|
| `self-contained-migration-plan` | Self-Contained 迁移计划 — 精确拷贝 444 个外部依赖文件 | 设计决策 |
| `external-dependency-extraction-method` | 判断 CMake 项目外部依赖文件的方法 | 配置规则 |

### 基础知识（Android/Linux 子系统）
| ID | 标题 | 类型 |
|----|------|------|
| `android-binder-ipc` | Binder IPC 分层架构 | 源码分析 |
| `aidl-binder-demo` | AIDL Binder Demo | 源码分析 |
| `bufferqueue-core` | BufferQueueCore 架构与状态机 | 源码分析 |
| `refbase-port` | RefBase 移植 — AOSP → CMake | 源码分析 |
| `v4l2-architecture` | V4L2 深度分析 — API/DMA/状态机 | 源码分析 |
| `dma-buf-heaps` | DMA-BUF Heaps — 用户态 DMA Buffer 分配 | 源码分析 |
| `poll-syscall-examples` | poll() 系统调用示例 | 源码分析 |

---

## 6. DRQ 系统调查摘要

> 详细分析见 KB 条目：`drq-dependency-mechanism`、`drq-four-dependency-types`、`chifence-dependency-flow`、`eis-chifence-usage`、`drq-bps-ipe-dependency-registration`、`drq-bps-dependency-satisfaction`

### 6.1 四种 DRQ 依赖类型 — 不可替代性

| 类型 | 等什么 | 信号者 | 为什么不可替代 |
|------|--------|--------|---------------|
| **Property** | 元数据就绪 | 进程内 Node | 内核/外部服务无法调 WriteDataList |
| **CSL Buffer Fence** | Buffer DMA 完成 | 内核 DMA 驱动 | 硬件事件无法触发 MetadataPool |
| **ChiFence** | 外部异步数据就绪 | 外部服务（NCS） | 外部服务不是 Node，CSL fence 绑定输出端口 |
| **IO Buffer Availability** | 不等待 | 框架自身 | 懒分配触发器，非等待机制 |

### 6.2 已完成的深度调查

- **CSL Buffer Fence**：per-output-port × per-request ring buffer 存储；上游→下游指针传递；DRQ 指针匹配机制
- **Property**：WriteDataList 无发布者 ACL；MetadataPool pub/sub 链；GetUnpublishedList 过滤
- **ChiFence**：DRQ 三条路径（Node级/ChiContext级/输入端口）；EISv2/v3 自依赖模式；NCS TriggerClientFence 信号链
- **IO Buffer Availability**：非等待机制；bindIOBuffers flag → BindInputOutputBuffers → late binding 分配
- **EPR 执行顺序**：PostJob 拓扑序发出，EPR 在线程池并行执行（非确定性）

### 6.3 BPS 依赖满足 — Session 预 signal [2026-06-29]

**测试场景**：TestBayerToYUV 离线管道 ZSLSnapshotYUVHAL（BPS+IPE，无 Sensor node，无预览管道）

**BPS seq=0 声明依赖**：
- property: **0 个**（`IsRealTime()=FALSE` → `sensorConnected=FALSE` → SetDependencies 外层跳过）
- fence: 1 个（`SetInputBuffersReadyDependency`，late binding，默认开启）

**fence 预 signal 机制**：
1. `Session::SetupRequestData()` 中 `CSLFenceSignal(hInternalCSLFence, ...)` — 在 `Pipeline::ProcessRequest()` 之前
2. `Node::SetupRequestInputPorts()` 记录 `pIsFenceSignaled = TRUE`
3. DRQ `AddDeferredNode` 检查 `CamxAtomicLoadU(pIsFenceSignaled) == 1` → fence 被 SKIP
4. `fenceCount=0, propertyCount=0` → BPS 入 ready queue → **零等待**

**结论**：BPS 在离线管道中实际不等待任何依赖，fence 在 Pipeline 处理前已被 Session 层 signal。

### 6.4 IPE 对 BPS 的 property 依赖 — IsTagPresentInPublishList gate

IPE 通过 `IsTagPresentInPublishList(PropertyIDBPSGammaOutput)` 检查 BPS 是否注册了 publish 来决定是否声明 property 依赖。BPS 通过 `QueryMetadataPublishList()` 注册可发布的 tags。

### 6.5 下一步方向

- **DRQ 调度细节**：DispatchReadyNodes 的优先级、批处理、背压机制

---

## 7. Feature2 教学计划

目标：系统学习 Feature2 架构，为面试准备。每章对应 1 个 KB 条目 + 可面试复述的"故事版本"。

| # | 核心问题 | 状态 | KB 条目 |
|---|---------|------|---------|
| 1 | Feature2 解决了什么问题？ | ✅ 完成 | `feature2-why-feature2` |
| 2 | FRO 十状态：每个状态在等什么？ | ✅ 完成 | `feature2-fro-state-machine` |
| — | ONP buffer 释放机制深度调查 | ✅ 完成 | `feature2-onp-buffer-release` |
| 3 | ProcessRequest 泵模型：谁在推动状态前进？ | ✅ 完成 | `feature2-processrequest-pump` |
| — | 两 Feature Graph 测试可行性评估 | ✅ 完成 | `two-feature-graph-feasibility` |
| 4 | Feature Graph：多 Feature 如何协作？ | 待开始 | — |
| 5 | Stages & Sequences：多帧合成怎么做？ | 待开始 | — |
| 6 | 面试能说什么？Feature2 的设计模式 | 待开始 | — |

已验证事项：
- FRO 状态转换日志 5/5 用例全部吻合（g_enableChxLogs=11 开启 CHX_LOG_INFO）
- URO-FRO 数量关系：FRO = 参与处理的 Feature 实例数 × URO 数
- 原始测试框架 ChiFeature2TestBase 是非功能性骨架（代码全部 #if 0），我们的 Feature2TestCase 是独立完整实现

注意事项：
- git stash 中有一个 Signal 修复（feature2testcase.cpp ProcessMessage 后 Signal 条件变量），评估后认为无修复必要（FRO 状态转换本身正常，500ms 延迟仅是测试框架轮询延迟）
- TestBayerToYUV 端到端时序详见 `feature2-testbayertoyuv-e2e`

---

## 8. 新 Session 启动清单

```bash
# 1. 加载 KB skill
# 2. 读取本文档: entries/project-onboarding.md
# 3. 按需加载 Feature2 KB 条目:
#    feature2-fro-state-machine (十状态 + 日志验证 + URO-FRO 关系)
#    feature2-onp-buffer-release (ONP 三标志位 + Graph 路由)
# 4. 确认 git 状态:
cd ~/code/CAMX_SAIPAN_LA.UM.8.13.R1_cmake
git log --oneline -5
git status --short
git stash list
# 5. 确认构建和测试:
cd build && cmake .. && make -j$(nproc) camera_qcom chifeature2test && ./chifeature2test/chifeature2test -f 1 2>&1 | grep -E "PASS|FAIL|Final"
```

---

## 9. 代码提交流程

**每笔 commit 前必须通过 clean build + 全量压测。** 耗时 ~80s（17s build + 64s test）。

```bash
# 1. 修改代码

# 2. clean build
rm -rf build
cmake -S . -B build
cmake --build build --target camera_qcom chifeature2test -j$(nproc)

# 3. 全量压测（10 轮 × 5 用例）
#    cd build：库按相对 CWD 路径加载 ./lib/libcamera_qcom.so（chxutils.cpp:773 LibMap）
cd build
for i in $(seq 1 10); do
  result=$(./chifeature2test/chifeature2test -f 1 2>&1 | grep 'Final Report')
  echo "Round $i: $result"
  echo "$result" | grep -q '0 FAILED' || { echo "FAIL"; exit 1; }
done
cd ..

# 4. code review
git diff

# 5. commit
git add -A && git commit -m "..."
```
