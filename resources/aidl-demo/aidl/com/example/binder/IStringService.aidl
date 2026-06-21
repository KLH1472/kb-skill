// IStringService.aidl  — 人类写，接口定义
// 放在 aidl/com/example/binder/ 下，目录对应 package 名

package com.example.binder;

interface IStringService {
    String upper(String input);
    String lower(String input);
}
