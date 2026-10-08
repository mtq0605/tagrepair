#pragma once
// 本程序对外的名字、版本、协议号 —— `info --json` 报的就是这几项(宿主程序靠它认出本程序,见 README「宿主协议」)
namespace tagrepair {

constexpr const char* kHostId = "tagrepair";       // 不变:宿主按它认
constexpr const char* kToolName = "tagrepair";
constexpr const char* kToolVersion = "1.1.0";
// 协议号:--json 输出里已有字段的名字或含义变了才加一;只是多出新字段不加(宿主应当忽略不认识的字段)
constexpr int kProtocol = 1;

} // namespace tagrepair
