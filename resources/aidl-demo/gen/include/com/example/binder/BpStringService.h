// ═══════════════════════════════════════════════════════════════
// 由 AIDL 编译器生成，只读，勿改
// 对应: aidl/com/example/binder/IStringService.aidl
// ═══════════════════════════════════════════════════════════════
#pragma once

#include <binder/IBinder.h>
#include <binder/IInterface.h>
#include <utils/Errors.h>
#include <com/example/binder/IStringService.h>

namespace com {
namespace example {
namespace binder {

// ═══════════════════════════════════════════════════════════════
// BpStringService — Client 代理（方法声明，实现在 IStringService.cpp）
// 用户永远不直接 include——asInterface() 内部 new BpStringService
// ═══════════════════════════════════════════════════════════════
class BpStringService : public ::android::BpInterface<IStringService> {
public:
    explicit BpStringService(const ::android::sp<::android::IBinder>& impl);
    virtual ~BpStringService() = default;

    ::android::String16 upper(const ::android::String16& input) override;
    ::android::String16 lower(const ::android::String16& input) override;
};

}  // namespace binder
}  // namespace example
}  // namespace com
