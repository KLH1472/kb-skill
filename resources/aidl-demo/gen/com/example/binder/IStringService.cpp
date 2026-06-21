// ═══════════════════════════════════════════════════════════════
// 由 AIDL 编译器生成，只读，勿改
// 对应: aidl/com/example/binder/IStringService.aidl
//
// 编译进: com.example.binder-cpp 库 (Server 和 Client 都链接)
//
// 包含:
//   ① IMPLEMENT_META_INTERFACE — DESCRIPTOR + getInterfaceDescriptor + asInterface
//   ② BpStringService 方法体 — Client 侧 Parcel 编解码 + transact
//   ③ BnStringService::onTransact — Server 侧 Parcel 拆包 → 调虚函数 → 打包
// ═══════════════════════════════════════════════════════════════

#include <com/example/binder/IStringService.h>
#include <com/example/binder/BpStringService.h>

// ═══════════════════════════════════════════════════════════════
// ① 接口元数据
// ═══════════════════════════════════════════════════════════════
IMPLEMENT_META_INTERFACE(StringService, "com.example.binder.IStringService");

// ── 展开后等价于 ────────────────────────────────────────────
//   const String16 IStringService::descriptor("com.example.binder.IStringService");
//   const String16& IStringService::getInterfaceDescriptor() const { return descriptor; }
//   sp<IStringService> IStringService::asInterface(const sp<IBinder>& obj) {
//       sp<IStringService> intr = queryLocalInterface(descriptor);
//       if (intr == nullptr)
//           intr = sp<BpStringService>::make(obj);  // ★ 构造 Bp
//       return intr;
//   }

#include <com/example/binder/BpStringService.h>
#include <com/example/binder/BnStringService.h>
#include <binder/Parcel.h>

namespace com {
namespace example {
namespace binder {

// ═══════════════════════════════════════════════════════════════
// ② BpStringService — Client 代理方法体
//   每个 Bp 方法 = writeXxx(入参) + transact(code) + readXxx(出参)
//   .aidl 写完那一刻，下面所有代码 100% 确定，零业务逻辑
// ═══════════════════════════════════════════════════════════════

BpStringService::BpStringService(const ::android::sp<::android::IBinder>& impl)
    : ::android::BpInterface<IStringService>(impl) {
}

::android::String16 BpStringService::upper(const ::android::String16& input) {
    ::android::Parcel data;
    ::android::Parcel reply;

    data.writeInterfaceToken(IStringService::getInterfaceDescriptor());
    data.writeString16(input);                                // 序列化入参
    remote()->transact(BnStringService::TRANSACTION_upper,    // RPC 调用
                       data, &reply);
    ::android::String16 result;
    reply.readString16(&result);                              // 反序列化出参
    return result;
}

::android::String16 BpStringService::lower(const ::android::String16& input) {
    ::android::Parcel data;
    ::android::Parcel reply;

    data.writeInterfaceToken(IStringService::getInterfaceDescriptor());
    data.writeString16(input);
    remote()->transact(BnStringService::TRANSACTION_lower, data, &reply);

    ::android::String16 result;
    reply.readString16(&result);
    return result;
}

// ═══════════════════════════════════════════════════════════════
// ③ BnStringService::onTransact — Server 端事务分发
//   每个 case = enforceInterface + readXxx(入参) + this->method() + writeXxx(出参)
//   this->method() 是纯虚，分派到子类 StringService 的业务实现
//
//   与 Bp 的严格对称（AIDL 编译器保证）:
//     Bp 写顺序 = Bn 读顺序
//     Bn 写顺序 = Bp 读顺序
// ═══════════════════════════════════════════════════════════════

BnStringService::BnStringService() = default;

::android::status_t BnStringService::onTransact(
        uint32_t code, const ::android::Parcel& data,
        ::android::Parcel* reply, uint32_t /*flags*/) {

    switch (code) {

    case ::android::IBinder::INTERFACE_TRANSACTION: {
        reply->writeString16(IStringService::getInterfaceDescriptor());
        return ::android::NO_ERROR;
    }

    case TRANSACTION_upper: {
        data.enforceInterface(IStringService::getInterfaceDescriptor());

        ::android::String16 input;
        data.readString16(&input);                           // 反序列化入参

        ::android::String16 result = this->upper(input);     // ★ 调子类业务方法

        reply->writeString16(result);                        // 序列化出参
        return ::android::NO_ERROR;
    }

    case TRANSACTION_lower: {
        data.enforceInterface(IStringService::getInterfaceDescriptor());

        ::android::String16 input;
        data.readString16(&input);

        ::android::String16 result = this->lower(input);

        reply->writeString16(result);
        return ::android::NO_ERROR;
    }

    default:
        return ::android::BBinder::onTransact(code, data, reply, 0);
    }
}

}  // namespace binder
}  // namespace example
}  // namespace com
