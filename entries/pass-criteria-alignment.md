# TestBayerToYUV PASS 标准对齐调查

> 类型：源码分析
> 置信度底线：✅已确认（原始源码 + patched 代码双向对比）

## ❓ 问题背景
系统调查当前 TestBayerToYUV 的 PASS 标准是否与源码对齐，是否存在假阳性风险。

## 💡 核心发现：源码的 PASS 标准本身就很低

源码 `feature2testcase.cpp` 中 `ValidateResult` 被注释掉 (`//TODO: validate result`)。
源码 `feature2offlinetest.cpp` 中 `MetadataNotification` handler 是空的 (`// not handled in phase 1`)。
**源码 PASS = "state machine 到达 Complete 且没有 crash"**。本质上是控制流正确性测试。

## 🔍 PASS 判定条件对比

| 检查项 | 源码 | patched | 一致性 |
|--------|------|---------|--------|
| Setup 6 项 ASSERT (Camera/Ops/Libs/Interface/Buffers/Meta) | ✅ | ✅ | ✅ 一致 |
| State machine → Complete | ✅ 必须 (死循环等) | ✅ 必须 (超时 break) | ⚠️ 行为不同 |
| 输出 buffer 内容校验 | ❌ 无 | ❌ 无 | ✅ 一致 |
| 输出 metadata 校验 | ❌ 无 | ❌ 无 | ✅ 一致 |
| ValidateResult | ❌ `//TODO` | ❌ 同 | ✅ 一致 |
| Pipeline 错误码 | ❌ return 被忽略 | ❌ 同 | ✅ 一致 |
| Buffer 尺寸/维度 | ❌ 无 | ❌ 无 | ✅ 一致 |
| ProcessBufferFromResult 返回值 | ❌ 忽略 | ❌ 忽略 | ✅ 一致 |
| 卡住时行为 | 死循环挂起 | 10s 超时 break | ⚠️ patched 更好 |

## ⚠️ patched 引入的 2 个新风险

### 风险1: OutputErrorNotificationPending 静默通过 (HIGH)
**源码行为**: `OutputErrorNotificationPending` 落入 `default` case → 只打日志 → 不调 `ProcessRequest` → 状态机卡在错误状态 → 最终挂起(无超时)或需要外部干预
**patched行为**: 与 `OutputNotificationPending` 同样处理 → 调 `ProcessRequest` → 状态机可能正常推进到 Complete → 管线错误被吞掉 → **假阳性**

### 风险2: fprintf PASS/FAIL 未挂钩 CF2_ASSERT (MEDIUM)
**源码行为**: 无 PASS/FAIL 输出，也无 CF2_ASSERT — 只靠 Setup 阶段的 ASSERT 和死循环挂起
**patched行为**: 循环后 fprintf PASS/FAIL — 但只是文本输出，未调 `SetFailed()` — test framework 不感知 FAIL

### 不存在的风险（两者共有的弱点）
- DummyNode 输出全零 buffer → 两者都不检查，都 PASS
- 空 metadata → 两者都不检查
- Fence failure → 两者都不检查
- 错误的输出维度 → 两者都不检查

## 📝 修复计划

| # | 修复 | 方法 |
|---|------|------|
| 1 | OutputErrorNotificationPending 标记失败 | 在此 case 中打 ERROR 日志 + 设置错误标志，循环后检查 |
| 2 | PASS/FAIL 挂钩 CF2 framework | 循环后用 `CF2_EXPECT(state == Complete)` 替代 fprintf |

## ✅ 修复结果

| Commit | 内容 |
|--------|------|
| `b00dc48` | OutputErrorNotificationPending 标记失败 + CF2_EXPECT 挂钩 framework |
| `bdf065b` | 移除 stuck-state escape — 对齐源码无超时循环 |
| `bc2b33a` | 恢复 default case CHX_LOG_INFO — 对齐源码 |

### 二轮审计结论 (bc2b33a 后)
- ALIGNED: 4 项 (cosmetic/build)
- INTENTIONAL_IMPROVEMENT: 5 项 (_pipelineError, OutputError 检测, CF2_EXPECT, pFeature2Base::Destroy, InputResourcePendingScheduled)
- RISK: 0
- BUG: 0
- ReleaseTargetBuffer 移除: 已确认是双释放修复 (commit b917281)，IMPROVEMENT

## 📍 关键代码位置 (修复后)
- `feature2testcase.cpp:698-728` — 状态机循环 (无 escape，对齐源码)
- `feature2testcase.cpp:712-716` — OutputErrorNotificationPending 标记 _pipelineError
- `feature2testcase.cpp:730-743` — CF2_EXPECT 挂钩 + fprintf PASS/FAIL
- `feature2testcase.cpp:725-726` — default case CHX_LOG_INFO (已恢复)
- `feature2testcase.cpp:748-751` — pFeature2Base::Destroy (源码缺失的清理)
- `feature2testcase.cpp:774` — `//ValidateResult` 注释 (源码原有，未改)
- `feature2offlinetest.cpp:595-598` — MetadataNotification 空 handler (源码原有，未改)
