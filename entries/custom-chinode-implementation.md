# 自定义 ChiNode 实现全景分析

## 问题背景
用户要系统理解实现一个自定义 ChiNode 需要做哪些修改，包括需要重写的回调方法、源码文件、构建系统、XML 拓扑配置、注册加载机制等。以 chinode.h:1091 的 ChiNodeCallbacks 为切入点进行全面分析。

## 分析结论

### 总体架构：6 层修改面

| # | 层面 | 文件类型 | 数量 |
|---|------|----------|------|
| 1 | 节点源码 | `.h` + `.cpp` | 2 |
| 2 | 构建系统 | `Android.mk` | 1 |
| 3 | XML 拓扑 | Usecase XML | 修改现有 |
| 4 | Port 定义 | `*.xml` (可选) | 如需新端口 |
| 5 | So 部署 | 编译产物 → `/vendor/lib64/camera/components/` | 1 |
| 6 | 框架侧 | 无需修改（自动发现机制） | 0 |

### ChiNodeCallbacks — 16 个回调

来自 `chinode.h:1091-1116`：

**必备（Mandatory）:**
- `pGetCapabilities` (PFNNODEGETCAPS): 返回 nodeCapsMask，声明能力位
- `pCreate` (PFNNODECREATE): new 节点对象 → Initialize → 回填 phNodeSession
- `pDestroy` (PFNNODEDESTROY): delete 节点对象，释放 session 级资源
- `pQueryBufferInfo` (PFNNODEQUERYBUFFERINFO): Buffer 协商，告知框架所需输入 buffer 的 min/max/optimal 尺寸和格式
- `pSetBufferInfo` (PFNNODESETBUFFERINFO): 接收最终确定的 buffer 属性
- `pProcessRequest` (PFNNODEPROCREQUEST): 核心处理 — 检查依赖 → 处理输入 → 产生输出 → 发布 metadata
- `pChiNodeSetNodeInterface` (PFCHINODESETNODEINTERFACE): 接收 ChiNodeInterface 函数表
- `pQueryMetadataPublishList` (PFNNODEQUERYMETADATAPUBLISHLIST): 声明发布的 metadata tag 列表

**推荐实现:**
- `pQueryVendorTag`: 若有自定义 vendor tag，必须实现
- `pFlushRequest` (PFNNODEFLUSH): 清理 per-request 资源
- `pGetFlushResponse` (PFNNODEFLUSHRESPONSEINFO): 返回 worst-case 响应时间
- `pPrepareStreamOn`, `pOnStreamOn`, `pOnStreamOff`: 流状态通知
- `pPostPipelineCreate`: Pipeline 创建完成通知
- `pFillHwdata` (PFNNODEFILLPERREQUESTHWDATA): HW 加速节点填 command buffer

### ChiNodeEntry — 唯一导出符号

`chinode.h:1173`: `CDK_VISIBILITY_PUBLIC VOID ChiNodeEntry(CHINODECALLBACKS* pNodeCallbacks)`
- 每个 .so 必须导出此符号
- 框架通过 dlopen + dlsym("ChiNodeEntry") 自动发现
- 函数内填充 pNodeCallbacks 结构体各字段

### 节点类设计模式

基参考类（memcpy/camxchinodememcpy.h:23-188）:
- 全局变量 `g_ChiNodeInterface` 保存框架回调接口
- 全局变量 `g_vendorTagBase` 保存 vendor tag 基地址
- pCreate 中 `new` + `Initialize` + 回填 `pCreateInfo->phNodeSession`
- pDestroy 中 `static_cast<MyNode*>(pDestroyInfo->hNodeSession)` 然后 `delete`

### ProcessRequest 核心流程

```
ProcessRequest(pInfo)
├── CheckDependency(pInfo)
│   ├── 尝试获取依赖的 metadata tags
│   ├── 若未发布 → 设置 pDependency (properties[], offsets[], processSequenceId)
│   │               返回 → 等框架下次回调
│   └── 全部已发布 → 继续处理
├── for each output port:
│   ├── 检查 flush 状态
│   ├── 检查 bypass 逻辑
│   ├── 实际处理（含 cacheOps invalidate/clean）
│   └── 更新 metadata
└── return CDKResultSuccess
```

### Buffer 协商两步走

1. `pQueryBufferInfo` — 反向传播（Sink→Source）：节点返回对每个 input port 的需求
2. `pSetBufferInfo` — 正向传播（Source→Sink）：接收最终 buffer 属性
3. 最简实现：`ChiNodeUtils::DefaultBufferNegotiation(pQueryBufferInfo)` 走 pass-through

### XML 拓扑配置

```xml
<Node>
  <NodeName>ChiNode</NodeName>
  <NodeId>255</NodeId>  <!-- ChiExternalNode = 255 -->
  <NodeInstance>ChiNodeInstanceName0</NodeInstance>
  <NodeInstanceId>0</NodeInstanceId>
  <NodeProperty>
    <NodePropertyName>NodePropertyCustomLib</NodePropertyName>
    <NodePropertyId>1</NodePropertyId>
    <NodePropertyDataType>STRING</NodePropertyDataType>
    <NodePropertyValue>com.qti.node.memcpy</NodePropertyValue>
  </NodeProperty>
</Node>
```

### 自动加载与注册

无需手动注册。流程:
1. `HwEnvironment::Initialize()` → `ProbeChiComponents()` (camxchicomponent.cpp:434)
2. 扫描 `/vendor/lib64/camera/components/` 下匹配 `*node*.so` 的文件
3. dlopen + dlsym("ChiNodeEntry") → 调用入口填充回调表
4. 回调表存入 `m_externalComponent[]` 表
5. Pipeline 创建时 `Titan17xFactory::HwCreateNode()` case `ChiExternalNode` → `ChiNodeWrapper::Create()` (camxchinodewrapper.cpp:47)
6. ChiNodeWrapper 读取 XML 中的 `NodePropertyCustomLib` → `SearchExternalComponent()` → memcpy 回调表 → 最后调 `pCreate`

### 构建系统

Android.mk 模式 (memcpy/build/android/Android.mk):
- LOCAL_MODULE := com.qti.node.<name>
- LOCAL_SHARED_LIBRARIES 必须包含 libcom.qti.chinodeutils
- 产物 .so 部署到 /vendor/lib64/camera/components/

### ChiNodeInterface — 节点调用框架的 20+ 函数

chinode.h:1119-1155: pGetMetadata, pSetMetadata, pGetVendorTagBase, pProcessRequestDone, pProcessMetadataDone, pCreateFence, pReleaseFence, pSignalFence, pWaitFenceAsync, pCacheOps, pGetFlushInfo, pCreateBufferManager, pDestroyBufferManager, pBufferManagerGetImageBuffer, pBufferManagerReleaseReference, pGetStaticMetadata, pGetMultiCamDynamicMetaByCamId, pGetSupportedPSMetadataList, pGetPSMetadata, pSetPSMetadata, pPublishPSMetadata, pIsPSMetadataPublished

### 现有参考实现

共 14 个自定义 ChiNode，按复杂度排列:
- 最简: MemCpy (1020行), SwCac (~300行), DummyRTB/Stich/SAT (~400行)
- 中等: Remosaic (~540行), FCV (~530行), Depth (~360行), CustomHwNode (~520行, 含CSL)
- 复杂: GPU (~3240行, OpenCL), Dewarp (~2140行, OpenGL ES/EIS), EISv2/v3 (~875行)

路径规律:
- `chi-cdk/oem/qcom/node/<name>/camxchinode<name>.cpp` — 11个节点
- `camx/src/swl/<name>/camxchinode<name>.cpp` — 3个SWL节点

## 关键代码位置
- `chi-cdk/api/node/chinode.h:1091` — `ChiNodeCallbacks` 结构体定义（16 个回调字段）
- `chi-cdk/api/node/chinode.h:1119` — `ChiNodeInterface` 结构体定义（框架提供给节点的接口）
- `chi-cdk/api/node/chinode.h:1173` — `ChiNodeEntry()` 函数声明（唯一导出符号）
- `chi-cdk/api/common/chi.h:96` — `NodePropertyCustomLib = 1`
- `camx/src/utils/camxdefs.h:52` — `ChiExternalNode = 255`
- `chi-cdk/api/node/camxchinodeutil.h:111` — `ChiNodeUtils` 工具类（DefaultBufferNegotiation, GetMetaData 等）
- `camx/src/core/chi/camxchinodewrapper.cpp:47` — `ChiNodeWrapper::Create()` 桥接入口
- `camx/src/core/chi/camxchicomponent.cpp:560` — `ProbeChiComponents()` 自动发现机制
- `camx/src/core/chi/camxchinodewrapper.cpp:1773-1818` — 调用 `pCreate` + `pChiNodeSetNodeInterface`
- `camx/src/core/chi/camxchinodewrapper.cpp:1868-2069` — Buffer 协商流程（pQueryBufferInfo → 回传）
- `chi-cdk/oem/qcom/node/memcpy/camxchinodememcpy.cpp` — 最简完整参考实现（1020行）
- `chi-cdk/oem/qcom/node/memcpy/camxchinodememcpy.h` — 节点类参考声明
- `chi-cdk/oem/qcom/node/memcpy/build/android/Android.mk` — 构建系统参考
- `chi-cdk/oem/qcom/topology/sm6150/sm6150_usecase.xml:23063` — XML 配置参考

## 相关概念
- ChiNode: 自定义处理节点的接口规范
- ChiNodeCallbacks: 框架调用节点的回调函数表
- ChiNodeInterface: 节点调用框架的函数表
- ChiNodeWrapper: 框架内部桥接层，连接 CamX Pipeline 和外部 ChiNode
- NodePropertyCustomLib: XML 属性标识自定义节点 .so 文件名
- ChiExternalNode (255): 自定义节点的 nodeId 常量
- ChiNodeUtils: 节点工具类（元数据获取/设置、buffer 协商默认实现等）
- probe/scan: 自动扫描 /vendor/lib64/camera/components/ 发现 *.so
- Buffer Negotiation: 反向传播需求+正向传播结果的 buffer 协商机制
- Dependency: 节点通过 processSequenceId + properties/offsets 声明依赖，框架满足后重新回调
- Vendor Tag: 自定义 metadata 标签，需定义 section/name/type

## 备注
- 全局变量 g_ChiNodeInterface 用于缓存框架接口（非线程安全，取决于 framework 的调用时序是否串行化）
- pPostPipelineCreate 取代已废弃的 pPipelineCreated
- memcpy 节点的 CheckDependency 展示了依赖声明的标准用法（设置 processSequenceId=1, hasIOBufferAvailabilityDependency=TRUE）
- 若节点需要 CSL 硬件访问，需要实现 pFillHwdata 并在 CreateInfo 中设置 requireCSLAccess
- 建议从 memcpy 或 swcac 直接复制代码骨架，修改核心处理逻辑
