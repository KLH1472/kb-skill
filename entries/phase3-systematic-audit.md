# Phase 3 系统审计结果

> 类型：源码分析
> 置信度底线：本文档最低置信度为 ❓推测 的内容不可作为行动依据

## ❓ 问题背景
Phase 3 teardown 修复完成后（5/5 PASS + EXIT=0），对所有核心文件进行系统审计，识别残留问题。

## 🔍 审计范围
| 文件 | 行数 | 发现 HIGH | 发现 MEDIUM |
|------|------|----------|------------|
| chi_stub.cpp | ~850 | 5→修复4 | 7 |
| chiframework_stubs.cpp | ~400 | 4 | 11 |
| camx_runtime_stubs.cpp | ~650 | 5→修复2 | 5 |
| patched_srcs (9 files) | 各异 | 1→修复 | 3 |

## ✅ 已修复 (commit ce8621a)

| # | 位置 | 问题 | 修复 |
|---|------|------|------|
| 1 | chi_stub.cpp ChiCloseContext | 不调 DestroyContext → ChiContext 泄漏 | 添加 CamXAdapter_DestroyContext() |
| 2 | feature2testcase.cpp:743 | stuck-state break 后仍打印 PASS → 假阳性 | 条件判断：Complete→PASS, else→FAIL |
| 3 | camx_runtime_stubs.cpp:596 | catch(...) 吞掉所有异常 → 掩盖腐败 | 移除 try/catch |
| 4 | camxvendortags.cpp | patched 副本与原始一致 → 维护负担 | 删除，CMakeLists 指回原始 |
| 5 | chi_stub.cpp | g_cameraModule, StubContext 未用字段, 重复注释 | 删除 dead code |

## ⚠️ 已知残留 (MEDIUM — 当前测试路径不触发)

### chi_stub.cpp
| # | 行 | 问题 | 为何当前安全 |
|---|-----|------|------------|
| C1 | StubQueryVendorTagLocation | 所有 tag 返回 0x80000000 | 真正查询走 CamX VendorTagManager |
| C2 | StubMetaGet/SetTag | no-op | CamX 内部用 MetaBuffer API |
| C3 | StubWaitFenceAsync | 永不回调 | CamX 内部 fence 机制独立 |
| C4 | StubBufferManagerReleaseReference | refCount=0 不释放 | chi-cdk buffer manager 未用于 DummyNode |
| C5 | ChiQueryPipelineMetadataInfo | 硬编码 maxNumMetaBuffers=2 | 够用 |

### chiframework_stubs.cpp
| # | 行 | 问题 | 为何当前安全 |
|---|-----|------|------------|
| F1 | CreateChiFence | 返回 NULL | TBM fence 当前路径未触发 |
| F2 | SetOutputBuffers/SetInputBuffers | 无边界检查 | Pipeline max ports < 16 |
| F3 | ActivatePipeline/DeactivatePipeline | no-op | 走 ChiOps → real CamX |
| F4 | Feature descriptors | 空 {} | 仅 Bayer2Yuv 测试用自己的 descriptor |

### camx_runtime_stubs.cpp
| # | 行 | 问题 | 为何当前安全 |
|---|-----|------|------------|
| R1 | GetPlaneSize | 仅 NV12 正确 | DummyNode 不实际处理 pixel data |
| R2 | ValidateBufferSize | 永返 success | 同上 |
| R3 | const_cast StaticSettings | 技术上 safe (heap-allocated) | 已加注释 |

### patched_srcs
| # | 文件 | 问题 | 为何当前安全 |
|---|------|------|------------|
| P1 | camxpipeline.cpp:1815 | 删除 pool ASSERT | DummyNode 不需要 pool |
| P2 | camxnode.cpp:1403 (W2) | activePixelArrayWidth=0 | DummyNode 不读 |
| P3 | camxnode.cpp:6955 (W5) | buffer mgr NULL | DummyNode 不需要 |

## 📝 关键发现

### 第二轮审计 (ce8621a 之后) — 无新问题
- `_exit` workaround: 已确认清除
- 关键词扫描 (TODO/FIXME/HACK/workaround): 仅原始 QC 代码注释，非我们的补丁
- 6 个 stub headers 看似"未使用"，实际通过 include path 优先级被 14-45 个 .d 依赖文件引用
- StubContext: 仅 `bool initialized` + `&g_context` handle，合理最小占位符
- camx_patched_srcs 4 文件: 2-28 行变更，均为必要补丁
- extern "C" CamXAdapter_*: 全部被调用，无死代码

### patched_srcs 不可简单删除
4 个文件 (chimetadatautil.cpp, chimodule.cpp, feature2buffermanager.cpp, genericbuffermanager.cpp) 内容与原始**完全一致**，但必须保留在 `patched_srcs/` 目录编译——因为 `#include "..."` 相对路径解析依赖源文件所在目录。从 `patched_srcs/` 编译时，stubs 目录优先于原始 chi-cdk 目录，确保 stub headers 生效。

### 测试命令
```
cd build && ./chifeature2test/chifeature2test -t Feature2OfflineTest.TestBayerToYUV -f 1
```

## 📍 Git 提交链
```
bc2b33a phase3: restore default-case log in state machine loop
bdf065b phase3: remove stuck-state escape — align with source code
b00dc48 phase3: harden PASS criteria — fix 2 false-positive risks
835af66 phase3: align B9-B11 — ActivatePipeline/DeactivatePipeline/Flush
06c934c phase3: fix GetVendorTagId — resolve via VendorTagManager
da64766 phase3: eliminate B1-B8 metadata stubs — delegate to real MetaBuffer
ce8621a phase3: audit fixes — 5 HIGH issues resolved
ce16993 phase3: remove chi_stub session/pipeline fallback
9e153a1 phase3: fix OfflineLogger ODR double-free
2450fb1 phase3: clean exit — fix 3 teardown bugs
b917281 phase3: fix heap corruption — eliminate TBM double-release
```
