# BufferQueueCore 架构与机制分析

## 问题背景
深入分析 AOSP `native/libs/gui/BufferQueueCore.cpp` 及关联文件，理解 BufferQueue 核心数据结构和 slot 状态机。

## 分析结论

### 1. 总体架构：三层分离设计

BufferQueue 系统划分为三个类，各司其职：

| 类 | 角色 | 责任 |
|---|---|---|
| `BufferQueueCore` | 共享状态核心 | 管理 slot 数组、状态、队列、同步原语 |
| `BufferQueueProducer` | 生产者接口 | 实现 IGraphicBufferProducer（dequeue/queue/detach/attach） |
| `BufferQueueConsumer` | 消费者接口 | 实现 IGraphicBufferConsumer（acquire/release/detach/attach） |

`BufferQueueProducer` 和 `BufferQueueConsumer` 都持有 `sp<BufferQueueCore> mCore` 和 `BufferQueueDefs::SlotsType mSlots`（指向 `mCore->mSlots` 的引用），三者通过 `mMutex` 同步。Producer 和 Consumer 被声明为 BufferQueueCore 的 `friend class`（`BufferQueueCore.h:56-57`）。

### 2. 核心数据结构

#### 2.1 Slot 数组（mSlots）

`BufferQueueCore.h:212` — 固定大小 64 的 `BufferSlot` 数组（`NUM_BUFFER_SLOTS = 64`，见 `ui/BufferQueueDefs.h:25`）。这是生产者/消费者之间共享的核心存储。每个 `BufferSlot` 包含：

| 字段 | 类型 | 说明 |
|---|---|---|
| `mGraphicBuffer` | `sp<GraphicBuffer>` | gralloc 分配的图形缓冲区 |
| `mBufferState` | `BufferState` | 当前状态机状态（4 个计数器） |
| `mRequestBufferCalled` | `bool` | 验证生产者是否调用了 requestBuffer |
| `mFrameNumber` | `uint64_t` | 该 slot 的帧序号，用于 LRU 出队排序 |
| `mFence` | `sp<Fence>` | 同步栅栏，信号表示前一拥有者的工作完成 |
| `mAcquireCalled` | `bool` | 消费者是否已见过此 buffer（影响 binder 传输） |
| `mNeedsReallocation` | `bool` | buffer 被重新分配且生产者需要知道 |

#### 2.2 四类 Slot 集合

`BufferQueueCore` 使用 4 个集合追踪所有 64 个 slot 的归属（`BufferQueueCore.h:217-230`）：

| 集合 | 类型 | 含义 |
|---|---|---|
| `mFreeSlots` | `std::set<int>` | FREE 状态且无 buffer 附着的 slot |
| `mFreeBuffers` | `std::list<int>` | FREE 状态且有 buffer 附着的 slot |
| `mActiveBuffers` | `std::set<int>` | 非 FREE 状态的 slot（DEQUEUED/QUEUED/ACQUIRED/SHARED） |
| `mUnusedSlots` | `std::list<int>` | 未分配、不参与当前工作的 slot |

这 4 个集合互斥：每个 slot 在同一时刻只能属于其中一个。

#### 2.3 FIFO 队列（mQueue）

`BufferQueueCore.h:215` — `Vector<BufferItem>` 类型的 FIFO。在同步模式下保存已 queued 待消费的 buffer 列表。在异步模式下最多只有 1 个元素（新 buffer 直接替换旧的）。

#### 2.4 同步原语

| 变量 | 用途 |
|---|---|
| `mMutex` | 保护所有成员变量的互斥锁 |
| `mDequeueCondition` | 生产者等待空闲 slot 的条件变量 |
| `mIsAllocatingCondition` | 生产者等待其他生产者完成分配的 CV |
| `mIsAllocating` | 布尔标志，防止并发 buffer 分配 |

### 3. BufferState 五状态状态机

`BufferSlot.h:35-174` 定义了 buffer 状态机，使用 4 个字段的组合编码 5 种状态：

| 状态 | mShared | mDequeueCount | mQueueCount | mAcquireCount |
|---|---|---|---|---|
| FREE | false | 0 | 0 | 0 |
| DEQUEUED | false | 1 | 0 | 0 |
| QUEUED | false | 0 | 1 | 0 |
| ACQUIRED | false | 0 | 0 | 1 |
| SHARED | true | any | any | any |

注意使用**计数器**而非布尔值——这使得状态转移可以正确处理重复操作（如多次 dequeue 同一 slot 在 shared buffer 模式下）。

状态转移矩阵：

```
FREE ──dequeue()──► DEQUEUED ──queue()────► QUEUED ──acquire()──► ACQUIRED ──release()──► FREE
  ▲                     │                       │                      │
  │    cancel()         │    freeQueued()       │                      │
  └─────────────────────┘  ◄────────────────────┘                      │
  │                                                                     │
  └───────────────────────────── detachConsumer() ──────────────────────┘
```

- `dequeue()`: `FREE → DEQUEUED`，mDequeueCount++
- `cancel()`: `DEQUEUED → FREE`，mDequeueCount--
- `queue()`: `DEQUEUED → QUEUED`，mDequeueCount--, mQueueCount++
- `freeQueued()`: `QUEUED → FREE`（异步模式下旧 buffer 被新 buffer 替换时），mQueueCount--
- `acquire()`: `QUEUED → ACQUIRED`，mQueueCount--, mAcquireCount++
- `acquireNotInQueue()`: 直接从 shared buffer 获取时，仅 mAcquireCount++
- `release()`: `ACQUIRED → FREE`，mAcquireCount--

SHARED 状态是叠加在其余状态之上的：`mShared = true` 表示该 buffer 处于 shared buffer mode，可同时处于多种状态。

### 4. 关键成员变量说明

`BufferQueueCore.h:149-378` 定义了约 50 个成员变量，分类如下：

**连接与标识：**
- `mIsAbandoned` — BufferQueue 已废弃标志
- `mConsumerControlledByApp` — 消费者是否由应用控制
- `mConsumerName` — 日志中识别的消费者名称
- `mUniqueId` — 全局唯一 ID（高 32 位 = PID，低 32 位 = 自增计数器），见 `BufferQueueCore.cpp:66-70`
- `mConnectedApi` / `mConnectedPid` — 当前连接的生产者 API 类型和 PID

**监听器：**
- `mConsumerListener` — 通知消费者的异步事件
- `mConnectedProducerListener` — 接收 onBufferReleased/onBuffersDiscarded 回调
- `mBufferReleasedCbEnabled` / `mBufferAttachedCbEnabled` — 控制回调是否启用

**Buffer 配置：**
- `mDefaultBufferFormat` — 默认 `PIXEL_FORMAT_RGBA_8888`
- `mDefaultWidth` / `mDefaultHeight` — 默认 1x1
- `mDefaultBufferDataSpace` — 默认 `HAL_DATASPACE_UNKNOWN`
- `mConsumerUsageBits` — 消费者要求的 gralloc usage flags
- `mConsumerIsProtected` — 消费者是否支持 protected buffer

**Buffer 数量控制（核心公式）：**

`BufferQueueCore.cpp:246-254` 的 `getMaxBufferCountLocked()`:

```
maxBufferCount = mMaxAcquiredBufferCount    // 消费者最多同时持有数（默认 1）
               + mMaxDequeuedBufferCount     // 生产者最多同时 dequeued 数（默认 1）
               + (asyncMode || dequeueBufferCannotBlock ? 1 : 0)  // 异步模式额外 +1
maxBufferCount = min(mMaxBufferCount, maxBufferCount)  // 不超过消费者设置的上限
```

`BufferQueueCore.cpp:224-232` 的 `getMinUndequeuedBufferCountLocked()`:
- 异步模式或非阻塞模式：`mMaxAcquiredBufferCount + 1`
- 否则：`mMaxAcquiredBufferCount`
- 这保证了消费者始终有 buffer 可获取

`getMinMaxBufferCountLocked()` = `getMinUndequeuedBufferCountLocked() + 1`（至少需要一个可被 dequeue 的 buffer）

**模式标志：**
- `mAsyncMode` — 异步模式，新 queue 的 buffer 替换旧的
- `mSharedBufferMode` — shared buffer 模式（producer 和 consumer 共享单个 buffer）
- `mAutoRefresh` — shared buffer 模式下消费者即使 queue 为空也返回 buffer
- `mSharedBufferSlot` — shared buffer 所在的 slot 索引
- `mDequeueBufferCannotBlock` — dequeue 不允许阻塞
- `mQueueBufferCanDrop` / `mLegacyBufferDrop` — queue 时是否允许丢弃 buffer
- `mAutoPrerotation` — 自动预旋转（当 consumer 驱动 buffer size 且 transform hint 含 90/270 度旋转时自动交换宽高）

**其他：**
- `mFrameCounter` — 每成功 queueBuffer 时递增的全局帧计数器
- `mTransformHint` / `mTransformHintInUse` — 屏幕旋转优化提示
- `mBufferAge` — 最近 dequeue 的 buffer 自从上次 queued 经过的帧数
- `mGenerationNumber` — 生成号，attachBuffer 时验证
- `mAllowAllocation` — dequeue 时是否允许分配新 buffer
- `mBufferHasBeenQueued` — 是否曾有 buffer queued（用于首次 dequeue 不限制的条件）
- `mOccupancyTracker` — 追踪队列占用率历史，用于判断双缓冲/三缓冲

### 5. 关键方法分析

#### 5.1 构造与初始化 (`BufferQueueCore.cpp:87-143`)

- 将所有 slot 的 `mBufferState` 初始化为 FREE
- 前 `maxBufferCount` 个 slot 放入 `mFreeSlots`（初始为 `mMaxBufferCount = 64`）
- 剩余 slot 放入 `mUnusedSlots`
- `mUniqueId` 通过 `getUniqueId()` 生成：高 32 位 = PID，低 32 位 = 原子计数器

#### 5.2 `clearBufferSlotLocked(int slot)` (`BufferQueueCore.cpp:256-279`)

重置一个 slot 到初始 FREE 状态：
- 清除 GraphicBuffer
- 重置 BufferState
- 清除 mRequestBufferCalled、mFrameNumber、mAcquireCalled
- 设置 mNeedsReallocation = true
- 清除 fence

#### 5.3 `freeAllBuffersLocked()` (`BufferQueueCore.cpp:281-310`)

释放所有 slot 的 buffer（producer 断开时调用）：
- 将 mFreeSlots、mFreeBuffers、mActiveBuffers 中的 slot 全部 clear 后归入 mFreeSlots
- mQueue 中的 buffer 标记为 stale，mAcquireCalled 设为 false（强制重新发送 buffer handle）

#### 5.4 `discardFreeBuffersLocked()` (`BufferQueueCore.cpp:312-326`)

释放 mFreeBuffers 中的 buffer 以减少内存占用，通知 producer 这些 buffer 已被丢弃。

#### 5.5 `adjustAvailableSlotsLocked(int delta)` (`BufferQueueCore.cpp:328-367`)

动态调整可用 slot 数量：
- `delta > 0`：从 mUnusedSlots 移出 delta 个 slot 到 mFreeSlots
- `delta < 0`：从 mFreeSlots 或 mFreeBuffers 中清除 delta 个 slot 并移入 mUnusedSlots
- 返回 false 表示无法满足请求

#### 5.6 `waitWhileAllocatingLocked()` (`BufferQueueCore.cpp:369-374`)

阻塞等待直到 mIsAllocating 为 false——防止并发 buffer 分配导致 slot 状态不一致。

#### 5.7 `dumpState()` (`BufferQueueCore.cpp:147-222`)

完整的调试输出，打印：
- 核心配置参数
- FIFO 队列中每个 buffer 的详细信息（slot、crop、transform、timestamp、scaling mode）
- 每个 slot 的状态（mActiveBuffers、mFreeBuffers、mFreeSlots 中的 buffer 信息）
- 生产者和消费者进程信息（读取 /proc/PID/cmdline）

#### 5.8 `validateConsistencyLocked()` (`BufferQueueCore.cpp:383-506`，仅在 DEBUG_ONLY_CODE 编译时启用）

严格的内部一致性校验。对每个 slot 检查：
- 不在任何两个集合中同时出现
- mUnusedSlots 中的 slot 必须是 FREE 状态且无 buffer
- mFreeSlots 中的 slot 必须是 FREE 状态且无 buffer
- mFreeBuffers 中的 slot 必须是 FREE 状态且有 buffer
- mActiveBuffers 中的 slot 必须是非 FREE 状态（或有 buffer 且正在分配中）
- 所有分配的 slot 总数 = getMaxBufferCountLocked()

### 6. Producer/Consumer 完整交互流程

#### 6.1 dequeueBuffer (生产者获取 buffer)

1. 检查 abort、connectedApi
2. 应用默认格式/尺寸/usage
3. `waitForFreeSlotThenRelock()` 循环：
   - 检查 dequeue 计数不超限
   - 优先从 `mFreeBuffers` 取（复用已分配 buffer）
   - 否则从 `mFreeSlots` 取（需分配新 buffer）
   - 如果队列中 buffer 太多则等待
   - 阻塞等待直到有 slot 可用（或在非阻塞模式下返回 WOULD_BLOCK）
4. 检查 buffer 是否需要 reallocation（尺寸/格式不匹配）
5. mSlots[found].mBufferState.dequeue()
6. 移入 mActiveBuffers
7. 如需要 reallocation：释放 mMutex → 创建新 GraphicBuffer → 重新获取锁
8. 返回 slot、fence、bufferAge

#### 6.2 queueBuffer (生产者提交 buffer) — 不在 BufferQueueCore 中，在 Producer 中

1. mBufferState.queue()（DEQUEUED → QUEUED）
2. 构建 BufferItem 存入 mQueue
3. 异步模式下，如果队列已有元素，释放旧 buffer
4. mFrameCounter++

#### 6.3 acquireBuffer (消费者获取 buffer)

1. 检查 acquired 计数不超限
2. 从 mQueue 头部取 buffer
3. 支持基于 expectedPresent 时间的 frame 跳过逻辑
4. mSlots[slot].mBufferState.acquire()（QUEUED → ACQUIRED）
5. 从 mQueue 移除
6. 如果 buffer 已被 acquire 过，mGraphicBuffer 设为 null（减少 binder 传输）

#### 6.4 releaseBuffer (消费者释放 buffer)

1. 验证 frameNumber 匹配（防止 stale release）
2. mSlots[slot].mBufferState.release()（ACQUIRED → FREE）
3. 设置 releaseFence
4. 从 mActiveBuffers 移除，加入 mFreeBuffers
5. 通知 mDequeueCondition（唤醒等待的生产者）

### 7. 特殊模式

#### 7.1 Shared Buffer Mode

由 mSharedBufferMode 控制。生产者和消费者使用同一个 buffer slot：
- 第一次 dequeue 的 slot 被设为 mSharedBufferSlot
- 该 buffer 的 mShared 标志为 true
- 消费者在 mQueue 为空时仍可通过 mSharedBufferCache 获取 buffer 数据
- mAutoRefresh 控制是否自动返回 shared buffer

#### 7.2 异步模式 (Async Mode)

- 每 queue 一个 buffer 即替换队列中原有的 buffer
- 需要额外 1 个 buffer slot（getMaxBufferCountLocked + 1）
- 使 dequeueBuffer 不会因消费者慢而阻塞

#### 7.3 Attach/Detach 机制

允许生产者和消费者跨 Binder 传递 buffer 所有权而不通过正常 dequeue/queue/acquire/release 流程：
- `attachBuffer`：将外部 buffer 放入 slot，状态变为 ACQUIRED 或 DEQUEUED
- `detachBuffer`：将 buffer 从 slot 取出，状态归零
- 通过 `mGenerationNumber` 防止跨 session 的 buffer 混用

## 关键代码位置

- `native/libs/gui/BufferQueueCore.cpp:87-143` — 构造函数，初始化所有 slot 和成员变量
- `native/libs/gui/BufferQueueCore.cpp:246-254` — `getMaxBufferCountLocked()`：核心 buffer 计数公式
- `native/libs/gui/BufferQueueCore.cpp:224-232` — `getMinUndequeuedBufferCountLocked()`：最小非 dequeued buffer 数
- `native/libs/gui/BufferQueueCore.cpp:256-279` — `clearBufferSlotLocked()`：重置单个 slot
- `native/libs/gui/BufferQueueCore.cpp:281-310` — `freeAllBuffersLocked()`：释放所有 buffer（producer disconnect 时）
- `native/libs/gui/BufferQueueCore.cpp:328-367` — `adjustAvailableSlotsLocked()`：动态调整可用 slot 数
- `native/libs/gui/BufferQueueCore.cpp:383-506` — `validateConsistencyLocked()`：DEBUG 模式下的内部一致性校验
- `native/libs/gui/include/gui/BufferQueueCore.h:149-378` — 全部成员变量声明及详细注释
- `native/libs/gui/include/gui/BufferSlot.h:35-174` — BufferState 五状态状态机定义
- `native/libs/gui/include/gui/BufferSlot.h:176-246` — BufferSlot 结构体定义
- `native/libs/ui/include/ui/BufferQueueDefs.h:25` — `NUM_BUFFER_SLOTS = 64`
- `native/libs/gui/BufferQueueProducer.cpp:426-731` — dequeueBuffer 完整实现
- `native/libs/gui/BufferQueueConsumer.cpp:84-324` — acquireBuffer 完整实现（含 frame drop 逻辑）
- `native/libs/gui/BufferQueueConsumer.cpp:480-574` — releaseBuffer 完整实现

## 相关概念

- BufferQueueProducer — 生产者侧实现（IGraphicBufferProducer）
- BufferQueueConsumer — 消费者侧实现（IGraphicBufferConsumer）
- BufferSlot — 单个 buffer slot（含 GraphicBuffer + 状态 + fence）
- BufferItem — FIFO 队列中的元素（含 crop/transform/timestamp/dataspace 等元数据）
- BufferState — 五状态状态机（FREE/DEQUEUED/QUEUED/ACQUIRED/SHARED）
- GraphicBuffer — gralloc 分配的图形缓冲区
- Fence — Android 同步栅栏（控制生产者-消费者之间的访问同步）
- NUM_BUFFER_SLOTS — 最大 slot 数 64
- Async Mode — 异步模式（mAsyncMode），单缓冲队列
- Shared Buffer Mode — 共享 buffer 模式（mSharedBufferMode）
- Attach/Detach — 跨 Binder 传递 buffer 所有权的旁路机制
- OccupancyTracker — 队列占用率追踪（判断双缓冲/三缓冲）

## 备注

- BufferQueueCore 本身不实现 IGraphicBufferProducer 或 IGraphicBufferConsumer 接口，仅作为共享状态容器
- Producer 和 Consumer 通过 `friend class` 直接访问 Core 的 private 成员，而非通过 getter/setter
- 所有公开方法是 `dumpState` 和条件编译下的 `notifyBufferReleased`，其余均为 private
- EGL fence（mEglFence）在 `BQ_GL_FENCE_CLEANUP` flag 启用时被移除，统一使用 native fence
- `BQ_EXTENDEDALLOCATE` flag 启用时支持附加分配参数（mAdditionalOptions）
- `BUFFER_RELEASE_CHANNEL` flag 启用时使用 `notifyBufferReleased()` 替代直接 `notify_all()`
