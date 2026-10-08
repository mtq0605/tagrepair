#include "win_util.h"

#include <bcrypt.h>

namespace wu {

std::string utf8(const std::wstring& w) {
    if (w.empty()) return std::string();
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s((size_t)(n > 0 ? n : 0), '\0');
    if (n > 0) WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), &s[0], n, nullptr, nullptr);
    return s;
}

std::wstring wide(const std::string& s) {
    if (s.empty()) return std::wstring();
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring w((size_t)(n > 0 ? n : 0), L'\0');
    if (n > 0) MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), &w[0], n);
    return w;
}

std::wstring longPath(const std::wstring& p) {
    if (p.compare(0, 4, L"\\\\?\\") == 0) return p;
    std::wstring full;
    DWORD need = GetFullPathNameW(p.c_str(), 0, nullptr, nullptr);
    if (need) {
        full.resize(need);
        const DWORD got = GetFullPathNameW(p.c_str(), need, &full[0], nullptr);
        full.resize(got < need ? got : 0);
    }
    if (full.empty()) full = p;
    if (full.compare(0, 2, L"\\\\") == 0) return L"\\\\?\\UNC\\" + full.substr(2);
    return L"\\\\?\\" + full;
}

std::wstring shownPath(const std::wstring& p) {
    if (p.compare(0, 8, L"\\\\?\\UNC\\") == 0) return L"\\\\" + p.substr(8);
    if (p.compare(0, 4, L"\\\\?\\") == 0) return p.substr(4);
    return p;
}

std::wstring fileName(const std::wstring& p) {
    const size_t s = p.find_last_of(L"\\/");
    return s == std::wstring::npos ? p : p.substr(s + 1);
}

std::wstring exeDir() {
    std::wstring b(32768, L'\0');
    const DWORD n = GetModuleFileNameW(nullptr, &b[0], (DWORD)b.size());
    b.resize(n);
    const size_t s = b.find_last_of(L"\\/");
    return s == std::wstring::npos ? std::wstring() : b.substr(0, s);
}

std::wstring winError(DWORD code) {
    wchar_t* msg = nullptr;
    const DWORD n = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, code, 0,
                                   (LPWSTR)&msg, 0, nullptr);
    std::wstring s = n && msg ? std::wstring(msg, n) : std::wstring();
    if (msg) LocalFree(msg);
    while (!s.empty() && (s.back() == L'\r' || s.back() == L'\n' || s.back() == L' ' || s.back() == L'.' || s.back() == L'。')) s.pop_back();
    wchar_t b[32];
    _snwprintf_s(b, _TRUNCATE, L"(%lu)", (unsigned long)code);
    return s + b;
}

namespace {

int g_statusWidth = 0;                                        // 状态行现在占着几格(0 = 没有)

bool isConsole(DWORD which) {
    DWORD mode = 0;
    return GetConsoleMode(GetStdHandle(which), &mode) != 0;
}

int cellWidth(wchar_t c) {
    // 汉字、假名、谚文、全角符号占两格;其余一格(够用了:这里只是为了不让状态行折到下一行)
    return (c >= 0x1100 && c <= 0x115F) || (c >= 0x2E80 && c <= 0xA4CF) || (c >= 0xAC00 && c <= 0xD7A3) || (c >= 0xF900 && c <= 0xFAFF) ||
                   (c >= 0xFE30 && c <= 0xFE4F) || (c >= 0xFF00 && c <= 0xFF60) || (c >= 0xFFE0 && c <= 0xFFE6)
               ? 2
               : 1;
}

} // namespace

bool stdoutIsConsole() { return isConsole(STD_OUTPUT_HANDLE); }
bool stdinIsConsole() { return isConsole(STD_INPUT_HANDLE); }

int displayWidth(const std::wstring& s) {
    int w = 0;
    for (wchar_t c : s) w += cellWidth(c);
    return w;
}

void clearStatus() {
    if (g_statusWidth <= 0) return;
    const std::wstring blank = L"\r" + std::wstring((size_t)g_statusWidth, L' ') + L"\r";
    DWORD wrote = 0;
    WriteConsoleW(GetStdHandle(STD_OUTPUT_HANDLE), blank.data(), (DWORD)blank.size(), &wrote, nullptr);
    g_statusWidth = 0;
}

void statusLine(const std::wstring& text) {
    const HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    CONSOLE_SCREEN_BUFFER_INFO info{};
    if (!stdoutIsConsole() || !GetConsoleScreenBufferInfo(h, &info)) return;
    const int room = (int)(info.srWindow.Right - info.srWindow.Left);     // 留出最后一格:写满一行光标会折到下一行
    std::wstring shown;
    int width = 0;
    for (wchar_t c : text) {
        if (width + cellWidth(c) > room) break;
        shown.push_back(c);
        width += cellWidth(c);
    }
    std::wstring out = L"\r" + shown;
    if (g_statusWidth > width) out += std::wstring((size_t)(g_statusWidth - width), L' ') + L"\r" + shown;      // 上一次更长:把多出来的擦掉
    DWORD wrote = 0;
    WriteConsoleW(h, out.data(), (DWORD)out.size(), &wrote, nullptr);
    g_statusWidth = width;
}

void outLineUtf8(const std::string& line) {
    const HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD mode = 0, wrote = 0;
    clearStatus();
    if (GetConsoleMode(h, &mode)) {
        const std::wstring w = wide(line) + L"\n";
        WriteConsoleW(h, w.data(), (DWORD)w.size(), &wrote, nullptr);
        return;
    }
    const std::string s = line + "\n";
    size_t done = 0;
    while (done < s.size()) {
        if (!WriteFile(h, s.data() + done, (DWORD)(s.size() - done), &wrote, nullptr) || !wrote) break;
        done += wrote;
    }
}

msg::Text errorText(DWORD code) {
    wchar_t* text = nullptr;
    const DWORD n = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, code,
                                   MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US), (LPWSTR)&text, 0, nullptr);
    std::wstring en = n && text ? std::wstring(text, n) : std::wstring();
    if (text) LocalFree(text);
    while (!en.empty() && (en.back() == L'\r' || en.back() == L'\n' || en.back() == L' ' || en.back() == L'.')) en.pop_back();
    bool ascii = !en.empty();
    for (wchar_t c : en) ascii = ascii && c < 0x80;
    const msg::Text number = msg::of(msg::SysErrorCode, { msg::num((long long)code) });
    return msg::Text(ascii ? en + L" (" + std::to_wstring(code) + L")" : number.en, winError(code));
}

void outLine(const std::wstring& line) { outLineUtf8(utf8(line)); }


// ---------------------------------------------------------------------------
Hash::Hash(const wchar_t* kind) {
    BCRYPT_ALG_HANDLE alg = nullptr;
    if (BCryptOpenAlgorithmProvider(&alg, kind, nullptr, 0) < 0) { m_bad = true; return; }
    BCRYPT_HASH_HANDLE h = nullptr;
    if (BCryptCreateHash(alg, &h, nullptr, 0, nullptr, 0, 0) < 0) {
        BCryptCloseAlgorithmProvider(alg, 0);
        m_bad = true;
        return;
    }
    m_alg = alg;
    m_hash = h;
}

Hash::~Hash() {
    if (m_hash) BCryptDestroyHash((BCRYPT_HASH_HANDLE)m_hash);
    if (m_alg) BCryptCloseAlgorithmProvider((BCRYPT_ALG_HANDLE)m_alg, 0);
}

void Hash::update(const void* d, size_t n) {
    const uint8_t* p = (const uint8_t*)d;
    while (n && m_hash && !m_bad) {
        const ULONG k = n > 0x40000000u ? 0x40000000u : (ULONG)n;
        if (BCryptHashData((BCRYPT_HASH_HANDLE)m_hash, (PUCHAR)p, k, 0) < 0) m_bad = true;
        p += k;
        n -= k;
    }
}

bool Hash::raw(uint8_t* out, size_t n) {
    if (!m_hash || m_bad) return false;
    DWORD len = 0, got = 0;
    if (BCryptGetProperty((BCRYPT_ALG_HANDLE)m_alg, BCRYPT_HASH_LENGTH, (PUCHAR)&len, sizeof len, &got, 0) < 0 || len > 64) return false;
    uint8_t d[64];
    if (BCryptFinishHash((BCRYPT_HASH_HANDLE)m_hash, d, len, 0) < 0) return false;
    for (size_t i = 0; i < n; ++i) out[i] = i < len ? d[i] : 0;
    return true;
}

std::string Hash::hex() {
    if (!m_hash || m_bad) return std::string();
    DWORD len = 0, got = 0;
    if (BCryptGetProperty((BCRYPT_ALG_HANDLE)m_alg, BCRYPT_HASH_LENGTH, (PUCHAR)&len, sizeof len, &got, 0) < 0 || len > 64) return std::string();
    uint8_t d[64];
    if (BCryptFinishHash((BCRYPT_HASH_HANDLE)m_hash, d, len, 0) < 0) return std::string();
    static const char* x = "0123456789abcdef";
    std::string s;
    for (DWORD i = 0; i < len; ++i) {
        s.push_back(x[d[i] >> 4]);
        s.push_back(x[d[i] & 15]);
    }
    return s;
}

// ---------------------------------------------------------------------------
uint64_t FileSource::size() const {
    LARGE_INTEGER s{};
    return GetFileSizeEx(m_h, &s) ? (uint64_t)s.QuadPart : 0;
}

bool FileSource::read(uint64_t at, void* dst, size_t n) const {
    uint8_t* p = (uint8_t*)dst;
    while (n) {
        OVERLAPPED ov{};
        ov.Offset = (DWORD)at;
        ov.OffsetHigh = (DWORD)(at >> 32);
        DWORD got = 0;
        const DWORD want = n > (1u << 20) ? (1u << 20) : (DWORD)n;
        if (!ReadFile(m_h, p, want, &got, &ov) || got == 0) return false;
        p += got;
        at += got;
        n -= got;
    }
    return true;
}

bool readRange(HANDLE h, uint64_t at, size_t n, std::vector<uint8_t>& out) {
    out.resize(n);
    return n == 0 || FileSource(h).read(at, out.data(), n);
}

bool writeAt(HANDLE h, uint64_t at, const uint8_t* d, size_t n) {
    while (n) {
        OVERLAPPED ov{};
        ov.Offset = (DWORD)at;
        ov.OffsetHigh = (DWORD)(at >> 32);
        DWORD wrote = 0;
        const DWORD want = n > (1u << 20) ? (1u << 20) : (DWORD)n;
        if (!WriteFile(h, d, want, &wrote, &ov) || wrote == 0) return false;
        d += wrote;
        at += wrote;
        n -= wrote;
    }
    return true;
}

bool hashRange(HANDLE h, uint64_t from, uint64_t to, std::string& hex) {
    Hash sha(BCRYPT_SHA256_ALGORITHM);
    if (!sha.ok()) return false;
    std::vector<uint8_t> buf(1u << 20);
    const FileSource src(h);
    while (from < to) {
        const size_t k = (size_t)(to - from < buf.size() ? to - from : buf.size());
        if (!src.read(from, buf.data(), k)) return false;
        sha.update(buf.data(), k);
        from += k;
    }
    hex = sha.hex();
    return !hex.empty();
}

} // namespace wu
