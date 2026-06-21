// ═══════════════════════════════════════════════════════════════
// 由 AIDL 编译器生成，只读，勿改
// 对应: aidl/com/example/binder/IStringService.aidl
// ═══════════════════════════════════════════════════════════════
#pragma once

#include <binder/IBinder.h>
#include <binder/IInterface.h>
#include <utils/String16.h>

namespace com {
namespace example {
namespace binder {

// ═══════════════════════════════════════════════════════════════
// IStringService — 纯虚接口
// Client 和 Server 共享的方法签名
// 事务码在 BnStringService.h 中定义（static constexpr） + 事务码枚举
// ═══════════════════════════════════════════════════════════════
class IStringService : public ::android::IInterface {
public:
    DECLARE_META_INTERFACE(StringService);

    virtual ::android::String16 upper(const ::android::String16& input) = 0;
    virtual ::android::String16 lower(const ::android::String16& input) = 0;
};

// ═══════════════════════════════════════════════════════════════
// IStringServiceDefault — 默认实现（所有方法返回 UNKNOWN_TRANSACTION）
// ═══════════════════════════════════════════════════════════════
class IStringServiceDefault : public IStringService {
public:
    ::android::IBinder* onAsBinder() override {
        return nullptr;
    }
    ::android::String16 upper(const ::android::String16&) override {
        return ::android::String16("UNKNOWN");
    }
    ::android::String16 lower(const ::android::String16&) override {
        return ::android::String16("UNKNOWN");
    }
};

}  // namespace binder
}  // namespace example
}  // namespace com
