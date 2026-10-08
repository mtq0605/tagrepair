#pragma once
// 用到的 Windows 小工具:宽字符 / 长路径、控制台输出、哈希、按位置读文件
#include "flac_meta.h"

#include <windows.h>

#include <cstdint>
#include <string>
#include <vector>

namespace wu {

std::string  utf8(const std::wstring& w);
std::wstring wide(const std::string& s);

// 绝对路径 + "\\?\" 前缀(超过 260 个字符的路径也能开);已经带前缀的原样返回
std::wstring longPath(const std::wstring& p);
// 给人看的路径(去掉 "\\?\" 前缀)
std::wstring shownPath(const std::wstring& p);
std::wstring fileName(const std::wstring& p);
std::wstring exeDir();
std::wstring winError(DWORD code);
// 系统错误码 → 一句话的两半:中文那一半是系统自己的说明(系统是什么语言就是什么语言),英文那一半是英文的说明;
// 系统里没有英文的说明时,英文那一半只有错误码
msg::Text errorText(DWORD code);

// 标准输出一行:是控制台就按宽字符写(中文不乱码),被重定向 / 接管道时写 UTF-8
void outLine(const std::wstring& line);
void outLineUtf8(const std::string& line);

// 标准输出 / 输入是不是控制台窗口(不是 = 被重定向到文件或管道)
bool stdoutIsConsole();
bool stdinIsConsole();
// 状态行(进度条):只在控制台窗口里有,在同一行上重画,不换行;文字太长就截到窗口宽度以内。
// 输出被重定向时什么都不写 —— 文件和管道里不会出现回车符之类的控制字符。outLine 写一行之前会先把状态行擦掉
void statusLine(const std::wstring& text);
void clearStatus();
// 一段文字在控制台里占几格(汉字、全角符号算两格)
int displayWidth(const std::wstring& s);

// 哈希(系统自带的 CNG):kind = L"SHA256" / L"MD5"
class Hash {
public:
    explicit Hash(const wchar_t* kind);
    ~Hash();
    Hash(const Hash&) = delete;
    Hash& operator=(const Hash&) = delete;
    bool ok() const { return m_hash != nullptr; }
    void update(const void* d, size_t n);
    std::string hex();                           // 取结果(之后不能再 update)
    bool raw(uint8_t* out, size_t n);            // 取结果的前 n 字节
private:
    void* m_alg = nullptr;
    void* m_hash = nullptr;
    bool  m_bad = false;
};

// 按位置读一个已打开的文件(不动、也不依赖文件指针)
class FileSource : public flacmeta::Source {
public:
    explicit FileSource(HANDLE h) : m_h(h) {}
    uint64_t size() const override;
    bool read(uint64_t at, void* dst, size_t n) const override;
private:
    HANDLE m_h;
};

bool readRange(HANDLE h, uint64_t at, size_t n, std::vector<uint8_t>& out);
bool writeAt(HANDLE h, uint64_t at, const uint8_t* d, size_t n);
// [from, to) 的 SHA-256
bool hashRange(HANDLE h, uint64_t from, uint64_t to, std::string& hex);

} // namespace wu
