#pragma once
// 命令行(main.cpp)与不用命令行的那种用法(interactive.cpp)共用的几样(实现在 batch.cpp)
#include "copy_flac.h"
#include "tagfile.h"

#include <string>
#include <vector>

namespace cli {

// 给的每一项:是文件夹就把里面的音频文件列出来(按扩展名;recursive = 连子文件夹),是文件就原样算一个(不看扩展名)
std::vector<std::wstring> expandInputs(const std::vector<std::wstring>& inputs, bool recursive);

// 找不到 FFmpeg 运行库时说的那一句:要哪四个文件、找过哪些文件夹、怎么指给它
msg::Text noFfmpegText(const std::vector<std::wstring>& tried);

// 控制台上写一行:English (中文)。indent = 前面空几格
void say(const msg::Text& t, int indent = 0);
void sayBlank();

// 进度条:整件事做到哪了 —— 第几个文件 / 一共几个,条按"做完的文件 + 手上这个文件做到的比例"走。
// 只在标准输出是控制台窗口时画(同一行上重画);输出被重定向到文件 / 管道时什么都不写,那时每个文件一行由调用方照常打
class Progress {
public:
    Progress(const msg::Entry& label, size_t total, bool enabled = true);
    ~Progress() { end(); }
    void begin(size_t index, const std::wstring& shownPath);      // 开始第 index 个(从 0 数)
    void within(double fraction);                                  // 手上这个文件做到哪了(0..1)
    void end();                                                    // 把那一行擦掉
    bool drawing() const { return m_on; }

private:
    void draw(bool force);
    std::wstring m_label, m_name;
    size_t m_total = 0, m_index = 0;
    double m_within = 0;
    int m_lastPermille = -1;
    unsigned long long m_lastTick = 0;
    bool m_on = false;
};

// 条上的文字(纯函数;tests/ 也核对):「Repairing (修复) [#########---------------]  37%  3/8  name.flac」
constexpr int kProgressCells = 24;
inline std::wstring progressText(const std::wstring& label, size_t index, size_t total, double within, const std::wstring& name) {
    within = within < 0 ? 0 : within > 1 ? 1 : within;
    const double whole = total ? ((double)index + within) / (double)total : 1.0;
    const int filled = (int)(whole * kProgressCells + 1e-9), percent = (int)(whole * 100 + 1e-9);
    wchar_t b[64];
    _snwprintf_s(b, _TRUNCATE, L"] %3d%%  %zu/%zu  ", percent, index + 1 > total ? total : index + 1, total);
    return label + L" [" + std::wstring((size_t)filled, L'#') + std::wstring((size_t)(kProgressCells - filled), L'-') + b + name;
}

// 一个文件的结果:给人看的几行 / 给程序读的一行 JSON。i = 第几个(从 0 数),n = 一共几个
void printScan(size_t i, size_t n, const std::wstring& shownPath, const tagrepair::Scan& s);
void printFix(size_t i, size_t n, const std::wstring& shownPath, const tagrepair::FixReport& r);
void printCopy(size_t i, size_t n, const std::wstring& shownPath, const tagrepair::CopyReport& r);
void jsonScan(const std::wstring& shownPath, const tagrepair::Scan& s);
void jsonFix(const std::wstring& shownPath, const tagrepair::FixReport& r);
void jsonCopy(const std::wstring& shownPath, tagrepair::CopyKind kind, const tagrepair::CopyReport& r);
// 每个文件开头的那一行:[3/12] 路径
std::wstring fileHeader(size_t i, size_t n, const std::wstring& shownPath);

// 一批文件挨个做;每个文件之前看一次取消(看到就停,剩下的算"没有开始")
struct RepairTally {
    int total = 0;
    int n[tagrepair::kOutcomes] = { 0 };          // 按 Outcome 数
    int notStarted = 0;
    bool cancelled = false;
};
RepairTally repairBatch(const std::vector<std::wstring>& files, const tagrepair::FixOptions& opt, bool json);
struct CopyTally {
    int total = 0;
    int n[tagrepair::kCopyOutcomes] = { 0 };      // 按 CopyOutcome 数
    int notStarted = 0;
    bool cancelled = false;
};
CopyTally copyBatch(const std::vector<std::wstring>& files, tagrepair::CopyKind kind, const tagrepair::CopyOptions& opt, bool json);

// 取消之后的那一句:「Cancelled. Repaired 2 of 5; 3 not started. (已取消。5 个里修好了 2 个,3 个没有开始。)」
// head = CancelRepair / CancelSave;skipped = 轮到了但不用处理的(本来就完整、不属于这一类),notDone = 做了但没做成的,
// stopped = 做到一半停下、保持原样的。真做的时候(不是 --dry-run)几项加起来正好是 total,读的人不用猜剩下的去哪了
inline msg::Text cancelText(const msg::Entry& head, int done, int total, int skipped, int notDone, int stopped, int notStarted) {
    msg::Text t = msg::of(head, { msg::num(done), msg::num(total) });
    if (skipped > 0) t = t + msg::of(msg::CancelPartSkipped, { msg::num(skipped) });
    if (notDone > 0) t = t + msg::of(msg::CancelPartNotDone, { msg::num(notDone) });
    if (stopped > 0) t = t + msg::of(msg::CancelPartStopped, { msg::num(stopped) });
    return t + msg::of(msg::CancelPartNotStarted, { msg::num(notStarted) });
}

// 没给命令:paths 为空 = 双击启动,否则 = 把文件 / 文件夹拖到了程序上。在控制台窗口里一问一答(见 interactive.cpp)
int runInteractive(const std::vector<std::wstring>& paths, const std::wstring& ffmpegDir);

// 退出码
enum { kExitOk = 0, kExitNotDone = 1, kExitUsage = 2, kExitNoFfmpeg = 3, kExitCancelled = 4 };

} // namespace cli
