// StringService.cpp — 人类写
// 纯业务逻辑：大小写转换。看不到 Binder/Parcel/transact 任何痕迹

#include "StringService.h"

namespace com {
namespace example {
namespace binder {

::android::String16 StringService::upper(const ::android::String16& input) {
    size_t len = input.size();
    ::android::String16 out(input);
    char16_t* buf = out.lockBuffer(len);
    for (size_t i = 0; i < len; i++) {
        if (buf[i] >= 'a' && buf[i] <= 'z')
            buf[i] -= 32;
    }
    out.unlockBuffer(len);
    return out;
}

::android::String16 StringService::lower(const ::android::String16& input) {
    size_t len = input.size();
    ::android::String16 out(input);
    char16_t* buf = out.lockBuffer(len);
    for (size_t i = 0; i < len; i++) {
        if (buf[i] >= 'A' && buf[i] <= 'Z')
            buf[i] += 32;
    }
    out.unlockBuffer(len);
    return out;
}

}  // namespace binder
}  // namespace example
}  // namespace com
