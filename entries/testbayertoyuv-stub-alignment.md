# TestBayerToYUV 执行路径 — real vs stub 全量对照

> 类型：源码分析
> 置信度底线：本文档最低置信度为 ✅已确认（三份并行调查交叉验证）

## ❓ 问题背景
聚焦 TestBayerToYUV 用例，系统调查当前执行逻辑与源代码的对齐程度，识别每个 stub 点。

## 🔍 调查方法
三路并行追踪：初始化阶段 / 执行阶段 / CamX 内部，覆盖 chi_stub.cpp、chiframework_stubs.cpp、camx_runtime_stubs.cpp、csl_mock.cpp、dummy_node.cpp 全部路径。

## 💡 整体结论

```
TestBayerToYUV 验证的是 Feature2 框架的 结构正确性（状态机、管线调度、
buffer 生命周期、fence 传播），而非 IQ 正确性（像素处理、metadata 驱动）。

当前 stub 层可分为三类：
  A. 必要 mock (by design) — DummyNode、CSL mock、fake sensor
  B. 可消除 stub — metadata ops、vendor tag query、ActivatePipeline 路由
  C. 架构限制 — CSL MapBuffer FD 映射、Gralloc buffer
```

## ✅ 运行真实代码的组件

| 组件 | 代码路径 | 验证 |
|------|---------|------|
| Feature2 状态机 | chifeature2base.cpp (chi-cdk，原始编译) | ✅已确认 |
| CamX Pipeline 调度 | Pipeline::ProcessRequest → DeferredRequestQueue → Node dispatch | ✅已确认 |
| CamX Session 生命周期 | CreateSession / DestroySession 通过 CamXAdapter | ✅已确认 |
| CSL fence 语义 | csl_mock.cpp: mutex+condvar+async callback，功能正确 | ✅已确认 |
| Vendor tag 注册 | hw_vendor_tags.cpp 81 sections，VendorTagManager lookup 全部成功 | ✅已确认 |
| MetaBuffer 生命周期 | Create/Destroy/AddRef/ReleaseRef 通过 CamXAdapter 到真实 MetaBuffer | ✅已确认 |
| TBM | CHITargetBufferManager 原始 chi-cdk 代码 | ✅已确认 |
| Buffer negotiation | DummyNode pass-through 传播维度 | ✅已确认 |
| Pipeline descriptor | CamXAdapter_CreatePipelineDescriptor → 真实 CamX | ✅已确认 |

## ⚠️ Stub 分类

### A. 必要 Mock (by design, 不应消除)

| Stub | 位置 | 行为 | 为何必要 |
|------|------|------|---------|
| DummyNode | dummy_node.cpp | 信号 fence，不处理像素 | 替代 BPS/IPE HW，无法在 x86 运行 |
| CSL mock (alloc/open/close) | csl_mock.cpp | mmap 替代 ION/DMA-BUF | 无内核驱动 |
| DummyHwFactory | camx_runtime_stubs.cpp:382 | 所有 node type → DummyNode | 无 HW |
| Fake sensor/camera info | chi_stub.cpp:101-216 | 硬编码 5344x4016, 3 modes | 无传感器 |
| CAMXCustomizeEntry | camx_runtime_stubs.cpp:446 | 空 OEM interface | 无 OEM 定制 |

### B. 可消除 Stub (有真实实现可委托)

| # | Stub | 状态 | 修复 |
|---|------|------|------|
| B1 | StubMetaGetTag | ✅ 已消除 (da64766) | 委托到 CamXAdapter_MetaGetTag → MetaBuffer::GetTag |
| B2 | StubMetaSetTag | ✅ 已消除 (da64766) | 委托到 CamXAdapter_MetaSetTag → HAL3MetadataUtil::GetMetadataInfoByTag + MetaBuffer::SetTag |
| B3 | StubMetaGetVendorTag | ✅ 已消除 (da64766) | VendorTagManager::QueryVendorTagLocation → MetaBuffer::GetTag |
| B4 | StubMetaSetVendorTag | ✅ 已消除 (da64766) | 同上 → MetaBuffer::SetTag |
| B5 | StubMetaCopy | ✅ 已消除 (da64766) | 委托到 CamXAdapter_MetaCopy → MetaBuffer::Copy |
| B6 | StubMetaMerge | ✅ 已消除 (da64766) | 委托到 CamXAdapter_MetaMerge → MetaBuffer::Merge |
| B7 | StubMetaClone | ✅ 已消除 (da64766) | 委托到 CamXAdapter_MetaClone → MetaBuffer::Clone |
| B8 | StubQueryVendorTagLocation | ✅ 已消除 (da64766) | 委托到 CamXAdapter_QueryVendorTagLocation → VendorTagManager |
| B8+ | GetVendorTagId stub | ✅ 已消除 (06c934c) | 旧 stub 返回 0x80000000+enum，产生伪造 tag ID。新实现用 CHITAGSOPS 函数指针查 VendorTagManager，24 个 tag name 映射表，lazy cache。19/24 正确解析，5 个 chi-cdk 扩展 tag 未注册(非关键) |
| B9 | ExtensionModule::ActivatePipeline | ✅ 已消除 (835af66) | 委托 ChiModule→pActivatePipeline→CamXAdapter→ChiContext::ActivatePipeline→StreamOn |
| B10 | ExtensionModule::DeactivatePipeline | ✅ 已消除 (835af66) | 委托 ChiModule→pDeactivatePipeline→CamXAdapter→ChiContext::DeactivatePipeline→StreamOff |
| B11 | ExtensionModule::Flush | ✅ 已消除 (835af66) | 委托 ChiModule→pFlushSession→CamXAdapter→ChiContext::FlushSession→Session::Flush |

**Size 计算 (B2/B4)**：`TagSizeByType[pMetadataInfo->type] * count`，与原始 camxchi.cpp 完全一致。`HAL3MetadataUtil::InitializeMetadataTable()` 已在 `CamXAdapter_InitContext` 中调用 (666 tags)。

### C. 架构限制 (需重大改造，当前阶段不修)

| Stub | 位置 | 问题 | 为何不修 |
|------|------|------|---------|
| CSL MockCSLMapBuffer | csl_mock.cpp | 丢弃 input FD，创建匿名 mmap | test buffer 是 malloc 不是 DMA-BUF，无法 mmap fd |
| StubBufferManager (全套) | chi_stub.cpp:639-740 | calloc 替代 Gralloc/ION | 无 Gralloc HAL |
| CamxMemAlloc | chi_stub.cpp:833 | calloc 替代 ION | 同上 |
| ImageFormatUtils | camx_runtime_stubs.cpp:40-193 | 简化尺寸计算，无对齐 | DummyNode 不处理像素 |
| CHI fence (chiframework_stubs) | chiframework_stubs.cpp:57-77 | CreateChiFence→NULL | chi-cdk 层 fence 与 CamX CSL fence 独立 |

## 🌳 Metadata 路径分析 (✅ 已修复 — commits da64766 + 06c934c)

```
修复后 metadata 路径:
  chi-cdk SetTag(tagID, data, count)
    → StubMetaSetTag → CamXAdapter_MetaSetTag
    → HAL3MetadataUtil::GetMetadataInfoByTag(tagID) → type
    → size = TagSizeByType[type] * count
    → MetaBuffer::SetTag(tagID, data, count, size) ✓

  chi-cdk GetTag(tagID)
    → StubMetaGetTag → CamXAdapter_MetaGetTag
    → MetaBuffer::GetTag(tagID) → VOID* ✓

  chi-cdk SetVendorTag("section", "name", data, count)
    → StubMetaSetVendorTag → CamXAdapter_MetaSetVendorTag
    → VendorTagManager::QueryVendorTagLocation → tagID
    → MetaBuffer::SetTag(tagID, ...) ✓

  chi-cdk Copy/Merge/Clone
    → MetaBuffer::Copy/Merge/Clone ✓

  chi-cdk GetVendorTagId(VendorTag::TuningMode)
    → ExtensionModule::GetVendorTagId(5)
    → s_cachedIds[5] (= 正确 tag ID from VendorTagManager) ✓
    (旧 stub: 返回 0x80000005 伪造 ID → MetaBuffer 报 Invalid tag)
```

**发现的附带问题 (B8+)**:
stub `GetVendorTagId()` 返回 `0x80000000 + enum` 而非查 VendorTagManager。
TuningMode(enum=5) → tag ID 0x80000005 → 线性索引映射到 sharpness.range(maxSize=8)
→ 写入 60 字节 TuningMode 数据时 MetaBuffer 报 `Invalid tag size 240 maxSize 8`。
修复: 24 条 name mapping 表 + lazy cache via CHITAGSOPS 函数指针。

## 📍 关键代码位置
- `chi_stub.cpp:387-458` — metadata ops (B1-B7 已改为 CamXAdapter 委托)
- `chi_stub.cpp:321-324` — StubQueryVendorTagLocation (B8 已改为 CamXAdapter + InitContext guard)
- `camx_runtime_stubs.cpp:645-760` — 15 个新 CamXAdapter_Meta* 函数
- `chiframework_stubs.cpp:182-230` — GetVendorTagId 真实实现 + 24 条 vendor tag name 映射表
- `chxextensionmodule.h:76` — GetVendorTagId 声明 (旧 inline stub 已移除)
- `chiframework_stubs.cpp:57-92` — fence/pipeline stubs (B9-B11 待修复)
- `camx_runtime_stubs.cpp:40-193` — ImageFormatUtils stubs (C4 位置)
- `csl_mock.cpp:260-283` — MockCSLMapBuffer (C1 位置)
- `dummy_node.cpp:26-48` — DummyNode::ExecuteProcessRequest (A1 位置)

## 📝 Git 提交链 (metadata 通道 + pipeline 生命周期修复)
```
835af66 phase3: align B9-B11 — ActivatePipeline/DeactivatePipeline/Flush delegate to real CamX
06c934c phase3: fix GetVendorTagId — resolve via VendorTagManager
da64766 phase3: eliminate B1-B8 metadata stubs — delegate to real MetaBuffer
```

## ⚠️ 红旗

1. ~~**B8 → B1-B4 依赖链**~~: ✅ 已修复 — QueryVendorTagLocation + MetaBuffer ops 同步消除
2. ~~**B9 ActivatePipeline 不走 ChiOps**~~: ✅ 已修复 — ExtensionModule 三个方法均通过 ChiModule → CHI ops 委托
3. ~~**StubMetaCopy/Merge no-op**~~: ✅ 已修复 — 委托到 MetaBuffer::Copy/Merge
4. **5 个 chi-cdk 扩展 vendor tag 未注册**: cropregions, statsSkip, ZSLSettings, livePreview, debugDumpConfig — 来自 chi-cdk usecase modules，非 HW vendor tags 也非 core vendor tags。GetVendorTagId() 对这 5 个返回 0。当前 B2Y 路径不使用。

## 📝 修复优先级建议

```
✅ Phase 1 (metadata 通道): B8 + B1-B7 + B8+ → DONE (commits da64766 + 06c934c)
   Metadata 从"黑洞"变为完整读写通道。
   
✅ Phase 2 (pipeline activate): B9-B11 → DONE (commit 835af66)
   ActivatePipeline/DeactivatePipeline/Flush 三层对齐。

🔧 Phase 2.5 (可选): 注册 5 个缺失 chi-cdk 扩展 vendor tag sections
   
❌ Phase 3 (如需 IQ): 替换 DummyNode 为真实 BPS/IPE (需要 FW 二进制)
```
