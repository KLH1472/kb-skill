// ═══════════════════════════════════════════════════════════════
// 由 AIDL 编译器生成，只读，勿改
// 对应: aidl/com/example/binder/IStringService.aidl
// ═══════════════════════════════════════════════════════════════
#pragma once

#include <binder/IInterface.h>
#include <binder/Delegate.h>
#include <com/example/binder/IStringService.h>

namespace com {
namespace example {
namespace binder {

// ═══════════════════════════════════════════════════════════════
// BnStringService — Server 桩
// 含 static constexpr 事务码（按 .aidl 方法声明顺序自动分配）
// ═══════════════════════════════════════════════════════════════
class BnStringService : public ::android::BnInterface<IStringService> {
public:
    static constexpr uint32_t TRANSACTION_upper = ::android::IBinder::FIRST_CALL_TRANSACTION + 0;
    static constexpr uint32_t TRANSACTION_lower = ::android::IBinder::FIRST_CALL_TRANSACTION + 1;

    explicit BnStringService();
    ::android::status_t onTransact(
            uint32_t code,
            const ::android::Parcel& data,
            ::android::Parcel* reply,
            uint32_t flags = 0) override;
};

// ═══════════════════════════════════════════════════════════════
// IStringServiceDelegator — 委托模式（代理到已存在的 IStringService 实例）
// ═══════════════════════════════════════════════════════════════
class IStringServiceDelegator : public BnStringService {
public:
    explicit IStringServiceDelegator(const ::android::sp<IStringService> &impl)
        : _aidl_delegate(impl) {}

    ::android::sp<IStringService> getImpl() { return _aidl_delegate; }

    ::android::String16 upper(const ::android::String16& input) override {
        return _aidl_delegate->upper(input);
    }
    ::android::String16 lower(const ::android::String16& input) override {
        return _aidl_delegate->lower(input);
    }
private:
    ::android::sp<IStringService> _aidl_delegate;
};

}  // namespace binder
}  // namespace example
}  // namespace com
