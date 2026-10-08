# NOTICE

## Purpose

This tool restores the identifying tags of FLAC files that are already MQA-encoded but have lost those tags,
typically after being downloaded, re-saved or edited with a tag editor. For such audio stored as a WAV file, and for
16-bit files when you choose so, it can save a FLAC copy that carries the tags. It does nothing else.
It was developed by an individual for noncommercial purposes and implements only the most basic tag-repair function.

## 1. How a file is identified

The tool examines only the file itself. It searches the audio data for the MQA synchronization marker and reads
the original sample rate stored with it. If no marker is found, the file is left untouched.

## 2. What is written

Three plain-text tags, with no signature and no encryption:

```
ENCODER            = MQAEncode v1.1, tag repair (unsigned)
MQAENCODER         = MQAEncode v1.1, tag repair (unsigned)
ORIGINALSAMPLERATE = the original sample rate read from the stream, for example 352800
```

A file that already carries an acceptable encoder value in either of the first two tags, together with the
original sample rate, is treated as complete and is not touched.
Otherwise, acceptable values already present in the file are kept; only missing or unacceptable values are written.
If the file has no comment block yet, one is created and labeled "tagrepair".
The value begins with "MQAEncode" because some devices recognize a file only if it does. The words "tag repair"
and "(unsigned)" make it clear that the value was not written by an official encoder.

## 3. The audio is never altered

Only the tag (metadata) area of the FLAC file is rewritten. The audio data remains byte-for-byte identical. This
is verified before and after writing, and the original file is restored if any difference is found.
For WAV files, and for 16-bit files when you choose so, the tool does not change the original file at all. It
writes a new FLAC file next to it that contains exactly the same sample values (16-bit samples may be stored in
24-bit words with zero low bits) together with the tags and the file's existing ordinary tags, and it keeps the
new file only after verifying that it decodes to exactly the same samples.

## 4. It cannot turn ordinary audio into MQA

The tool does not create MQA-encoded audio. It does not create, alter or circumvent any authentication data in
the stream, and it contains no MQA decoding (unfolding) code.

## 5. Unofficial tool; no MQA authentication

This tool is not affiliated with, authorized by or endorsed by the owner of MQA. "MQA" is a trademark of its
owner and is used here solely to identify the file format. Whether a device or program recognizes or displays a
file as MQA is determined by that device or program. This tool provides no authentication and gives no assurance
of any.
This tool serves only to restore simple identifying tags that have been lost (tag repair). It does not constitute
a statement or guarantee regarding any authentication status.

## 6. No reverse engineering by the authors

The authors of this tool have not reverse-engineered or disassembled any MQA encoder or decoder, nor examined the
internals of one. Their own work is limited to reading and analyzing the structure that can be observed directly
in the files themselves. All source code of this tool was written by its authors. The location of the
synchronization marker, the layout of the record that follows it, its checksum rule and the sample-rate codes are
taken from publicly available descriptions and have been verified, file by file, against files the authors
lawfully hold.

## 7. Run-time libraries

FFmpeg shared libraries (LGPL) are loaded at run time. They are not part of this tool.

## 8. License

PolyForm Noncommercial License 1.0.0. In short: anyone may use, modify and share this tool free of charge for
noncommercial purposes; it may not be used for any commercial purpose and may not be sold. This sentence is a
summary only. The LICENSE file contains the full terms and is the governing text.

Required Notice: Copyright (c) 2026 tagrepair authors

## 9. Your responsibility; disclaimer

Use this tool only on files you lawfully own, and ensure that your use complies with the laws that apply to you.
The tool is provided "as is", without warranty of any kind, express or implied.
Anyone who misuses the tool or its code, for example to infringe the rights of others or as a starting point for
reverse-engineering third-party technology, bears sole responsibility for the consequences. The authors accept
no liability.

This notice states facts and conditions of use. It is not legal advice.

# 声明

## 这个工具是做什么的

有些 FLAC 文件本身是 MQA 编码,但在下载、转存或者用软件改标签的时候,把用来识别它的标签弄丢了。
这个工具把丢掉的标签补回去。同样情况的 WAV 文件,以及在你选择时 16 bit 的文件,它可以在旁边另存一个带标签的 FLAC。别的事它不做。
本工具是个人出于非商业目的开发的,只实现了最基本的标签修复功能。

## 一、怎么判断一个文件

只看文件本身:在音频数据里找 MQA 的同步标记,找到了,再读出和它存在一起的原始采样率。
找不到同步标记的文件,一个字节都不会动。

## 二、写进去的是什么

三个纯文字的标签,没有签名,也没有加密:

```
ENCODER            = MQAEncode v1.1, tag repair (unsigned)
MQAENCODER         = MQAEncode v1.1, tag repair (unsigned)
ORIGINALSAMPLERATE = 从码流里读到的原始采样率,比如 352800
```

文件里已经有合格的编码器值(前两个标签里有一个就行)和原始采样率的,算标签完整,这个文件不动。
其余情况下,原来就有、而且合格的值不动,只补缺的、换掉不合格的。
文件里原来没有标签区的话,会新建一个,并标注为 tagrepair。
值用 "MQAEncode" 开头,是因为有的设备只认这个开头。
后面写上 "tag repair" 和 "(unsigned)",是为了不让人把它当成官方编码器写的值。

## 三、音频不会被改动

只重写 FLAC 文件里放标签(元数据)的那一段,音频数据一个字节都不变。
写之前、写之后各核对一遍,对不上就把文件恢复成原样。
对 WAV 文件,以及在你选择时对 16 bit 的文件,这个工具完全不改原文件,而是在旁边另存一个新的 FLAC 文件:
里面的采样值和原文件一模一样(16 bit 的可以放进 24 bit 的格子里,低 8 位补零),再加上标签和原文件里已有的普通标签;
新文件要解码核对、采样值与原文件完全一致才会留下。

## 四、不能把普通音频变成 MQA

这个工具不生成 MQA 编码,不生成、不修改、也不绕过码流里的任何认证数据,里面也没有 MQA 解码(展开)的代码。

## 五、不是官方工具,不保证任何 MQA 认证

这个工具和 MQA 的权利人没有任何关系,没有得到对方的授权或认可。
"MQA" 是其权利人的商标,这里只是用来称呼这种文件格式。
设备或软件认不认、显不显示成 MQA,由它们自己决定。这个工具不提供任何认证,也不做任何保证。
本工具仅用于恢复已丢失的简单识别标签(tag repair),不构成对任何认证状态的声明或保证。

## 六、作者没有做过逆向工程

这个工具的作者没有对任何 MQA 编码器或解码器做过逆向、反汇编,也没有研究过它们的内部。
我们自己做的只是读取和分析文件本身可以直接观察到的结构。这个工具的源代码全部由作者自己编写。
同步标记的位置、它后面那条记录的排布、校验规则和采样率的码取自公开的资料,并在我们合法持有的文件上逐个核对过。

## 七、运行时用到的库

运行时会加载 FFmpeg 的动态库(LGPL),它们不属于这个工具。

## 八、许可

PolyForm Noncommercial License 1.0.0。简单说:任何人都可以为非商业目的免费使用、修改、转发;不能用于任何商业目的,也不能出售。
这句话只是概括,完整条款以 LICENSE 文件为准。

## 九、使用者的责任与免责

请只用它处理你自己合法拥有的文件,并自己确认这样做在你所在的地方是合法的。
这个工具按"现状"提供,不附带任何明示或暗示的保证。
任何人滥用这个工具或它的代码(比如拿去侵犯别人的权利,或者以它为起点去逆向别人的技术),后果由他自己承担,作者不承担责任。

本声明只说明事实和使用条件,不是法律意见。

# tagrepair 使用说明

下面是用法、命令、宿主协议、构建与安全设计。上面的声明是前提;这里说的任何一句都不超出它。

## 怎么用(不用命令行)

1. 把 `tagrepair.exe` 和 FFmpeg 的四个运行库(`avutil-*.dll`、`swresample-*.dll`、`avcodec-*.dll`、`avformat-*.dll`)放在同一个文件夹里。
   自己构建的话,`build\bin` 就是这样一个文件夹。
2. 把音乐文件,或者整个文件夹,拖到 `tagrepair.exe` 的图标上再松手。
3. 它先只读地检查(文件夹连子文件夹一起),检查时有进度条。查完把结果分开列出来:
   - 有同步标记、标签完整的有几个;
   - 有同步标记、标签缺了、可以原地修复的(逐个列出);
   - WAV:可以在原文件旁边另存一个新的 FLAC 文件的(逐个列出,带新文件的名字;WAV 不改);
   - 16 bit:可以在原文件旁边另存一个 24 bit 拷贝的(逐个列出,带新文件的名字;原文件不改);
   - 没有同步标记的有几个、处理不了的有几个和各自的原因。
4. 上面三类里有哪一类,就问哪一类,按这个顺序一类问一次,各有各的 `Y` / `N`:
   「原地修复这 N 个文件吗?」「把这 N 个 WAV 另存为新的 FLAC 文件吗?」「给这 N 个文件另存 24 bit 的拷贝吗?」
   回答 `Y` 再按回车:马上显示「Starting: …(开始……)」,接着是进度和每个文件的结果。
   别的回答、直接回车:显示一句「没有……,什么都没有改」(前面的问题已经修过或存过文件时,说的是「别的什么都没有改」)。
   没有对应的那个 `Y`,不写、也不新建任何东西。
   旁边已经有同名新文件的(比如上次存过了)不再问,单独标出来。
5. 全部做完,按回车关窗口。

随时可以按 `Ctrl+C` 取消(程序一开始就会说明)。正在写的那个文件会先写完或者恢复原样,然后停下,并说清楚做了多少、没做多少,
比如「Cancelled. Repaired 2 of 5; 3 not started. (已取消。5 个里修好了 2 个,3 个没有开始。)」;轮到了但不用处理的、没做成的、
做到一半停下保持原样的,也各报一个数。直接关窗口也一样安全。

直接双击 `tagrepair.exe`(什么都不拖)只显示一段说明。

窗口里的文字一律是「English text (中文文字)」:英文在前,同一句的中文在后面的括号里。文件名和标签的值原样显示。

## 它做什么

- `scan`:逐个文件判断属于哪一类 —— 不是无损 PCM / 没有同步标记 / 有同步标记且标签完整 / 有同步标记且标签缺失或不完整(可以修复)/
  WAV 里有同步标记(可以另存为 FLAC)/ 有同步标记但这里处理不了(别的容器、码流里没读到原始采样率、文件结构损坏)。
  同时说出这个文件上能做哪几件事。
- `fix`:给"可以修复"的 FLAC 写上三个标签,原地修。已有的合格值保留,只补缺的、换掉不合格的;同一个键有几份的只留一份。
  - `ENCODER` / `MQAENCODER` 默认写 `MQAEncode v1.1, tag repair (unsigned)`
  - `ORIGINALSAMPLERATE` 写码流里读到的原始采样率
  - 标签完整的文件一个字节都不动。
  - 没有注释块的 FLAC 会新建一个,厂商串写 `tagrepair`;已有的注释块厂商串不动。
- `fix --wav-to-flac`(另存,明说了才做):有同步标记的 WAV → 在它旁边新写一个 `<文件名>.flac`。
- `fix --copy-24bit`(另存,明说了才做):有同步标记的 16 bit 文件(WAV 或 FLAC)→ 在它旁边新写一个 `<文件名>.24bit.flac`。
- 不带这两个选项,`fix` 不会新建任何文件;Ogg / Matroska / MP4 里的 FLAC 只报告,不写。

### 两种另存各做什么、不做什么

| | `--wav-to-flac` | `--copy-24bit` |
| --- | --- | --- |
| 对哪些文件 | 有同步标记的 WAV(16 或 24 bit 的整数 PCM) | 有同步标记的 16 bit 文件:WAV,或者结构完好的 FLAC(标签修没修过都行) |
| 新文件 | `<文件名>.flac`,与原文件在同一个文件夹 | `<文件名>.24bit.flac`,与原文件在同一个文件夹 |
| 采样 | 采样率、声道数、位深与 WAV 相同;每个采样值与 WAV 里的相同(FLAC 是无损编码) | 采样率、声道数相同;每个 16 bit 的采样原样放在 24 bit 的高 16 位,低 8 位补零。不重新计算,不加抖动 |
| 标签 | 三个识别标签;加上 FFmpeg 从 WAV 里读得到的普通标签(标题、艺人、专辑……) | 三个识别标签;原文件是 FLAC 时,它的注释、图片、CUE 表带过去(定位表、应用块不带) |

两种都守这几条:

- 原文件只读地打开,从不修改、改名、删除。
- 从不覆盖已有的文件:目标名下已经有文件,就拒绝这一个(`target_exists`),已有的文件不动。
- 先写进旁边的临时文件,核对无误才改成正式的名字。核对 = 把新文件从头到尾解一遍:采样个数、全部采样的 MD5 与原文件相同;
  文件头里的 MD5 就是这些采样的;新文件扫描出来是"有同步标记、标签完整",原始采样率与原文件相同。
  24 bit 的拷贝另外逐个采样看:低 8 位全是零,高 16 位就是原文件的采样。有一处对不上就删掉临时文件、报失败。
- 没有同步标记的文件绝不转。
- 16 bit 的 WAV 两个选项都给时,两个新文件都会写(一个 16 bit 的 `.flac`、一个 `.24bit.flac`)。
- 新文件的音频帧由运行时加载的 FFmpeg 里的 FLAC 编码器编出;文件头是本程序自己拼的,注释块的厂商串是 `tagrepair`。

为什么有 `--copy-24bit`:有的设备只认 24 bit 的文件。它默认不做,要明说。

## 命令

```
tagrepair <文件或文件夹>…                    (= 拖放;在窗口里一问一答)
tagrepair scan <文件或文件夹>… [--recursive] [--json]
tagrepair fix  <文件或文件夹>… [--recursive] [--dry-run] [--keep-backup] [--json]
               [--wav-to-flac] [--copy-24bit] [--copy-only]
               [--encoder-tag "<值>"] [--accept-prefix "<文字>"]
tagrepair selftest [--keep]
tagrepair info [--json]
tagrepair --help
```

| 选项 | 作用 |
| --- | --- |
| `--recursive` | 文件夹连子文件夹一起看(不跟进目录联接) |
| `--dry-run` | 只说会怎么做,不写、不新建 |
| `--keep-backup` | 原地修复时,原文件原样留在 `<文件名>.bak`(这个名字已经有文件时用 `.bak2`、`.bak3`…);这时一律整文件重写 |
| `--wav-to-flac` | 有同步标记的 WAV:在旁边另存 `<文件名>.flac`(见上一节) |
| `--copy-24bit` | 有同步标记的 16 bit 文件:在旁边另存 `<文件名>.24bit.flac`(见上一节) |
| `--copy-only` | 只做上面选的另存,不原地修任何文件(要和 `--wav-to-flac` 或 `--copy-24bit` 一起用;不能和 `--keep-backup` 一起用) |
| `--json` | 每个文件一行 JSON(UTF-8),最后一行汇总 |
| `--encoder-tag "<值>"` | 换一个写进 `ENCODER` / `MQAENCODER` 的值。值里必须带着 `tag repair` 和 `(unsigned)`;不能有控制字符、分号,头尾不能是空格,最长 200 字节 |
| `--accept-prefix "<文字>"` | 已有的值以这段文字开头就算合格、保留不动(默认 `MQAEncode`)。给空串 = 只要不是空值就合格 |
| `--ffmpeg-dir <文件夹>` | FFmpeg 的四个运行库在哪(`scan` / `fix` / `selftest` 都认;给了就只看这一处) |

`--encoder-tag`、`--accept-prefix` 对 `scan` 同样有效(用同一套规则判"完不完整")。
文件夹里只看常见音频扩展名的文件;直接给出的文件不看扩展名。
第一个参数不是命令、但是磁盘上存在的文件或文件夹时,按拖放处理(所有参数都当路径,文件夹一律连子文件夹)。
`fix` 带另存选项时分几遍做:先把整批文件原地修一遍(除非 `--copy-only`),再做 `--wav-to-flac`,最后做 `--copy-24bit`。

不加 `--json` 时输出是给人看的:每个文件以 `[第几个/一共几个] 路径` 开头,下面是结果,一律「English (中文)」。
在控制台窗口里另有一行进度条(在同一行上重画);输出被重定向到文件或管道时不画进度条,也不会写任何控制字符。
`Ctrl+C` 随时可以取消(见下面「取消」)。

退出码:

| 码 | 含义 |
| --- | --- |
| 0 | 正常结束 |
| 1 | 有文件被拒绝或没做成;或自检没有通过 |
| 2 | 用法不对,或选项的值不能用;不带参数启动(只显示说明)也是 2 |
| 3 | 找不到 FFmpeg 运行库 |
| 4 | 被取消(`Ctrl+C`):有文件没有开始,或者做到一半停下了 |

## 判断规则

- 无损 PCM、两声道、整数样本不少于 16 bit、采样率 44.1 kHz 或 48 kHz 的文件才去找同步标记;从头解码最多 10 秒。
- "找到同步标记"只看标记本身;原始采样率要有一条校验通过的数据同步记录才读得出。标记在、采样率读不出的文件,
  只有它自己已经带着一个合格的 `ORIGINALSAMPLERATE` 时才算能修(那个值保留)。
- 一个键"合格"= 恰好出现一次,而且值对:`ENCODER` / `MQAENCODER` 以认可的前缀开头(或正好是本程序会写的值),
  `ORIGINALSAMPLERATE` 是只含数字的整数并等于码流里的原始采样率。
- 标签"完整"= `ORIGINALSAMPLERATE` 合格,并且 `ENCODER` / `MQAENCODER` 两个里至少有一个合格。
  另一个编码器键不在、或者是别的值(比如合格的 `MQAENCODER` 旁边有 `ENCODER=Lavf…`),不算不完整,原样留着 ——
  有的文件本来就只带 `ENCODER` 和 `ORIGINALSAMPLERATE` 两个标签,这样的文件是完整的,不动。
- 不完整的才算"可以修复";修的时候把三个键都补到合格(原来合格的值保留)。
- 这条规则只写在 `src/tag_rule.h` 一处(`tagsComplete`);逐键的情况(在不在、合不合格)照旧在 JSON 里报出来。
- 检测的代码只有 `src/sync_detect.h` / `src/sync_detect.cpp` 两个文件;它依据的格式事实写在头文件开头。

## 安全设计

- 写之前:只读属性、被别的程序打开的文件一律拒绝(独占打开);元数据结构走不通、解码有错、音频不完整的不碰。
- 音频帧到文件末尾的字节在修之前、修之后各算一遍 SHA-256;FFmpeg 整轨解码的样本各算一遍 MD5;`STREAMINFO` 不变。
  任何一项对不上都算失败。
- 写完读回:本程序自己的解析器与 FFmpeg 都要读到三个键且值正确。
- 写法两种:
  - 原地改写 —— 只在长度能靠 `PADDING` 找平、并且改动的字节全落在文件的同一个 512 字节扇区内时使用
    (带压缩 / 加密 / 稀疏属性的文件不用);读回不对就把原字节写回去。
  - 整文件重写 —— 其余情况:在旁边写一个完整的新文件(名字 `<文件名>.tagfix.<8 位十六进制>.tmp`;文件名太长、
    这个名字建不出来时用 `~tagfix.<8 位十六进制>.tmp`;只新建、不覆盖已有文件),
    全部核对通过后用 `ReplaceFileW` 换入。任何失败都删掉临时文件,原文件保持原样。
- 换入之前再核对一遍原文件(大小、修改时间、音频帧之前的字节);这中间被别的程序改过就放弃,不把别人的改动盖掉。
- 换入做到一半、原来的名字下已经没有文件时,不删任何东西:先多试几次把名字补回去;补不回去就把仅有的那一份原样留着,
  并在结果里写明它的路径(原因代码 `swap_incomplete`)。
- 整文件重写某个文件时,会顺手删掉上次被打断留下的 `<文件名>.tagfix.<8 位十六进制>.tmp` —— 只删名字、内容都对得上且没有程序开着的
  (`~tagfix.….tmp` 这种名字的不会自动删)。
- 整文件重写需要与文件一样大的空闲空间;它会断开硬链接(另一个名字保留原内容)。
- 另存(`--wav-to-flac` / `--copy-24bit`)从不动原文件;新文件的临时名字是 `<新文件名>.tagfix.<8 位十六进制>.tmp`,核对通过才改名,
  改名不带"覆盖"。需要与新文件一样大的空闲空间。
- 拖放 / 双击的用法里,没有明确回答 `Y` 就不写、不新建。

### 取消

`Ctrl+C`、`Ctrl+Break`、关窗口都只是告诉程序"停":

- 文件与文件之间、只读的阶段(扫描、算指纹、解码)—— 立刻停。
- 正在往临时文件里写、正在核对临时文件(整文件重写、另存都是这样)—— 立刻停,临时文件删掉,原文件没有动过。
- 原地写的那一下、换入(或另存最后的改名)已经开始 —— 把这个文件做完(或者按上面的设计恢复原样),然后停。

所以任何时刻取消,每个文件要么没动过、要么完整做好,不会停在中间;不留临时文件。
关窗口时系统只给程序几秒钟:要是那一刻正好在写临时文件而且来不及收尾,留下的也只是一个 `.tagfix.….tmp`,原文件没动过。
原地修复留下的那种,下次整文件重写这个文件时会被清掉;另存留下的那种要手工删。

## 构建

Windows、MSVC(Visual Studio 2022 的工具链)、CMake 3.20 以上。除了这个文件夹,只需要一份 FFmpeg 开发包
(用它的 `include\` 和 `bin\` 里的 DLL;FFmpeg 不编进来,也不在这个仓库里)。

```
cmake -S . -B build -DFFMPEG_ROOT=<FFmpeg 开发包>
cmake --build build --config Release
ctest --test-dir build -C Release
```

没给 `-DFFMPEG_ROOT` 时看环境变量 `TAGREPAIR_FFMPEG_ROOT`;也没有就报错。构建文件里不写任何别处的路径。

成品在 `build\bin\`:`tagrepair.exe`,旁边是构建脚本从你给的开发包里拷过来的四个 DLL(图方便:这个文件夹整个拿走就能用;
这四个 DLL 不属于本工具),以及 `LICENSE`、`NOTICE.txt`、`README.md` 各一份(许可与声明跟着成品走)。
测试三项:`unit`(`tagrepair_tests`,纯函数,不需要 FFmpeg;同步标记检测的核对、文字表每一条两种语言都有的核对也在这里)、
`selftest`(程序自己合成 FLAC / WAV,在临时目录里把写入、换入、读回、另存、取消的每一条验一遍,不需要任何外部音频文件)、
`process`(把成品当黑盒跑:`tests/process_tests.cmake`;其中控制台里才有的东西 —— 进度条、`Ctrl+C`、关窗口 ——
放在一个看不见的伪控制台里验,不开窗口)。测试用的文件全部是按格式拼出来的字节,不含任何音乐。

给人看的文字全部在 `src/messages.h` 一个文件里,每一句英文、中文各写一次;要改措辞、要加一句话,都在那里。

测试程序另有几个命令(成品里没有):`tagrepair_tests samples <新的空文件夹>` 造一套合成文件;
`tagrepair_tests many <新的文件夹> <个数>` 造一批名字全是 ASCII 的;
`tagrepair_tests strip <入.flac> <出.flac>` 把三个标签拿掉、另存一份(不改已有的文件);
`tagrepair_tests pty <超时毫秒> <步骤>… -- <程序> <参数>…` 在看不见的伪控制台里跑一个程序并喂键盘输入;
`tagrepair_tests bilingual <抓下来的输出>` 逐行核对两种语言都有。
环境变量 `TAGREPAIR_TEST_DELAY_MS`(只给测试用):是正数时程序每处理一个文件之前等这么多毫秒,好在确定的时刻按 `Ctrl+C`;平时不要设。

被别的 CMake 工程用 `add_subdirectory()` 带上时:对方先设好 `FFMPEG_ROOT`,可以设 `TAGREPAIR_OUTPUT_DIR`(成品放哪);
这时默认不编测试程序、不拷 DLL;`LICENSE`、`NOTICE.txt`、`README.md` 照样拷到成品旁边。

## 运行时找 FFmpeg

程序要 FFmpeg 的四个 DLL:`avutil`、`swresample`、`avcodec`、`avformat`(`swresample` 是 `avcodec` 要用的;
文件名里的版本号在构建时按开发包定下,`tagrepair info --json` 的 `ffmpeg` 一项列着)。四个要在同一个文件夹里。找的顺序:

1. `--ffmpeg-dir <文件夹>`(给了就只看这一处)
2. `tagrepair.exe` 所在的文件夹
3. 往上两级的文件夹(被别的程序装成 `<那个程序>\plugins\tagrepair\tagrepair.exe` 时,运行库在那个程序那一层)
4. 环境变量 `TAGREPAIR_FFMPEG_DIR` 指的文件夹

都没有:打印一行说明(要哪四个文件、找过哪些文件夹),退出码 3。不会弹系统的"缺少 DLL"对话框,也不到系统的搜索路径里去找。
另存要用 FFmpeg 里的 FLAC 编码器;运行库里没有它时,另存报 `no_encoder`,别的功能照常。

## 宿主协议

别的程序只通过下面这些与本程序打交道。协议号现在是 1;已有字段的名字或含义变了才会加一,
只是多出新字段、多出新的取值不加 —— 宿主应当忽略不认识的字段,把不认识的 `state` 当成"这里处理不了"。
输出是 UTF-8,一行一个 JSON 对象;路径里的反斜杠按 JSON 转义。加了 `--json` 就没有进度条、没有给人看的文字。

### `tagrepair info --json`

不需要 FFmpeg。一行:

```json
{"type":"info","id":"tagrepair","name":"tagrepair","version":"1.1.0","protocol":1,"commands":["info","scan","fix","selftest"],"ffmpeg":["avutil-61.dll","swresample-7.dll","avcodec-63.dll","avformat-63.dll"]}
```

`id` 固定是 `tagrepair`,宿主按它和 `protocol` 认。

### `tagrepair scan <文件>… --json`

每个文件一行(`"type":"file"`),最后一行汇总(`"type":"summary"`)。只读,共享打开,不妨碍正在用这个文件的程序。

```json
{"type":"file","path":"D:\\Music\\a.flac","state":"repairable","text":"MQA,标签不完整(ENCODER、MQAENCODER、ORIGINALSAMPLERATE),可以修复","textEn":"MQA, tags incomplete (ENCODER, MQAENCODER, ORIGINALSAMPLERATE); can be repaired","container":"flac","codec":"flac","rate":44100,"bits":24,"channels":2,"marker":true,"origRate":352800,"bad":["ENCODER","MQAENCODER","ORIGINALSAMPLERATE"],"tags":{"ENCODER":"Lavf58.76.100","MQAENCODER":"","ORIGINALSAMPLERATE":""},"tagCount":{"ENCODER":1,"MQAENCODER":0,"ORIGINALSAMPLERATE":0},"tagOk":{"ENCODER":false,"MQAENCODER":false,"ORIGINALSAMPLERATE":false},"write":{"ENCODER":"MQAEncode v1.1, tag repair (unsigned)","MQAENCODER":"MQAEncode v1.1, tag repair (unsigned)","ORIGINALSAMPLERATE":"352800"},"actions":["repair"]}
{"type":"summary","files":1,"complete":0,"repairable":1,"unrepairable":0,"noMarker":0,"notLossless":0,"error":0,"convertible":0,"notScanned":0}
```

| 字段 | 含义 |
| --- | --- |
| `state` | 这个文件是哪一类:`complete`(有同步标记,标签完整)/ `repairable`(有同步标记,可以修复)/ `rate_unknown`(有同步标记,码流里没读到原始采样率,写不出 `ORIGINALSAMPLERATE`)/ `damaged`(有同步标记的 FLAC,结构有问题)/ `convertible`(WAV 里有同步标记,可以另存为 FLAC;只对 WAV 出现)/ `unsupported_container`(有同步标记,但这种容器这里处理不了)/ `no_marker`(没有同步标记)/ `not_lossless` / `error`(打不开) |
| `text` / `textEn` | 同一个结论,中文 / 英文各一句 |
| `container` / `codec` | FFmpeg 的格式名 / 编码名(原生 FLAC 是 `flac` / `flac`) |
| `rate` / `bits` / `channels` | 文件本身的采样率、位深、声道数 |
| `marker` | 音频数据里找到了同步标记 |
| `origRate` | 码流里写的原始采样率(Hz);0 = 没读到 |
| `tags` | 三个标签现在的值(同一个键有几份时是第一份;没有就是空串) |
| `tagCount` | 每个键在文件里出现了几次(0 = 没有) |
| `tagOk` | 每个键各自合不合格(在、只有一份、值对)。这是逐键的情况;文件算不算完整看 `state` —— 完整的文件也可以有一个编码器键是 `false` |
| `bad` | 各自不合格的键的名字(= `tagOk` 里为 false 的;没有同步标记的文件为空) |
| `write` | 修的话,哪个键会写成什么(只列要写的;`state` 不是 `repairable` 时为空对象) |
| `actions` | 这个文件上能做的事:`repair`(`fix` 原地修复)、`wav_to_flac`(`fix --wav-to-flac`)、`copy_24bit`(`fix --copy-24bit`);一样都不能做时为空数组 |

汇总行里:`unrepairable` = `rate_unknown` + `damaged` + `unsupported_container`;`convertible` 单独数;`notScanned` = 被取消时还没查的个数。

### `tagrepair fix <文件>… --json`

每个文件一行(`"type":"fix"`),最后一行汇总。会独占打开文件:别的程序正开着它就拒绝(`in_use`)。

```
{"type":"fix","path":"D:\\Music\\a.flac","outcome":"fixed","code":"","reason":"","reasonEn":"","mode":"in_place","wrote":["ENCODER","MQAENCODER","ORIGINALSAMPLERATE"],"audioSha256":"098f27968fa3d3be3b3ee800584946870162b1f16e5a87a0c7cfdefffa4a89ba","audioBytes":794132,"pcmMd5":"cdc07ade9fc206ff08396961a16619cd","pcmFrames":132300,"backup":"","leftAt":"","staleRemoved":0,"before":{…与 scan 那一行同样的字段…},"tags":{"ENCODER":"MQAEncode v1.1, tag repair (unsigned)","MQAENCODER":"MQAEncode v1.1, tag repair (unsigned)","ORIGINALSAMPLERATE":"352800"},"tagCount":{"ENCODER":1,"MQAENCODER":1,"ORIGINALSAMPLERATE":1}}
{"type":"summary","files":1,"fixed":1,"wouldFix":0,"unchanged":0,"skipped":0,"refused":0,"failed":0,"cancelled":0,"notStarted":0}
```

| 字段 | 含义 |
| --- | --- |
| `outcome` | `fixed`(修好了)/ `unchanged`(本来就完整,没动)/ `would_fix`(`--dry-run`:可以修)/ `skipped`(不属于能修的那一类)/ `refused`(拒绝,文件没动)/ `failed`(失败)/ `cancelled`(被取消,文件没动) |
| `code` | 没修的原因(`fixed` / `unchanged` / `would_fix` 时为空):`skipped` 时是 `state` 的值;`cancelled` 时是 `cancelled`;`refused` / `failed` 时是 `read_only`、`in_use`、`access_denied`、`open_failed`、`too_big`、`damaged`、`decode_error`、`incomplete_audio`、`changed`、`verify_failed`、`swap_failed`、`swap_incomplete`、`io`、`internal` 之一 |
| `reason` / `reasonEn` | 同一个原因,中文(带细节)/ 英文各一句 |
| `mode` | `in_place`(原地改写)/ `rewrite`(整文件重写后换入);没写时为空 |
| `wrote` | 写了(或 `--dry-run` 时会写)哪几个键 |
| `audioSha256` / `audioBytes` | 音频帧到文件末尾这一段的 SHA-256 与字节数(修之前、修之后相同才算修好) |
| `pcmMd5` / `pcmFrames` | FFmpeg 整轨解出的样本的 MD5 与帧数(同上) |
| `backup` | `--keep-backup` 时原文件留在哪 |
| `leftAt` | 只在 `code` 是 `swap_incomplete` 时有:原来的名字下已经没有文件了,这个文件现在在这个路径 |
| `staleRemoved` | 顺手删掉的、上次被打断留下的临时文件个数 |
| `before` | 修之前的扫描结果(字段与 `scan` 那一行相同) |
| `tags` / `tagCount` | 只在 `fixed` 时有:修完之后读回的三个标签 |

汇总行里 `cancelled` = 做到一半停下的个数,`notStarted` = 被取消时还没开始的个数。

带 `--wav-to-flac` / `--copy-24bit` 时,`fix` 那些行之后还有每个文件一行的 `"type":"copy"`(每种另存各一遍),汇总行里多出同名的对象:

```
{"type":"copy","action":"wav_to_flac","path":"D:\\Music\\a.wav","outcome":"created","code":"","reason":"已另存 a.flac:24 bit,每声道 132300 个采样,逐个样本与原文件相同","reasonEn":"saved a.flac: 24 bit, 132300 samples per channel, every sample identical to the original","output":"D:\\Music\\a.flac","bits":24,"pcmFrames":132300,"pcmMd5":"cdc07ade9fc206ff08396961a16619cd","before":{…与 scan 那一行同样的字段…},"tags":{"ENCODER":"MQAEncode v1.1, tag repair (unsigned)","MQAENCODER":"MQAEncode v1.1, tag repair (unsigned)","ORIGINALSAMPLERATE":"352800"}}
{"type":"summary","files":1,"fixed":0,"wouldFix":0,"unchanged":0,"skipped":1,"refused":0,"failed":0,"cancelled":0,"notStarted":0,"wav_to_flac":{"created":1,"wouldCreate":0,"skipped":0,"refused":0,"failed":0,"cancelled":0,"notStarted":0}}
```

| 字段 | 含义 |
| --- | --- |
| `action` | `wav_to_flac` / `copy_24bit` |
| `outcome` | `created`(新文件写好并核对过)/ `would_create`(`--dry-run`)/ `skipped`(这个文件不属于这种另存的对象)/ `refused`(拒绝,什么都没写)/ `failed`(失败,没有留下新文件)/ `cancelled`(被取消,没有留下新文件) |
| `code` | `created` / `would_create` 时为空;`skipped` 时是 `state` 的值或 `not_16bit`;其余是 `target_exists`、`in_use`、`decode_error`、`incomplete_audio`、`no_encoder`、`encode_failed`、`verify_failed`、`io`、`cancelled` 之一 |
| `reason` / `reasonEn` | 同一句话,中文 / 英文(`created` 时说存成了什么) |
| `output` | 新文件的路径(`created` / `would_create` 时有) |
| `bits` / `pcmFrames` / `pcmMd5` | 只在 `created` 时有:新文件的位深、每声道的采样数、原文件全部采样的 MD5(新文件解出来的与它相同) |
| `before` | 原文件的扫描结果 |
| `tags` | 只在 `created` 时有:新文件里的三个标签 |

### 出错的那一行与退出码

程序自己没法干活时输出一行 `{"type":"error","code":"…","text":"…","textEn":"…"}`:`no_ffmpeg`(找不到运行库,退出码 3)、
`bad_option`(选项的值不能用、或者选项搭配不对,退出码 2)。退出码见上面「命令」一节的表。

## 用到的别人的东西

- 源代码:没有。这个仓库里的源代码全部是本工具的作者写的。
- FFmpeg 的四个动态库(`avformat` / `avcodec` / `avutil` / `swresample`,LGPL)在运行时加载;不编进来,也不在这个仓库里。
  构建时用到它的头文件。读文件、解码用它;另存时的 FLAC 音频帧由它的编码器编出。
- Windows 自带的 `bcrypt`(SHA-256 / MD5)。链接时静态带进可执行文件的还有 Visual Studio 工具链自带的 C / C++ 运行库和延迟加载辅助代码。

---

## English summary of the usage part

`tagrepair` restores the three identifying tags (`ENCODER`, `MQAENCODER`, `ORIGINALSAMPLERATE`) described in the notice above.
It is a stand-alone program; another program can attach it through the host protocol (`info --json`, `scan --json`,
`fix --json`: one JSON object per line, UTF-8; id `tagrepair`, protocol 1).

Without a command line: drop files or a folder onto `tagrepair.exe`. It scans read-only with a progress bar and lists what it
found in separate groups: files that can be repaired in place, WAV files that can be saved as a new FLAC file, and 16-bit files
of which a 24-bit copy can be saved. It asks one question per group, in that order, and acts only after an explicit `Y` to that
question; after a `Y` it says "Starting: …" at once and then shows progress. Everything on the console is written as
`English text (中文文字)`. `Ctrl+C` cancels at any moment: a file that is being written is finished or put back first, and
the program then says what was done and what was not.

Tags are complete when `ORIGINALSAMPLERATE` is acceptable and at least one of `ENCODER` / `MQAENCODER` is acceptable; such a
file is never touched. Only an incomplete file is repaired, and the repair makes all three keys acceptable.

Commands: `scan`, `fix` (`--recursive`, `--dry-run`, `--keep-backup`, `--json`, `--encoder-tag`, `--accept-prefix`,
`--wav-to-flac`, `--copy-24bit`, `--copy-only`), `selftest`, `info`; `--ffmpeg-dir` tells it where the FFmpeg DLLs are.
Exit codes: 0 ok, 1 a file was refused or failed (or the self-test failed), 2 bad usage, 3 FFmpeg run-time libraries not found,
4 cancelled.

The two copy actions are off unless asked for. `--wav-to-flac` writes `<name>.flac` next to a WAV file that contains the marker:
same sample rate, channels and bit depth, every sample value identical, with the three tags and the ordinary tags of the WAV.
`--copy-24bit` writes `<name>.24bit.flac` next to a 16-bit file (WAV or FLAC) that contains the marker: each 16-bit sample is
stored unchanged in the upper 16 bits of a 24-bit word, the low 8 bits are zero; nothing is recomputed. In both cases the
original is opened read-only and is never modified, renamed or deleted; an existing file is never overwritten; the new file is
written under a temporary name and renamed only after it has been decoded completely and compared with the original (sample
count and MD5 of all samples; for the 24-bit copy also every sample's low 8 bits and upper 16 bits) and scans as complete.

Safety of the in-place repair: exclusive open; read-only, in-use and damaged files and files without the marker are refused;
SHA-256 of the audio byte range and MD5 of the decoded PCM must match before and after; read-back by the tool's own parser and
by FFmpeg. Changes confined to one 512-byte sector are written in place; everything else goes through a complete new file next
to the original, swapped in with `ReplaceFileW`. If a swap stops half-way nothing is deleted and the surviving file's path is
reported.

New in the JSON output (protocol still 1): `scan` lines carry `actions` (`repair`, `wav_to_flac`, `copy_24bit`); a WAV file
with the marker has the state `convertible`; `fix` can report the outcome `cancelled`; the copy actions emit `"type":"copy"`
lines; the summary lines carry `convertible`, `notScanned`, `cancelled`, `notStarted` and one object per copy action.

Build: Windows, MSVC, CMake; `cmake -S . -B build -DFFMPEG_ROOT=<FFmpeg dev package>`, then build and `ctest`. The result
is `build\bin\tagrepair.exe`; the build script copies the four FFmpeg DLLs from the package you supplied next to it. All
user-visible strings are in `src/messages.h`, each written once in both languages.
