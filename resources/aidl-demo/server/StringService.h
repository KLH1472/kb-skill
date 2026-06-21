// StringService.h — 人类写
// Server 侧业务逻辑声明
#pragma once

#include <com/example/binder/BnStringService.h>   // AIDL 生成的桩

namespace com {
namespace example {
namespace binder {

// 继承 BnStringService（onTransact 已生成完成）
// 只需覆写 upper / lower 纯虚业务方法
class StringService : public BnStringService {
public:
    ::android::String16 upper(const ::android::String16& input) override;
    ::android::String16 lower(const ::android::String16& input) override;
};

}  // namespace binder
}  // namespace example
}  // namespace com
