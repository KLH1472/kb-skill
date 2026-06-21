# 格式协商流程

## 问题背景
搜索 CAMX 中格式协商（format negotiation）相关代码，了解从 HAL 请求到 Pipeline 内部 buffer 协商的完整流程。

## 分析结论

### 三层格式选择

CAMX 的格式协商发生在三个层次：

1. **ChiContext::SelectFormat** — 显式格式映射层，将 Android `ChiStreamFormat` 映射为 CAMX 内部 `Format` 枚举
2. **ChiContext::GetImplDefinedFormat** — IMPLEMENTATION_DEFINED 格式的深度决策，基于 static settings、gralloc flags、平台能力等多条件分支
3. **ImageFormatUtils::GetFormat** → **FormatMapper** — 硬件感知的格式最终确定

### Pipeline 层 buffer 协商

缓冲区需求协商采用**反向传播**模式（从 sink 到 source）：

1. `Pipeline::CreateNodes()` 从 sink 节点触发 `TriggerBufferNegotiation()`
2. 每个 sink 节点处理自身输入 buffer 需求后，通过 `BufferRequirementNotification()` 通知父节点
3. 父节点收集所有输出端口的需求通知后，处理自身 `ProcessInputBufferRequirement()`
4. 递归向上传播直到 source 节点

### 协商失败重试机制

若初始协商失败，`Pipeline::RenegotiateInputBufferRequirement()` 将：
- 重置所有节点的 `BufferNegotiationData`
- 强制所有 sink output port 切换为 NV12 格式（仅对 UBWC 格式的 IMPLEMENTATION_DEFINED buffer 生效）
- 清除 UBWC gralloc flags（Private0、Private3）
- 重新触发 buffer 协商

### 静态设置强制覆盖

- `forceDisableUBWCOnIfeIpeLink` → IFE→IPE 链路 UBWCTP10 强制降级为 YUV420NV12
- `enableUBWCNV124ROnIFEFullOutIPELink` → IFE FullOut→IPE 链路 UBWCTP10 改为 UBWCNV124R

### HAL3 Override 透传

通过 `SetNativeOverrideFormat`/`GetNativeOverrideFormat` 将上层 `pHalStream->overrideFormat` 传递到 CHI 层，在 `SetChiStreamInfo` 中被消费写回 stream wrapper。

## 关键代码位置

- `camx/src/core/chi/camxchicontext.cpp:3898` — `ChiContext::SelectFormat` 显式格式映射
- `camx/src/core/chi/camxchicontext.cpp:4042` — `ChiContext::GetImplDefinedFormat` IMPLEMENTATION_DEFINED 深度决策
- `camx/src/mapperutils/formatmapper/camximageformatutils.cpp:723` — `ImageFormatUtils::GetFormat` 硬件层格式确定
- `camx/src/core/camxpipeline.cpp:3346` — `Pipeline::RenegotiateInputBufferRequirement` 协商失败重试
- `camx/src/core/camxpipeline.cpp:1617-1644` — Pipeline 层 buffer 协商触发入口
- `camx/src/core/camxpipeline.cpp:1740-1767` — 离线 pipeline 的协商重试路径
- `camx/src/core/camxnode.cpp:5541` — `Node::TriggerBufferNegotiation` 触发协商
- `camx/src/core/camxnode.cpp:5558` — `Node::ProcessInputBufferRequirement` 处理输入需求
- `camx/src/core/camxnode.cpp:5687` — `Node::BufferRequirementNotification` 反向传播需求通知
- `camx/src/core/camxnode.cpp:7315` — `Node::SwitchNodeOutputFormat` 切换输出格式为 NV12
- `camx/src/core/camxnode.cpp:7396` — `Node::ResetBufferNegotiationData` 重置协商状态
- `camx/src/core/chi/camxchicontext.cpp:4596-4635` — 静态设置强制链路格式覆盖
- `camx/src/core/hal/camxhal3stream.h:249` — `SetNativeOverrideFormat` HAL override 透传
- `camx/src/core/chi/camxchicontext.cpp:4339` — override format 在 SetChiStreamInfo 中被消费

## 相关概念

- SelectFormat
- GetImplDefinedFormat
- FormatMapper
- ImageFormatUtils
- BufferNegotiationData
- TriggerBufferNegotiation
- RenegotiateInputBufferRequirement
- SwitchNodeOutputFormat
- OverrideOutputFormat
- GrallocUsage
- StaticSettings.outputFormat

## 备注

- 代码中多处有 `@todo (CAMX-1797)` 标记，提示当前协商逻辑不够完善，理想情况应遍历 topology XML 中所有有效格式
- `@todo (CAMX-1512)` 标记 Format selection 需要修复
