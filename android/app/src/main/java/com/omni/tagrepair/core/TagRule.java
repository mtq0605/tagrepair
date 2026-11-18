package com.omni.tagrepair.core;

import java.nio.charset.StandardCharsets;

/**
 * 三个识别标签的判定规则(纯函数)。逐条对着 Windows 版的 src/tag_rule.h 写,定义与那边相同:
 *   ENCODER / MQAENCODER   值以 ACCEPT_PREFIX 开头,或者正好是修复时写进去的那个值
 *   ORIGINALSAMPLERATE     十进制整数(没有空白、正负号、前导 0,最多 9 位),等于码流里的原始采样率(没读到时只要求正整数)
 *   一个键要合格,还得恰好出现一次
 *   标签完整 = ORIGINALSAMPLERATE 合格,并且两个编码器键里至少一个合格(tagsComplete,只在这一处)
 *
 * 标签的值是文件里的原样字节,不一定是合法的 UTF-8。这里一律用"字节串":Java 的 String,每个 char 就是一个字节(0..255,
 * 按 ISO-8859-1 装)。比较、找前缀、找分号都是逐字节的,与 Windows 版的 std::string 完全一样;要给人看时用 shown() 按 UTF-8 解。
 */
public final class TagRule {
    private TagRule() {}

    public static final int ENCODER = 0, FORMAT_ENCODER = 1, ORIG_RATE = 2, KEYS = 3;
    public static final int BAD_ENCODER = 1 << ENCODER, BAD_FORMAT_ENCODER = 1 << FORMAT_ENCODER, BAD_ORIG_RATE = 1 << ORIG_RATE;

    private static final String[] NAMES = { "ENCODER", "MQAENCODER", "ORIGINALSAMPLERATE" };
    public static String keyName(int k) { return k >= 0 && k < KEYS ? NAMES[k] : ""; }

    /** 修复时写进 ENCODER / MQAENCODER 的值(与 Windows 版 kDefaultEncoderTag 相同)。 */
    public static final String DEFAULT_ENCODER_TAG = "MQAEncode v1.1, tag repair (unsigned)";
    /** 已有的值以它开头就合格(与 Windows 版 kDefaultAcceptPrefix 相同)。 */
    public static final String DEFAULT_ACCEPT_PREFIX = "MQAEncode";
    public static final String WRITER_MARK = "tag repair";
    public static final String UNSIGNED_MARK = "(unsigned)";

    /** 一次扫描 / 修复用的规则。安卓版只用默认的这一套。 */
    public static final class Rule {
        public final String encoderTag, acceptPrefix;       // 字节串
        public Rule(String tag, String prefix) { encoderTag = tag; acceptPrefix = prefix; }
    }
    public static final Rule DEFAULT = new Rule(DEFAULT_ENCODER_TAG, DEFAULT_ACCEPT_PREFIX);

    // ---- 字节串 ----
    public static String bytesToB(byte[] d, int off, int n) { return new String(d, off, n, StandardCharsets.ISO_8859_1); }
    public static byte[] bToBytes(String b) { return b.getBytes(StandardCharsets.ISO_8859_1); }
    /** 给人看:按 UTF-8 解(不合法的字节换成 U+FFFD) */
    public static String shown(String b) { return new String(bToBytes(b), StandardCharsets.UTF_8); }

    static char lowerAscii(char c) { return c >= 'A' && c <= 'Z' ? (char) (c + 32) : c; }

    /** 键名比较:Vorbis 注释的键不分大小写(只有 ASCII) */
    public static boolean keyEquals(String a, String b) {
        if (a.length() != b.length()) return false;
        for (int i = 0; i < a.length(); i++) if (lowerAscii(a.charAt(i)) != lowerAscii(b.charAt(i))) return false;
        return true;
    }

    public static boolean encoderValueOk(String v, Rule rule) {
        if (v.isEmpty()) return false;
        return v.equals(rule.encoderTag) || v.startsWith(rule.acceptPrefix);
    }

    /** "352800" → 352800;不合规矩返回 −1 */
    public static int parseRate(String v) {
        if (v.isEmpty() || v.length() > 9 || v.charAt(0) == '0') return -1;
        int n = 0;
        for (int i = 0; i < v.length(); i++) {
            char c = v.charAt(i);
            if (c < '0' || c > '9') return -1;
            n = n * 10 + (c - '0');
        }
        return n;
    }

    public static boolean origRateValueOk(String v, int streamOrigRate) {
        int hz = parseRate(v);
        if (hz < 0) return false;
        return streamOrigRate <= 0 || hz == streamOrigRate;
    }

    public static boolean valueOk(int k, String v, int streamOrigRate, Rule rule) {
        return k == ORIG_RATE ? origRateValueOk(v, streamOrigRate) : encoderValueOk(v, rule);
    }

    /** 文件里看到的:每个键出现了几次、第一次出现的值(字节串) */
    public static final class Seen {
        public final int[] count = new int[KEYS];
        public final String[] value = { "", "", "" };
    }

    /** 每个键各自合不合格(位图:不合格的键的位是 1) */
    public static int problems(Seen s, int streamOrigRate, Rule rule) {
        int bad = 0;
        for (int k = 0; k < KEYS; k++)
            if (s.count[k] != 1 || !valueOk(k, s.value[k], streamOrigRate, rule)) bad |= 1 << k;
        return bad;
    }

    public static int problems(Seen s, int streamOrigRate) { return problems(s, streamOrigRate, DEFAULT); }

    /** 标签完整不完整 —— 规则只在这一处 */
    public static boolean tagsComplete(int bad) {
        int both = BAD_ENCODER | BAD_FORMAT_ENCODER;
        return (bad & BAD_ORIG_RATE) == 0 && (bad & both) != both;
    }

    // ---- 从容器的元数据字典看(WAV 用):同名的条目被接成 "a;b",按分号拆开,有一段合格就算合格 ----
    public static boolean joinedValueOk(int k, String joined, int streamOrigRate, Rule rule) {
        return firstAcceptable(k, joined, streamOrigRate, rule) != null;
    }

    /** 接起来的值里第一段合格的;一段都不合格返回 null */
    public static String firstAcceptable(int k, String joined, int streamOrigRate, Rule rule) {
        int a = 0;
        while (true) {
            int b = joined.indexOf(';', a);
            String part = b < 0 ? joined.substring(a) : joined.substring(a, b);
            if (valueOk(k, part, streamOrigRate, rule)) return part;
            if (b < 0) return null;
            a = b + 1;
        }
    }

    public static boolean validUtf8(String b) {
        int n = b.length();
        for (int i = 0; i < n; ) {
            int c = b.charAt(i);
            int need, cp, floor;
            if (c < 0x80) { i++; continue; }
            else if ((c & 0xE0) == 0xC0) { need = 1; cp = c & 0x1F; floor = 0x80; }
            else if ((c & 0xF0) == 0xE0) { need = 2; cp = c & 0x0F; floor = 0x800; }
            else if ((c & 0xF8) == 0xF0) { need = 3; cp = c & 0x07; floor = 0x10000; }
            else return false;
            if (n - i <= need) return false;
            for (int k = 1; k <= need; k++) {
                int x = b.charAt(i + k);
                if ((x & 0xC0) != 0x80) return false;
                cp = (cp << 6) | (x & 0x3F);
            }
            if (cp < floor || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) return false;
            i += need + 1;
        }
        return true;
    }
}
