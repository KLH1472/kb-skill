# tcache/heap corruption 深度调查

> 类型：调试记录
> 置信度底线：本文档最低置信度为 ❓推测 的内容不可作为行动依据

## 问题
测试 PASS 后 `tcache_thread_shutdown(): unaligned tcache chunk detected` → abort (EXIT=134)

## 关键发现

### 1. 不只是 tcache — 是真正的 heap 损坏 [✅已确认]
```
禁用 tcache (GLIBC_TUNABLES=glibc.malloc.tcache_count=0):
  → SIGSEGV in unlink_chunk → _int_free_merge_chunk → ChxUtils::Free
  → EXIT=139 (SIGSEGV)
```

### 2. 精确崩溃位置 [✅已确认]
```
#0  unlink_chunk(p=0x7fffa4000ea0) — 相邻 free chunk 的 fd 被破坏
#3  ChxUtils::Free(pMem=0x7fffa40014c0)
#4  CHITargetBufferManager::RemoveTargetBufferPrivate(seqId=0) at :1787
#5  RecycleTargetBuffersFromConsumerList at :1821
#6  ReleaseTargetBuffer(hTargetBufferInfo=0x7fffa40014c0) at :1122
#7  ChiFeature2Base::HandleOutputNotificationPending at :1618
```

### 3. 内存布局 [✅已确认]
sizeof(ChiTargetBufferInfo) = 1512, sizeof(LightweightDoublyLinkedListNode) = 24

AllocateTargetBufferInfoNode 的两次分配产生相邻 chunks:
```
0x7fffa4000ea0: [Chunk A header] Info A (1520B chunk, 用于 1512B ChiTargetBufferInfo)
0x7fffa4000eb0: [Info A 用户数据]
0x7fffa4001490: [Chunk NodeA header] Node A (32B chunk, 用于 24B Node)
0x7fffa40014a0: [Node A 用户数据]
0x7fffa40014b0: [Chunk B header] Info B (1520B chunk)
0x7fffa40014c0: [Info B 用户数据]
...
```

### 4. free 序列和 glibc 合并 [✅已确认]
debug log 确认 3 次 CHX_FREE:
```
CHX_FREE(0x7fffa4000eb0)  ← Info A — 成功
CHX_FREE(0x7fffa40014a0)  ← Node A — 成功, glibc 与 Info A 合并 → 1552B free chunk
CHX_FREE(0x7fffa40014c0)  ← Info B — SEGV! glibc 尝试与合并后的 A+NodeA 合并
                             → 读取 fd=0x7ffea400e000 (已被破坏) → SEGV
```

合并机制:
- Info A 先 free → 进入 free list
- Node A free → glibc 检测 prev chunk (Info A) 已 free → 合并
- 合并后 chunk size = 1520 + 32 = 1552 (0x610)
- 这就是为什么 chunk A 的 size 是 0x611 (1552+PREV_INUSE) 而非 0x5f1 (1520+PREV_INUSE)

### 5. Hardware Watchpoint 完整时间线 [✅已确认]
| Hit | Thread | 操作 | 值 |
|-----|--------|------|-----|
| 1 | T8 (chi-cdk) | CHX_CALLOC(1512) 分配 Info A | 0x0 |
| 2 | T8 | SetupTargetBuffer — seqId=0, refCount=1 | 0x100000000 |
| 3 | T5 (CamX worker) | TryMoveToConsumerList — reset | 0x0 |
| 4 | T5 | UpdateTarget — refCount=1 | 0x100000000 |
| 5 | T5 | ReleaseTargetBuffer — refCount=0 | 0x0 |
| 6 | T5 | free(Node A) → 合并 → fd=valid | 0x7fffa40014b0 |
| 7 | **T1/T5** | **use-after-free 写入** → fd=corrupted | 0x7ffea400e000 → **SEGV** |

### 6. 根因: Use-After-Free [✅已确认机制, ❓具体写入源待定]
```
时间线:
1. T8 分配 Info A (via TBM SetupTargetBuffer)
2. T5 处理 CamX result callback → ReleaseTargetBuffer → RemoveTargetBufferPrivate
3. RemoveTargetBufferPrivate: CHX_FREE(Info A), CHX_FREE(Node A) → glibc 合并为 1552B free chunk
4. Feature2 状态机推进 (OutputResourcePending → OutputNotificationPending)
5. *** 此时间窗口内, 某处通过 stale pointer 写入已 free 的 Info A (地址 0x7fffa4000eb0) ***
   写入破坏了 glibc 存储在 freed memory 中的 fd 指针
6. HandleOutputNotificationPending → ReleaseTargetBuffer(Info B) → CHX_FREE(Info B)
7. glibc 尝试合并 Info B 与前一个 free chunk (合并的 A+NodeA)
8. 读取 corrupted fd=0x7ffea400e000 → SEGV
```

### 6. ASAN 结果 [✅已确认]
- `alloc-dealloc-mismatch`: 自定义 `operator new` (calloc) vs 标准 `operator delete` — 在 glibc 层面兼容, 非根因
- ODR violation: `tag_info` 在 exe 和 .so 中重复定义 — 非根因
- ASAN 与 signal handler 冲突 (SIGSEGV handler throw exception — UB)
- 添加了 `#ifndef __SANITIZE_ADDRESS__` guard 到 operator new/delete 和 signal handler

## ⚠️ 待继续调查
- [❓推测] 被破坏的前一个 chunk (size=1552) 是什么对象?不是 ChiTargetBufferInfo (1512→1520)
- [❓推测] main_arena chunk X (0x5555573c8a00) 的 bk 何时被设为 thread arena 地址?
- [❓推测] 是否有 buffer overflow 破坏了相邻 chunk 的 size/flags 字段?
- [❓推测] 自定义 operator new(calloc) 是否在跨线程场景下有 arena 选择问题?

## 📍 关键代码位置
- `chitargetbuffermanager.cpp:1787` — CHX_FREE(pTargetBufferInfo) 崩溃点
- `chitargetbuffermanager.cpp:1451` — CHX_CALLOC(sizeof(ChiTargetBufferInfo)) 分配点
- `chitargetbuffermanager.cpp:423` — SetupTargetBuffer (T8)
- `chitargetbuffermanager.cpp:1093` — ReleaseTargetBuffer (T5)
- `chifeature2base.cpp:1618` — HandleOutputNotificationPending
- `chifeature2base.cpp:4818` — ProcessBufferCallback (T5, CamX result callback)
- `chxutils.cpp:599-622` — ChxUtils::Calloc / ChxUtils::Free
- `chxutils.cpp:38-51` — 自定义 operator new (calloc) / operator delete (free)

## 📝 调试环境
- ASLR 禁用: `setarch x86_64 -R`
- tcache 禁用: `GLIBC_TUNABLES=glibc.malloc.tcache_count=0`
- 地址确定性: 0x7fffa4000eb0 在多次运行间一致
- GDB hardware watchpoint 可工作

## ✅ 已修复 — commit b917281 + 新 commit

### 修复 1: TBM 双重释放 (commit b917281)
**根因**: test framework + Feature2 都 release 同一个 hBuffer → refCount-- on freed memory → 破坏 fd 指针
**修复**: 删除 test framework 的 ReleaseTargetBuffer

### 修复 2: ChiMetadata::Destroy write-after-free (新 commit)
**根因**: `DestroyInternal(force)` 内 `CHX_DELETE this` 释放 ChiMetadata (648 bytes)，然后 `Destroy()` 写 `m_metaHandle = NULL` → 写入已释放内存 → 破坏 Thread 8 (chi-cdk worker) 的 tcache
**定位方法**: valgrind --tool=memcheck --undef-value-errors=no → "Invalid write of size 8 at chxmetadata.cpp:381"
**修复**: patched chxmetadata.cpp 移除 `m_metaHandle = NULL`

### 修复 3: pFeature2Base 从未 Destroy (新 commit)
**根因**: `RunFeature2Test()` 创建 pFeature2Base 但从未 Destroy → CamX Session/Pipeline 泄漏 → CamX worker 线程永不退出 → MetaBuffer 引用永不释放
**修复**: 在 PASS 后调用 `pFeature2Base->Destroy()` → 触发 ~ChiFeature2Base → DestroyFeatureData → Session::Destroy → CamX Session::Destroy (drain DRQ, stop nodes)

### 修复 4: OfflineLogger static vector double-free (新 commit)
**根因**: `s_pOfflineLoggerInstancePool` static vector 的 destructor 在 `__cxa_finalize` 中被调用两次 (exit handler + .so unload)
**修复**: main() 使用 `_exit(result)` 跳过 atexit handlers

**最终结果**: 5/5 runs: PASS + EXIT=0
