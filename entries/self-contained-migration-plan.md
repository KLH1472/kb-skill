# Self-Contained 迁移计划（精确拷贝方案）

> 类型：设计决策
> 状态：待执行
> 更新：2026-06-21 — 审计完成，修正提取 bug，444 个文件确认

## 一、目标

将 cmake 项目从依赖外部源码树 (`~/code/CAMX_SAIPAN_LA.UM.8.13.R1`) 改为完全自包含，消除所有外部路径引用。

## 二、精确文件数

| 类别 | 数量 | 来源 |
|------|------|------|
| .cpp 源文件 | 72 | CMakeLists.txt 编译列表 |
| .h 头文件 | 195 | 编译器 .d 依赖传递闭包 |
| .xml 代码生成器输入 | 3 | CMakeLists.txt add_custom_command |
| .xsd（sensor/） | 9 | ParameterParser 目录输入 |
| chromatix/XSD/（递归） | 165 | ParameterParser 目录输入 |
| **合计** | **444** | |

sensor/ 目录有 12 个文件（9 xsd + 3 h），其中 3 个 .h 已计入 195，无重复。

## 三、提取方法论

### 3.1 .cpp 提取（CMakeLists.txt → 72 个）

**方法**：从 CMakeLists.txt 提取所有 `${CAMX_ROOT}` 引用的 .cpp 文件。

```bash
# 直接引用 ${CAMX_ROOT} 的（camera.qcom.so）
grep -h '${CAMX_ROOT}.*\.cpp' camera.qcom.so/CMakeLists.txt | sed 's|.*\${CAMX_ROOT}/||; s/[[:space:]]*$//' | sort -u
# → 51 个

# 通过中间变量引用的（chifeature2test 用 ${CHIFEATURE2_DIR} 等）
# 需要手动展开变量，或直接用 .d 文件交叉验证
```

**交叉验证**：.d 文件中的 .cpp 应与 CMakeLists.txt 完全匹配

```bash
cd build && find . -name "*.cpp.o.d" | xargs cat | tr ' \\' '\n' \
  | grep '/CAMX_SAIPAN_LA.UM.8.13.R1/' | sed 's|.*/CAMX_SAIPAN_LA.UM.8.13.R1/||' \
  | grep '\.cpp$' | sort -u
# → 72 个，与 CMakeLists.txt 51 + 21 = 72 完全一致
```

**注意**：chifeature2test/CMakeLists.txt 不直接用 `${CAMX_ROOT}`，而是通过中间变量：
- `${CHIFEATURE2_DIR}` = `${CAMX_ROOT}/chi-cdk/core/chifeature2`
- `${F2FRAMEWORK_SRC}` = `${CAMX_ROOT}/chi-cdk/test/chifeature2testframework`
- `${OEM_FEATURE2_DIR}` = `${CAMX_ROOT}/chi-cdk/oem/qcom/feature2`
- `${CHIFRAMEWORK_DIR}` = `${CAMX_ROOT}/chi-cdk/core/chiframework`

### 3.2 .h 提取（.d 依赖文件 → 195 个）

**方法**：从编译器 .d 依赖文件提取所有指向外部树的 .h 文件。

```bash
cd build && find . -name "*.cpp.o.d" | xargs cat | tr ' \\' '\n' \
  | grep -v '^\s*$' | grep -v ':$' \
  | grep '/CAMX_SAIPAN_LA.UM.8.13.R1/' \
  | sed 's|.*/CAMX_SAIPAN_LA.UM.8.13.R1/||' \
  | grep -v '^build/' \
  | grep '\.h$' | sort -u
# → 195 个
```

**⚠️ 已知陷阱**：不能用 `grep -v '_cmake/'` 过滤本地文件！

原因：patched_srcs 的 .d 文件中，外部头文件路径格式为
`/home/.../CAMX_SAIPAN_LA.UM.8.13.R1_cmake/../CAMX_SAIPAN_LA.UM.8.13.R1/chi-cdk/...`
其中包含 `_cmake/` 子串，会被误过滤。

正确做法：用 `sed 's|.*/CAMX_SAIPAN_LA.UM.8.13.R1/||'` 从最后一个匹配点截取，而非过滤。

**首次提取 bug**：用 `grep -v '_cmake/'` 过滤后只得到 109 个，遗漏了 86 个（均为 patched_srcs 的传递依赖，含关键的 g_pipelines.h）。

### 3.3 .h 引用证据验证

**方法**：对每个 .h 文件，找到至少一个引用它的 .d 文件：

```bash
cd build
while IFS= read -r h; do
  d=$(find . -name "*.cpp.o.d" -exec grep -l "$h" {} \; | head -1)
  src=$(echo "$d" | sed 's|.*/CMakeFiles/[^/]*/||; s|\.o\.d$||')
  echo "$h ← $src"
done < /tmp/opencode/ext_h_fixed.txt
```

结果：195/195 全部有引用者，0 个 NOT_FOUND。

### 3.4 XML 提取（CMakeLists.txt add_custom_command → 3 个）

```bash
grep -n 'DEPENDS.*\.xml\|COMMAND.*\.xml' camera.qcom.so/CMakeLists.txt
```

引用点：
- `camera.qcom.so/CMakeLists.txt:39` — camxproperties.xml (props.pl 输入)
- `camera.qcom.so/CMakeLists.txt:63,77` — g_camxsettings.xml (settingsgenerator.pl 输入)
- `camera.qcom.so/CMakeLists.txt:66,78` — camxtitan17xsettings.xml (settingsgenerator.pl 输入)

### 3.5 XSD 提取（CMakeLists.txt add_custom_command → 2 个目录）

```bash
grep -n 'ParameterParser' camera.qcom.so/CMakeLists.txt
```

引用点：
- `camera.qcom.so/CMakeLists.txt:90` — `${CAMX_ROOT}/chi-cdk/api/sensor/` (9 xsd)
- `camera.qcom.so/CMakeLists.txt:104` — `${CAMX_ROOT}/chi-cdk/api/chromatix/XSD/` (165 files, 递归)

ParameterParser 按目录扫描，不是按单文件引用，必须整目录拷贝。

## 四、排除项及证据

| 文件 | 排除原因 | 证据 |
|------|---------|------|
| `chi-cdk/core/chiframework/chxfeature.cpp` | `CHIFRAMEWORK_REAL_SOURCES` 变量定义于 `chifeature2test/CMakeLists.txt:87` 但未被 `add_executable` 引用 | `ninja -n -j1 2>&1 \| grep chxfeature` → 空，无 .d 文件 |
| `ext/` 目录（ext/system, ext/hardware） | CMake include path 存在但 0 个 .h 被引用 | .d 文件中无 ext/ 路径（已被 stubs/ 替代） |
| patched_srcs 对应的外部原始 .cpp | patched 版本从本地编译，不从外部树编译 | 72 个外部 .cpp 中无任何 patched 文件 |

## 五、完整文件清单

### 5.1 源文件（72 个 .cpp）

**camera.qcom.so/camx_core — 51 个：**

| # | 文件 | CMakeLists.txt 行号 |
|---|------|-------------------|
| 1 | `camx/src/utils/camxdebug.cpp` | `camera.qcom.so/:124` |
| 2 | `camx/src/utils/camxdebugprint.cpp` | `camera.qcom.so/:125` |
| 3 | `camx/src/utils/camxhashmap.cpp` | `camera.qcom.so/:126` |
| 4 | `camx/src/utils/camximagedump.cpp` | `camera.qcom.so/:127` |
| 5 | `camx/src/utils/camxmemspy.cpp` | `camera.qcom.so/:128` |
| 6 | `camx/src/utils/camxnodeutils.cpp` | `camera.qcom.so/:129` |
| 7 | `camx/src/utils/camxstabilization.cpp` | `camera.qcom.so/:130` |
| 8 | `camx/src/utils/camxthreadcore.cpp` | `camera.qcom.so/:131` |
| 9 | `camx/src/utils/camxthreadjoblist.cpp` | `camera.qcom.so/:132` |
| 10 | `camx/src/utils/camxthreadjobregistry.cpp` | `camera.qcom.so/:133` |
| 11 | `camx/src/utils/camxthreadmanager.cpp` | `camera.qcom.so/:134` |
| 12 | `camx/src/utils/camxthreadqueue.cpp` | `camera.qcom.so/:135` |
| 13 | `camx/src/utils/camxtrace.cpp` | `camera.qcom.so/:136` |
| 14 | `camx/src/utils/camxtranslator.cpp` | `camera.qcom.so/:137` |
| 15 | `camx/src/utils/camxtypes.cpp` | `camera.qcom.so/:138` |
| 16 | `camx/src/utils/log/camxchiofflinelogger.cpp` | `camera.qcom.so/:139` |
| 17 | `camx/src/osutils/camxosutilslinuxembedded.cpp` | `camera.qcom.so/:144` |
| 18 | `camx/src/csl/camxcsl.cpp` | `camera.qcom.so/:148` |
| 19 | `camx/src/csl/camxcsljumptable.cpp` | `camera.qcom.so/:149` |
| 20 | `camx/src/csl/camxcdmdefs.cpp` | `camera.qcom.so/:150` |
| 21 | `camx/src/core/chi/camxchicomponent.cpp` | `camera.qcom.so/:155` |
| 22 | `camx/src/core/chi/camxchicontext.cpp` | `camera.qcom.so/:156` |
| 23 | `camx/src/core/halutils/camxhal3metadatautil.cpp` | `camera.qcom.so/:160` |
| 24 | `camx/src/core/halutils/camxhal3queue.cpp` | `camera.qcom.so/:161` |
| 25 | `camx/src/core/hal/camxhal3stream.cpp` | `camera.qcom.so/:162` |
| 26 | `camx/src/core/hal/camxhal3module.cpp` | `camera.qcom.so/:163` |
| 27 | `camx/src/core/camxcmdbuffer.cpp` | `camera.qcom.so/:180` |
| 28 | `camx/src/core/camxcmdbuffermanager.cpp` | `camera.qcom.so/:181` |
| 29 | `camx/src/core/camxdeferredrequestqueue.cpp` | `camera.qcom.so/:182` |
| 30 | `camx/src/core/camxhwcontext.cpp` | `camera.qcom.so/:183` |
| 31 | `camx/src/core/camxhwenvironment.cpp` | `camera.qcom.so/:184` |
| 32 | `camx/src/core/camxhwfactory.cpp` | `camera.qcom.so/:185` |
| 33 | `camx/src/core/camximagebuffer.cpp` | `camera.qcom.so/:186` |
| 34 | `camx/src/core/camximagebuffermanager.cpp` | `camera.qcom.so/:187` |
| 35 | `camx/src/core/camximagesensormoduledatamanager.cpp` | `camera.qcom.so/:188` |
| 36 | `camx/src/core/camxmempoolgroup.cpp` | `camera.qcom.so/:189` |
| 37 | `camx/src/core/camxmempoolmgr.cpp` | `camera.qcom.so/:190` |
| 38 | `camx/src/core/camxmetabuffer.cpp` | `camera.qcom.so/:191` |
| 39 | `camx/src/core/camxmetadatapool.cpp` | `camera.qcom.so/:192` |
| 40 | `camx/src/core/camxoverridesettingsfile.cpp` | `camera.qcom.so/:194` |
| 41 | `camx/src/core/camxpacket.cpp` | `camera.qcom.so/:195` |
| 42 | `camx/src/core/camxpacketbuilder.cpp` | `camera.qcom.so/:196` |
| 43 | `camx/src/core/camxpacketresource.cpp` | `camera.qcom.so/:197` |
| 44 | `camx/src/core/camxresourcemanager.cpp` | `camera.qcom.so/:199` |
| 45 | `camx/src/core/camxsession.cpp` | `camera.qcom.so/:200` |
| 46 | `camx/src/core/camxsettingsmanager.cpp` | `camera.qcom.so/:201` |
| 47 | `camx/src/core/camxstatsparser.cpp` | `camera.qcom.so/:202` |
| 48 | `camx/src/core/camxtest.cpp` | `camera.qcom.so/:203` |
| 49 | `camx/src/core/camxtuningdatamanager.cpp` | `camera.qcom.so/:204` |
| 50 | `camx/src/core/camxvendortags.cpp` | `camera.qcom.so/:205` |
| 51 | `camx/src/core/camxerrorinducer.cpp` | `camera.qcom.so/:206` |

**chifeature2test — 21 个：**

| # | 文件 | CMakeLists.txt 行号 |
|---|------|-------------------|
| 52 | `chi-cdk/test/chifeature2testframework/chibufferutils.cpp` | `chifeature2test/:31` |
| 53 | `chi-cdk/test/chifeature2testframework/chifeature2log.cpp` | `chifeature2test/:32` |
| 54 | `chi-cdk/test/chifeature2testframework/chifeature2test.cpp` | `chifeature2test/:33` |
| 55 | `chi-cdk/test/chifeature2testframework/cmdlineparser.cpp` | `chifeature2test/:36` |
| 56 | `chi-cdk/test/chifeature2testframework/metaconfigparser.cpp` | `chifeature2test/:41` |
| 57 | `chi-cdk/test/chifeature2testframework/spectraconfigparser.cpp` | `chifeature2test/:42` |
| 58 | `chi-cdk/test/chifeature2testframework/streamconfigparser.cpp` | `chifeature2test/:43` |
| 59 | `chi-cdk/test/chifeature2testframework/xmlparser.cpp` | `chifeature2test/:44` |
| 60 | `chi-cdk/core/chifeature2/chifeature2baserequestflow.cpp` | `chifeature2test/:54` |
| 61 | `chi-cdk/core/chifeature2/chifeature2featurepool.cpp` | `chifeature2test/:55` |
| 62 | `chi-cdk/core/chifeature2/chifeature2graph.cpp` | `chifeature2test/:56` |
| 63 | `chi-cdk/core/chifeature2/chifeature2graphmanager.cpp` | `chifeature2test/:57` |
| 64 | `chi-cdk/core/chifeature2/chifeature2requestobject.cpp` | `chifeature2test/:59` |
| 65 | `chi-cdk/core/chifeature2/chifeature2usecaserequestobject.cpp` | `chifeature2test/:60` |
| 66 | `chi-cdk/core/chifeature2/chifeature2wrapper.cpp` | `chifeature2test/:61` |
| 67 | `chi-cdk/core/chifeature2/chitargetbuffermanager.cpp` | `chifeature2test/:62` |
| 68 | `chi-cdk/oem/qcom/feature2/chifeature2generic/chifeature2generic.cpp` | `chifeature2test/:72` |
| 69 | `chi-cdk/oem/qcom/feature2/chifeature2graphselector/chifeature2bayer2yuvdescriptor.cpp` | `chifeature2test/:73` |
| 70 | `chi-cdk/oem/qcom/feature2/chifeature2graphselector/chifeature2bpsdescriptor.cpp` | `chifeature2test/:74` |
| 71 | `chi-cdk/oem/qcom/feature2/chifeature2graphselector/chifeature2ipedescriptor.cpp` | `chifeature2test/:75` |
| 72 | `chi-cdk/oem/qcom/feature2/chifeature2graphselector/chifeature2jpegdescriptor.cpp` | `chifeature2test/:76` |

### 5.2 头文件（195 个 .h）— 按目录分组

**camx/src/core/ — 49 个** ← 被引用者

```
camxactuatordata.h          ← camximagesensormoduledatamanager.cpp
camxcmdbuffer.h             ← camxpipeline.cpp (patched)
camxcmdbuffermanager.h      ← camxpipeline.cpp (patched)
camxcvpproperty.h           ← dummy_node.cpp
camxdeferredrequestqueue.h  ← dummy_node.cpp
camxeebindata.h             ← camxpipeline.cpp (patched)
camxerrorinducer.h          ← camxnode.cpp (patched)
camxfdproperty.h            ← dummy_node.cpp
camxflashdata.h             ← camximagesensormoduledatamanager.cpp
camxhwcontext.h             ← dummy_node.cpp
camxhwdefs.h                ← dummy_node.cpp
camxhwenvironment.h         ← dummy_node.cpp
camxhwfactory.h             ← dummy_node.cpp
camxifeproperty.h           ← dummy_node.cpp
camximagebuffer.h           ← dummy_node.cpp
camximagebuffermanager.h    ← dummy_node.cpp
camximagesensordata.h       ← camxpipeline.cpp (patched)
camximagesensormoduledata.h ← camxpipeline.cpp (patched)
camximagesensormoduledatamanager.h ← camxpipeline.cpp (patched)
camxipeproperty.h           ← hw_vendor_tags.cpp
camxjpegproperty.h          ← dummy_node.cpp
camxlrmeproperty.h          ← dummy_node.cpp
camxmempoolgroup.h          ← camxmempoolgroup.cpp
camxmempoolmgr.h            ← dummy_node.cpp
camxmetabuffer.h            ← dummy_node.cpp
camxmetadatapool.h          ← dummy_node.cpp
camxnode.h                  ← dummy_node.cpp
camxoisdata.h               ← camximagesensormoduledatamanager.cpp
camxoverridesettingsfile.h  ← dummy_node.cpp
camxoverridesettingsstore.h ← dummy_node.cpp
camxpacket.h                ← camxpipeline.cpp (patched)
camxpacketbuilder.h         ← camxpacketbuilder.cpp
camxpacketresource.h        ← camxpipeline.cpp (patched)
camxpdafdata.h              ← camximagesensormoduledatamanager.cpp
camxpipeline.h              ← dummy_node.cpp
camxpropertyblob.h          ← dummy_node.cpp
camxpropertydefs.h          ← dummy_node.cpp
camxresourcemanager.h       ← dummy_node.cpp
camxsdksensordriver.h       ← camxpipeline.cpp (patched)
camxsensorproperty.h        ← dummy_node.cpp
camxsensorsdkcommon.h       ← camxpipeline.cpp (patched)
camxsession.h               ← dummy_node.cpp
camxsettingsmanager.h       ← dummy_node.cpp
camxstaticcaps.h            ← dummy_node.cpp
camxstatsinternalproperty.h ← dummy_node.cpp
camxstatsparser.h           ← dummy_node.cpp
camxtest.h                  ← camxtest.cpp
camxtuningdatamanager.h     ← camxtuningdatamanager.cpp
camxvendortags.h            ← dummy_node.cpp
```

**camx/src/core/chi/ — 6 个**

```
camxchi.h          ← dummy_node.cpp
camxchicomponent.h ← camxhwenvironment.cpp
camxchicontext.h   ← dummy_node.cpp
camxchidefs.h      ← dummy_node.cpp
camxchisession.h   ← camxchisession.cpp (patched)
camxchitypes.h     ← dummy_node.cpp
```

**camx/src/core/hal/ — 12 个**

```
camxcommontypes.h        ← dummy_node.cpp
camxentry.h              ← camxhal3module.cpp
camxhal3.h               ← camx_runtime_stubs.cpp
camxhal3defs.h           ← dummy_node.cpp
camxhal3metadatatags.h   ← dummy_node.cpp
camxhal3metadatatagtypes.h ← dummy_node.cpp
camxhal3module.h         ← camxnode.cpp (patched)
camxhal3stream.h         ← dummy_node.cpp
camxhal3types.h          ← dummy_node.cpp
camxhaldevice.h          ← camxchisession.cpp (patched)
camxpresilmem.h          ← feature2buffermanager.cpp (patched)
camxthermalmanager.h     ← camxnode.cpp (patched)
```

**camx/src/core/halutils/ — 3 个**

```
camxhal3defaultrequest.h ← camx_runtime_stubs.cpp
camxhal3metadatautil.h   ← dummy_node.cpp
camxhal3queue.h          ← camxsession.cpp
```

**camx/src/core/ncs/ — 4 个**

```
camxncsintf.h      ← camxpipeline.cpp (patched)
camxncssensor.h    ← camxpipeline.cpp (patched)
camxncssensordata.h ← camxpipeline.cpp (patched)
camxncsservice.h   ← camxpipeline.cpp (patched)
```

**camx/src/core/oem/ — 1 个**

```
camxcustomization.h ← camx_runtime_stubs.cpp
```

**camx/src/csl/ — 7 个**

```
camxcdmdefs.h        ← camxpacketbuilder.cpp
camxcsl.h            ← dummy_node.cpp
camxcslispdefs.h     ← camxpacket.cpp
camxcsljumptable.h   ← csl_mock.cpp
camxcslresourcedefs.h ← camxnode.cpp (patched)
camxcslsensordefs.h  ← dummy_node.cpp
camxpacketdefs.h     ← camxpipeline.cpp (patched)
```

**camx/src/hwl/titan17x/ — 2 个**

```
camxtitan17xdefs.h           ← camxnode.cpp (patched)
camxtitan17xsettingsmanager.h ← g_camxtitan17xsettings.cpp (generated)
```

**camx/src/mapperutils/formatmapper/ — 3 个**

```
camxdisplayconfig.h    ← camx_runtime_stubs.cpp
camxformats.h          ← dummy_node.cpp
camximageformatutils.h ← dummy_node.cpp
```

**camx/src/osutils/ — 2 个**

```
camxmem.h     ← dummy_node.cpp
camxosutils.h ← dummy_node.cpp
```

**camx/src/utils/ — 21 个**

```
camxatomic.h          ← dummy_node.cpp
camxdebug.h           ← dummy_node.cpp
camxdebugprint.h      ← dummy_node.cpp
camxdefs.h            ← dummy_node.cpp
camxhashmap.h         ← dummy_node.cpp
camximagedump.h       ← camxnode.cpp (patched)
camxincs.h            ← dummy_node.cpp (force-include)
camxlist.h            ← dummy_node.cpp
camxmemspy.h          ← camxchisession.cpp (patched)
camxnodeutils.h       ← camxnode.cpp (patched)
camxstabilization.h   ← camxstabilization.cpp
camxthreadcommon.h    ← dummy_node.cpp
camxthreadcore.h      ← camxthreadcore.cpp
camxthreadjoblist.h   ← camxthreadqueue.cpp
camxthreadjobregistry.h ← camxthreadqueue.cpp
camxthreadmanager.h   ← dummy_node.cpp
camxthreadqueue.h     ← camxthreadqueue.cpp
camxtrace.h           ← dummy_node.cpp
camxtranslator.h      ← camxtranslator.cpp
camxtypes.h           ← chimetadatautil.cpp (patched)
camxutils.h           ← dummy_node.cpp
```

**chi-cdk/api/common/ — 7 个**

```
camxcdktypes.h    ← chimetadatautil.cpp (patched)
chi.h             ← chimetadatautil.cpp (patched)
chicommon.h       ← chimetadatautil.cpp (patched)
chicommontypes.h  ← chimetadatautil.cpp (patched)
chioverride.h     ← chimetadatautil.cpp (patched)
chituningmodeparam.h ← chimetadatautil.cpp (patched)
chivendortag.h    ← dummy_node.cpp
```

**chi-cdk/api/fd/ — 1 个**

```
chifdproperty.h ← dummy_node.cpp
```

**chi-cdk/api/isp/ — 7 个**

```
chieisdefs.h        ← dummy_node.cpp
chiifedefs.h        ← dummy_node.cpp
chiipedefs.h        ← dummy_node.cpp
chiiqmoduledefines.h ← hw_vendor_tags.cpp
chiiqmodulesettings.h ← hw_vendor_tags.cpp
chiisphvxdefs.h     ← dummy_node.cpp
chiispstatsdefs.h   ← chifeature2base.cpp (patched)
```

**chi-cdk/api/node/ — 2 个**

```
camxchinodeutil.h ← dummy_node.cpp
chinode.h         ← dummy_node.cpp
```

**chi-cdk/api/pdlib/ — 2 个**

```
chipdlibcommon.h    ← feature2testcase.cpp (patched)
chipdlibinterface.h ← dummy_node.cpp
```

**chi-cdk/api/sensor/ — 3 个**（另有 9 个 .xsd 见 5.4）

```
camxeebinlib.h       ← camxpipeline.cpp (patched)
camxeepromdriverapi.h ← camxpipeline.cpp (patched)
camxsensordriverapi.h ← camxpipeline.cpp (patched)
```

**chi-cdk/api/stats/ — 14 个**

```
chiaecinterface.h         ← dummy_node.cpp
chiafcommon.h             ← dummy_node.cpp
chiafdinterface.h         ← dummy_node.cpp
chiafinterface.h          ← dummy_node.cpp
chiasdinterface.h         ← dummy_node.cpp
chiawbinterface.h         ← dummy_node.cpp
chihafalgorithminterface.h ← dummy_node.cpp
chihistalgointerface.h    ← dummy_node.cpp
chistatsalgo.h            ← dummy_node.cpp
chistatsdebug.h           ← dummy_node.cpp
chistatsinterfacedefs.h   ← feature2testcase.cpp (patched)
chistatsproperty.h        ← feature2testcase.cpp (patched)
chistatspropertydefines.h ← feature2offlinetest.cpp (patched)
chitrackerinterface.h     ← dummy_node.cpp
```

**chi-cdk/api/utils/ — 6 个**

```
camxchiofflinelogger.h      ← camxpipeline.cpp (patched)
camxchiofflineloggercommon.h ← chimetadatautil.cpp (patched)
cdkutils.h                  ← chimetadatautil.cpp (patched)
chibinarylog.h              ← camxpipeline.cpp (patched)
chiofflineloggerinterface.h ← chimetadatautil.cpp (patched)
chxdebugprint.h             ← chimetadatautil.cpp (patched)
```

**chi-cdk/core/chifeature2/ — 12 个**

```
chifeature2base.h                ← feature2offlinetest.cpp (patched)
chifeature2descriptors.h         ← chifeature2featurepool.cpp
chifeature2featurepool.h         ← chifeature2generic.cpp
chifeature2graph.h               ← chifeature2generic.cpp
chifeature2graphmanager.h        ← chifeature2featurepool.cpp
chifeature2requestobject.h       ← feature2offlinetest.cpp (patched)
chifeature2types.h               ← chifeature2multistagedescriptor.cpp (patched)
chifeature2usecaserequestobject.h ← feature2offlinetest.cpp (patched)
chifeature2utils.h               ← chifeature2base.cpp (patched)
chifeature2wrapper.h             ← chifeature2wrapper.cpp
chitargetbuffermanager.h         ← chimetadatautil.cpp (patched)
chithreadmanager.h               ← feature2offlinetest.cpp (patched)
```

**chi-cdk/core/chiframework/ — 3 个**

```
chxextensionmodule.h ← chxmetadata.cpp (local chiutils)
chxfeature.h         ← chiframework_stubs.cpp
chxusecase.h         ← chiframework_stubs.cpp
```

**chi-cdk/core/chiusecase/ — 1 个**

```
chxusecaseutils.h ← chxutils.cpp (local chiutils)
```

**chi-cdk/core/chiutils/ — 3 个**

```
chxdefs.h     ← chimetadatautil.cpp (patched)
chxmetadata.h ← chimetadatautil.cpp (patched)
chxutils.h    ← chimetadatautil.cpp (patched)
```

**chi-cdk/core/lib/common/ — 1 个**

```
g_pipelines.h ← chimetadatautil.cpp (patched)  [39130行, 预生成]
```

**chi-cdk/oem/qcom/feature2/chifeature2generic/ — 1 个**

```
chifeature2generic.h ← feature2offlinetest.cpp (patched)
```

**chi-cdk/oem/qcom/feature2/chifeature2graphselector/ — 3 个**

```
chifeature2graphdescriptors.h  ← chifeature2ipedescriptor.cpp
chifeature2graphselector.h     ← chifeature2ipedescriptor.cpp
chifeature2graphselectoroem.h  ← chifeature2featurepool.cpp
```

**chi-cdk/test/chifeature2testframework/ — 19 个**

```
bayer2yuvinputdata.h   ← feature2offlinetest.cpp (patched)
bpsinputdata.h         ← feature2offlinetest.cpp (patched)
chibufferutils.h       ← chibufferutils.cpp
chifeature2interface.h ← feature2offlinetest.cpp (patched)
chifeature2log.h       ← chimetadatautil.cpp (patched)
chifeature2test.h      ← chimetadatautil.cpp (patched)
chimetadatautil.h      ← feature2offlinetest.cpp (patched)
chimodule.h            ← feature2offlinetest.cpp (patched)
cmdlineparser.h        ← chimetadatautil.cpp (patched)
feature2buffermanager.h ← feature2offlinetest.cpp (patched)
feature2offlinetest.h  ← feature2offlinetest.cpp (patched)
feature2testcase.h     ← feature2offlinetest.cpp (patched)
genericbuffermanager.h ← feature2offlinetest.cpp (patched)
ipeinputdata.h         ← feature2offlinetest.cpp (patched)
metaconfigparser.h     ← feature2testcase.cpp (patched)
spectraconfigparser.h  ← feature2buffermanager.cpp (patched)
streamconfigparser.h   ← feature2testcase.cpp (patched)
xmlparser.h            ← feature2buffermanager.cpp (patched)
yuv2jpeginputdata.h    ← feature2offlinetest.cpp (patched)
```

### 5.3 XML 代码生成器输入（3 个）

| 文件 | CMakeLists.txt 证据 |
|------|-------------------|
| `camx/src/core/camxproperties.xml` | `camera.qcom.so/:39` DEPENDS |
| `camx/src/settings/g_camxsettings.xml` | `camera.qcom.so/:63,77` symlink+DEPENDS |
| `camx/src/hwl/titan17x/camxtitan17xsettings.xml` | `camera.qcom.so/:66,78` symlink+DEPENDS |

### 5.4 XSD 代码生成器输入（174 个）

**chi-cdk/api/sensor/ — 9 个 .xsd**

| CMakeLists.txt 证据 | 文件 |
|-------------------|------|
| `camera.qcom.so/:90` | camxactuatordriver.xsd, camxeebindriver.xsd, camxeepromdriver.xsd, camxflashdriver.xsd, camxmoduleconfig.xsd, camxoisdriver.xsd, camxpdafconfig.xsd, camxsensorcommon.xsd, camxsensordriver.xsd |

**chi-cdk/api/chromatix/XSD/ — 165 个文件（递归）**

| CMakeLists.txt 证据 | 分布 |
|-------------------|------|
| `camera.qcom.so/:104` | 1 顶层 .xsd + 1 xsd_version.txt + common/ (1) + isp/ (82) + stats/ (80) |

ParameterParser 按目录递归扫描，必须整目录拷贝。

## 六、Patch 合并

15 个 patched 文件在拷贝后替换对应外部原始文件。

**camx_patched_srcs/ — 4 个** → 目标路径 `camx/src/`

| Patched 文件 | 替换目标 |
|-------------|---------|
| camxatomic.cpp | `camx/src/utils/camxatomic.cpp` |
| camxchisession.cpp | `camx/src/core/chi/camxchisession.cpp` |
| camxnode.cpp | `camx/src/core/camxnode.cpp` |
| camxpipeline.cpp | `camx/src/core/camxpipeline.cpp` |

注意：这 4 个文件的外部原始版本不在 72 个外部 .cpp 中（patched 版本直接编译，不需要先拷贝原始版本再覆盖）。

**chifeature2test/patched_srcs/ — 11 个** → 目标路径 `chi-cdk/`

| Patched 文件 | 替换目标 |
|-------------|---------|
| chifeature2base.cpp | `chi-cdk/core/chifeature2/chifeature2base.cpp` |
| chifeature2multistagedescriptor.cpp | `chi-cdk/oem/qcom/feature2/chifeature2graphselector/chifeature2multistagedescriptor.cpp` |
| chifeature2testmain.cpp | `chi-cdk/test/chifeature2testframework/chifeature2testmain.cpp` |
| chimetadatautil.cpp | `chi-cdk/test/chifeature2testframework/chimetadatautil.cpp` |
| chimodule.cpp | `chi-cdk/test/chifeature2testframework/chimodule.cpp` |
| chithreadmanager.cpp | `chi-cdk/core/chifeature2/chithreadmanager.cpp` |
| chxmetadata.cpp | `chi-cdk/core/chiutils/chxmetadata.cpp` |
| feature2buffermanager.cpp | `chi-cdk/test/chifeature2testframework/feature2buffermanager.cpp` |
| feature2offlinetest.cpp | `chi-cdk/test/chifeature2testframework/feature2offlinetest.cpp` |
| feature2testcase.cpp | `chi-cdk/test/chifeature2testframework/feature2testcase.cpp` |
| genericbuffermanager.cpp | `chi-cdk/test/chifeature2testframework/genericbuffermanager.cpp` |

## 七、一键验证命令

迁移完成后运行以下命令验证完整性：

```bash
# 1. 无外部路径残留
grep -rn 'CAMX_SAIPAN_LA.UM.8.13.R1' CMakeLists.txt */CMakeLists.txt

# 2. 重新提取 .d 依赖，确认无外部引用
cd build && cmake .. && ninja
find . -name "*.cpp.o.d" | xargs cat | tr ' \\' '\n' \
  | grep 'CAMX_SAIPAN_LA.UM.8.13.R1' | grep -v '_cmake/' | head
# 应为空

# 3. 测试通过
./chifeature2test -t Feature2OfflineTest -f 1  # 重复 10 次

# 4. static_assert tag 校验
ninja  # validate_metadata_tags.cpp 编译通过即验证通过
```

## 八、平台关系（已验证）

```
SAIPAN = bitra = lito 子变体
Pipeline 拓扑: lito (g_lito_usecase.xml)
Feature2 图选择: bitra (chifeature2graphselectorbitra)
g_pipelines.h: common_usecase.xml + g_lito_usecase.xml → 0 diff 验证通过
```

## 九、顺搭 Bug 修复

| Bug | 文件 | 修复 |
|-----|------|------|
| DestroyFence delete 栈地址 + CreateFence 内存泄漏 | `chi-cdk/test/` 下 3 个文件 | 删除 delete 块 + CF2_NEW→栈变量 |
| uint16_t 溢出 | `nativechitest/.../camera3common.h:43` | `uint16_t`→`uint32_t` |
