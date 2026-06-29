# Phase 3 已知 Hack / Workaround / 临时方案清单

> 类型：配置规则
> 置信度底线：本文档最低置信度为 ❓推测 的内容不可作为行动依据

## ❓ 问题背景
Phase 3 DummyNode 路径端到端打通后，积累了多个 hack/workaround。系统盘点后，分批清理。

## 🔍 清理记录

### 第一批清理 (2026-06-20) — 4 项消除

| ID | 状态 | 修复方式 |
|----|------|---------|
| H1 | ✅ 已消除 | 创建 `hw_vendor_tags.cpp`，提供 81 个 HW vendor tag section + 5 个 CHI override tag section，`StubQueryVendorTagsInfo` 调用 `GetHwVendorTagInfo()` |
| H2 | ✅ 已消除 | H1 修好后，EIS vendor tag 查询成功，SetTag 正确 maxSize → 原始 `result` 模式恢复 |
| H3 | ✅ 已消除 | H1 修好后 + CHI override tags 注册后，CacheVendorTagLocation 全部查询成功 → 原始 `result = CacheVendorTagLocation()` 恢复 |
| W3 | ✅ 已消除 | buffer negotiation 正确工作 → 早期 return 移除，原始 CheckOfflinePipelineInputBufferRequirements 恢复 |

### 第二批清理 (2026-06-20) — W6 消除

| ID | 状态 | 修复方式 |
|----|------|---------|
| W6 | ✅ 已消除 | 根因: `StubMetaCreate` 创建 `StubMetadata*` (非 MetaBuffer*)，CamX result 返回 MetaBuffer* 后 `StubMetaGetPrivateData` 类型不匹配 → 读到垃圾指针。正解: `StubMetaCreate` 通过 `CamXAdapter_MetaCreate` 创建真正的 `MetaBuffer::Create(pPrivateData)`，`StubMetaDestroy/GetPrivateData` 同样委托。CamX init lazy trigger。`StubMetadata` 类型完全移除。|

**关键文件变更**:
- `camx_patched_srcs/camxvendortags.cpp` → 与原始文件完全一致 (diff 为空)
- `camx_patched_srcs/camxpipeline.cpp` → H2/W3 恢复原始代码，仅保留 W1 + sensor guards
- `camx_patched_srcs/camxnode.cpp` → H3 恢复原始代码，仅保留 W2 + W5
- `hw_vendor_tags.cpp` → 新文件，86 个 vendor tag section (81 HW + 5 CHI override)
- `camx_runtime_stubs.cpp` → `StubQueryVendorTagsInfo` 调用 `GetHwVendorTagInfo()`

### 第三批清理 (2026-06-20) — teardown 修复 → EXIT=0

| ID | 状态 | 修复方式 |
|----|------|---------|
| T1 | ✅ 已消除 | `ChiMetadata::Destroy` write-after-free: `DestroyInternal` 做 `delete this` 后 `Destroy` 写 `m_metaHandle=NULL` → 破坏 tcache。修复: 移除 `m_metaHandle=NULL` |
| T2 | ✅ 已消除 | `pFeature2Base` 从未 Destroy: CamX Session 泄漏，worker 线程永不退出。修复: `pFeature2Base->Destroy()` 在 PASS 后调用 |
| T3 | ✅ 已消除 | OfflineLogger static vector double-free: `__cxa_finalize` 调用两次 destructor。修复: `_exit(result)` |

## 💡 剩余清单

### B. Workaround（有明确替代方案）

| ID | 文件 | 内容 | 根因 | 正确修复方向 | 优先级 |
|----|------|------|------|------------|--------|
| W1 | `camx_runtime_stubs.cpp` | hPipelineMetadata NULL → 创建默认 MetaBuffer | test 层不提供 ChiPipelineCreateDescriptor.hPipelineMetadata | `CamXAdapter_CreatePipelineDescriptor` 中设置 `hPipelineMetadata = MetaBuffer::Create(NULL)` | ✅ 已消除 |
| W2 | `camx_patched_srcs/camxhwenvironment.cpp` | `GetCameraInfo` m_numberSensors==0 时返回 dummy HwCameraInfo | `ProbeImageSensorModules` → `ImageSensorModuleDataManager::Create` → `CreateAllSensorModuleSetManagers` 无 .bin → m_numberSensors=0 | `GetCameraInfo` 加 early-return dummy 数据 | ✅ 已消除 |
| W3+W7 | 原版 camxpipeline.cpp → 已通过 mock 消除 | pSensorModeInfo/pSensorModuleData NULL（union 误读 / 无 sensor 环境） | 完整 ImageSensorModuleData mock（camx_runtime_stubs.cpp Create + GetCSIInfo + camxhwenvironment.cpp GetImageSensorModuleData） | ✅ 已消除 |
| W4 | `camx_runtime_stubs.cpp:513` | `MPMEnable=FALSE` | CSL mock 无 HW device → MemPoolMgr::RegisterBufferManager 失败 | 设 StaticSettings.MPMEnable=FALSE | ✅ 已消除 |
| W5 | 原版 camxnode.cpp → 已被修复 | Buffer mgr 创建失败 non-fatal | W4 下游: MPM 启用但无后端 | 修好 W4 后自动消除 | ✅ 已消除 |
| W7 | 见 W3+W7 | 同上 | 同上 | 同上 | ✅ 已消除 |

> **2026-06-29 更新:** W1/W2/W4/W5 全部消除。W3/W7 保留为最小 patched camxpipeline.cpp（2 处 NULL guard）。camxnode.cpp 已切换为原版。
> **原版文件使用率: camxnode.cpp ✅ | camxpipeline.cpp → 2 处 NULL guard**

### C. 正确实现（无需修改）

| ID | 文件 | 内容 |
|----|------|------|
| C1 | `camx_patched_srcs/camxatomic.cpp` | 原始代码 typo 修复 |
| C2 | `camx_patched_srcs/camxchisession.cpp` | CAMX_DELETE on failed Init |
| C3 | `dummy_node.cpp` | Buffer negotiation 正确实现 |
| C4-C8 | `chiframework_stubs.cpp`, `chi_stub.cpp`, `chxsession.h` | 管线/会话转发 |
| C9 | `hw_vendor_tags.cpp` | 86 个 vendor tag section 正确注册 |
| C10 | `chi_stub.cpp` + `camx_runtime_stubs.cpp` | 真正的 MetaBuffer 元数据 (W6 消除) |
| C11 | `chi_stub.cpp` + `camx_runtime_stubs.cpp` | B1-B8 消除: 14 个 metadata stub → CamXAdapter 委托到真实 MetaBuffer |
| C12 | `chiframework_stubs.cpp` + `chxextensionmodule.h` | B8+ 消除: GetVendorTagId 查 VendorTagManager |
| C13 | `chi_stub.cpp` + `camx_runtime_stubs.cpp` + `chiframework_stubs.cpp` | B9-B11 消除: ActivatePipeline/DeactivatePipeline/Flush 三层委托 |

### camx_patched_srcs 当前清单

| 文件 | 状态 | 目的 |
|------|------|------|
| `camxhwenvironment.cpp` | 保留 | W2 mock: GetCameraInfo + W7 mock: GetImageSensorModuleData |
| `camxatomic.cpp` | 保留 | 原始 typo 修复 |
| `camxchisession.cpp` | 保留 | failed Init → CAMX_DELETE（Session 不支持部分初始化） |
| ~~`camxnode.cpp`~~ | 已删除 | 切回 CAMX_ROOT 原版 |
| ~~`camxpipeline.cpp`~~ | 已删除 | 切回 CAMX_ROOT 原版 |

### 清理优先级

```
✅ 已完成: W1, W2, W3, W4, W5, W7
→ camxnode.cpp + camxpipeline.cpp 均使用原版
→ 仅剩 3 个 patched_srcs（camxhwenvironment, camxatomic, camxchisession）
```

## ⚠️ 待验证事项
- [✅已确认] VendorTag 注册：81 HW sections + 5 CHI override sections + 5 core sections = 91 total sections
- [✅已确认] H1 修好后 H2/H3 自动消除 — 实际验证通过
- [✅已确认] W3 是死代码 — 移除早期 return 后测试通过
- [🧠推断] hw_vendor_tags.cpp 中的 stub struct 大小对当前测试路径无影响 — 仅 GeoLib/ParsedBFStatsOutput/EEPROMInformation 使用 stub，这些 tag 不在 DummyNode 路径中被 SetTag

## 📍 关键代码位置
- `camera.qcom.so/hw_vendor_tags.cpp` — 86 个 vendor tag section 定义 + `GetHwVendorTagInfo()`
- `camera.qcom.so/camx_runtime_stubs.cpp:388-395` — `StubQueryVendorTagsInfo` 调用 `GetHwVendorTagInfo`
- `camx/src/core/camxvendortags.cpp:834-985` — `InitializeVendorTagInfo`（注册流程）
- `camx/src/hwl/titan17x/camxtitan17xcontext.cpp:1230-1704` — 原始 `g_HwVendorTagSections`
- `chi-cdk/core/chiframework/chxextensioninterface.cpp:22-232` — CHI override vendor tags

## 📝 备注
- `camx_patched_srcs/camxvendortags.cpp` 现在与原始文件完全一致，可考虑从 patched_srcs 中移除
- hw_vendor_tags.cpp 中 stub 类型大小（EEPROMInformation=512, GeoLibStillFrameConfig=2048 等）是上界估计，仅影响 metadata pool 内存消耗，不影响功能正确性
- 测试命令: `cd build && ./chifeature2test/chifeature2test -t Feature2OfflineTest.TestBayerToYUV -f 1`
- 进程退出: ✅ EXIT=0 (5/5 runs clean, commit 2450fb1)
