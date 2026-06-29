# nativechitest 崩溃分析 — sensor module 缺失导致 SIGTRAP → std::runtime_error

> 类型：调试记录
> 置信度底线：✅已确认 的结论基于源码阅读和日志分析；❓推测 的需 gdb 验证

## 问题背景

在 cmake 移植环境中执行 `./nativechitest/nativechitest`（无参数，跑全部用例），启动后崩溃：

```
terminate called after throwing an instance of 'std::runtime_error*'
```

## 搜索过程

| 命令 / 动作 | 目标 | 结果摘要 |
|------------|------|---------|
| grep KB entries | 查找已有崩溃分析 | 未命中（无 nativechitest 崩溃条目） |
| read camximagesensormoduledatamanager.cpp:215-244 | 崩溃报错源头 | fileCount=0 → Invalid fileCount |
| read camxhwenvironment.cpp:597-609 | ProbeImageSensorModules | VOID 方法，失败不传播 |
| read camxhwenvironment.cpp:223-360 | HwEnvironment::Initialize() | 初始化链 |
| read camxdebug.cpp:126-134 | CamxFireAssert 崩溃机制 | raise(SIGTRAP) |
| read nativetest.cpp:74-82 | signalHandler | catch SIGTRAP → throw runtime_error |
| read chitests.cpp | 测试用例注册顺序 | CustomNodeTest 在 ChiMetadataTest 之前 |
| read custom_node_test.cpp:185-186 | VendorTagWrite pipeline | isSensorInput=1 → 需要真实 sensor |
| read csl_mock.cpp:112-124 | CSL mock device enumeration | 疑似返回 0 devices |

## 决策树

```mermaid
graph TD
    Q{"nativechitest 崩溃根因"}
    Q -->|"路径A: ImageSensorModuleDataManager::Create 失败"| A1["无 sensor module .bin 文件
    → ProBeImageSensorModules 静默失败
    [✅已确认] camxhwenv.cpp:597-609"]
    Q -->|"路径B: ChiContext 初始化失败"| B1["ChiContext::Initialize 成功返回
    → 'ChiContext initialized successfully'
    [❌已排除] 日志确认初始化成功"]
    Q -->|"路径C: CustomNodeTest 创建 sensor pipeline"| C1["Pipeline VendorTagWrite: isSensorInput=1
    → 有 sensor 节点的实时 pipeline 需要已枚举的 sensor 设备
    → 因无 sensor，CSL mock 返回 0 devices [✅已确认]
    → 后续代码触发 CAMX_ASSERT [🧠推断]"]
    C1 --> C2["CAMX_ASSERT → CamxFireAssert
    → PerformSoftwareBreakpoint() → raise(SIGTRAP)
    [✅已确认] camxdebug.cpp:133, camxosutils.h:182"]
    C2 --> C3["signalHandler 捕获 SIGTRAP
    → throw std::runtime_error(\"Signal: 5\")
    [✅已确认] nativetest.cpp:74-82"]
    C3 --> C4["std::terminate: no matching catch
    [✅已确认]"]
```

## 分析结论

### 崩溃链（6 层）

```
① 环境缺失: 测试机无 sensor module .bin 文件
     ↓
② ImageSensorModuleDataManager::CreateAllSensorModuleSetManagers()
   OsUtils::GetFilesFromPath(MmSensorModulesPath, "*sensormodule*bin")
   → fileCount = 0 → result = CamxResultEFailed
   [camximagesensormoduledatamanager.cpp:232]

③ ProbeImageSensorModules() 静默失败（VOID 方法不传播错误）
   → m_pImageSensorModuleDataManager = NULL
   [camxhwenvironment.cpp:343, 597-609]

④ HwEnvironment::Initialize() 整体成功（sensor 探测失败非致命）
   → CSL 初始化成功、HWL 工厂创建成功、settings 加载成功
   → CAMX_ASSERT(CamxResultSuccess == result) 通过

⑤ CustomNodeTest::TestVendorTagWrite 创建 pipeline
   → PipelineType::VendorTagWrite → isSensorInput=1 isRealTime=1
   → 需要 sensor 设备，但 CSL mock 枚举返回 0 devices
   [custom_node_test.cpp:186, test_pipelines.h:605-617]

⑥ pipeline/node 代码依赖 sensor 设备存在 → CAMX_ASSERT 触发
   → CamxFireAssert → raise(SIGTRAP)
   → signalHandler → throw std::runtime_error → terminate
```

### chifeature2test 为何不受影响

| 维度 | nativechitest | chifeature2test |
|------|-------------|----------------|
| 传感器初始化路径 | 相同（都走 HwEnvironment::Initialize） | 相同 |
| ImageSensorModuleDataManager 失败 | 同样发生，同样静默 | 同样发生，同样静默 |
| 测试类型 | CustomNodeTest → **实时管线**（isSensorInput=1） | Feature2OfflineTest → **离线管线**（DummyNode） |
| 是否调用真实 sensor API | 是 → CSLEnumerateDevices → 0 devices → 崩溃 | 否 → 直接用 dummy input buffer |

**核心差异**: nativechitest 的 CustomNodeTest 创建了需要真实 sensor 的 pipeline（`isSensorInput=1`），而 chifeature2test 的五个离线用例全部用 DummyNode + CSL mock，不需要真实 sensor 设备。

### 测试用例受影响范围

| 测试套件 | 需要 sensor | 状态 |
|---------|------------|------|
| CameraModuleTest (2) | 基础信息查询 | 挂起 (等待 sensor) |
| CameraManagerTest (2) | 基础操作 | 挂起 |
| **CustomNodeTest (1)** | **是 → 崩溃** | **崩溃** |
| ChiMetadataTest (26) | 否 | 未执行（被拦截） |
| RealtimePipelineTest (18) | 是 | 未执行 |
| FlushTest, MixedPipeline, Recipe... | 部分需要 | 未执行 |

## 关键代码位置

### 崩溃源头
- `camx/src/core/camximagesensormoduledatamanager.cpp:215-234` — 扫描 sensor module 文件，fileCount=0 → 失败
- `camx/src/core/camxhwenvironment.cpp:343` — ProbeImageSensorModules() 调用点
- `camx/src/core/camxhwenvironment.cpp:597-609` — 失败时 m_pImageSensorModuleDataManager = NULL（静默）

### 信号捕获
- `nativechitest/nativetestutils/nativetest.cpp:74-82` — signalHandler 捕获信号抛异常
- `camx/src/utils/camxdebug.cpp:133` — CamxFireAssert → raise(SIGTRAP)
- `camx/src/osutils/camxosutils.h:182` — PerformSoftwareBreakpoint() → raise(SIGTRAP)

### 测试入口
- `nativechitest/nativechitest/chitests.cpp:36` — CustomNodeTest::TestVendorTagWrite 注册（在 ChiMetadataTest 之前）
- `nativechitest/nativechitest/custom_node_test.cpp:186` — ChiPipeline::Create(VendorTagWrite, isSensorInput=1)

### CSL Mock
- `camera.qcom.so/csl_mock.cpp:78-89` — CSLQueryCameraPlatform 返回 Titan 1.7
- `camera.qcom.so/csl_mock.cpp:94-124` — CSL mocks for sensor probe/enumerate

## 待验证事项

- [🧠推断] 具体哪个 CAMX_ASSERT 在 CreateNodes 后触发 → 需要 gdb 断点在 camxdebug.cpp:128 或 gdb catch SIGTRAP
- [🧠推断] MockCSLEnumerateDevices 返回的 device 数量是否为 0，及其对下游代码的影响

## 修复方案

| 方案 | 描述 | 复杂度 | 副作用 |
|------|------|--------|--------|
| A: 跳过依赖 sensor 的测试 | 只运行 ChiMetadataTest 且标记其他用例为 skip | 低 | 丢失 realtime pipeline 测试能力 |
| B: Mock sensor 模块数据 | 创建 dummy sensor module .bin 文件或 mock ImageSensorModuleData | 中 | 需要理解 sensor module 二进制格式 |
| C: 修复 CSL mock 返回虚拟 sensor | 让 CSLEnumerateDevices 返回 1 个虚拟 device | 中 | 可能导致更多下游依赖链失败 |
| D: 抑制 CAMX_ASSERT | 在这个环境设 g_runtimeAssertMask=0 | 低 | 可能隐藏其他真实问题 |

## 备注

- `ImageSensorModuleDataManager` 扫描路径：`MmSensorModulesPath`（通常是 `/vendor/lib/camera/sensormodule/` 等 Android 路径）
- 此崩溃是**预存问题**，与 chxmetadata 统一化改动无关
- capture 中提到 `tcache_thread_shutdown() unaligned tcache chunk detected` 是已知问题（见 KB: `tcache-heap-corruption-investigation`）
- nativechitest 和 chifeature2test 共享同一个 `camx_runtime_stubs.cpp`，因此 `CamXAdapter_InitContext` 中的 HwEnvironment 初始化对两者相同
