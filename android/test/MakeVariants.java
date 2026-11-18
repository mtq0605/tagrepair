import com.omni.tagrepair.core.*;

import java.io.*;
import java.nio.file.*;
import java.util.*;

/**
 * 从一个(标签完整的)FLAC 造出一批"标签坏了"的变体,两个版本各修一遍、逐字节对照用。只改元数据区,音频帧原样。不进安卓包。
 *   java MakeVariants <源.flac> <输出文件夹>
 */
public class MakeVariants {
    static byte[] src;
    static int metaAt, audioAt;

    public static void main(String[] a) throws Exception {
        src = Files.readAllBytes(Paths.get(a[0]));
        File out = new File(a[1]);
        out.mkdirs();
        FlacMeta.Layout l = new FlacMeta.Layout();
        if (FlacMeta.readLayout(new Rio.Mem(src), l).err != FlacMeta.Err.OK) throw new IllegalStateException("not flac");
        if (l.id3Bytes != 0) throw new IllegalStateException("has id3");
        metaAt = (int) l.metaAt;
        audioAt = (int) l.audioAt;
        List<FlacMeta.MetaBlock> base = FlacMeta.splitBlocks(Arrays.copyOfRange(src, metaAt, audioAt), audioAt - metaAt);

        write(out, "v01_strip", strip(base, true), null, null);
        write(out, "v02_strip_nopad", noPad(strip(base, true)), null, null);
        write(out, "v03_encoder_wrong", setKey(strip(base, true), "ENCODER=Lavf61.1.100"), null, null);
        List<FlacMeta.MetaBlock> d = strip(base, true);
        d = setKey(d, "ENCODER=Lavf61.1.100");
        d = setKey(d, "ENCODER=MQAEncode v1.1, 2.4.0+0 (a1b2c3d)");
        write(out, "v04_dup_encoder", d, null, null);
        write(out, "v05_lower_bad_rate", setKey(setKey(strip(base, true), "encoder=MQAEncode v1.1, x"), "originalsamplerate=0352800"), null, null);
        write(out, "v06_id3v2_strip", strip(base, true), id3(200), null);
        List<FlacMeta.MetaBlock> two = strip(base, true);
        two.add(1, vc("second-vendor", "ENCODER=foo", "TITLE=second block"));
        write(out, "v07_two_comment_blocks", two, null, null);
        write(out, "v08_no_comment_block", noComments(base), null, null);
        write(out, "v09_no_comment_no_pad", noPad(noComments(base)), null, null);
        write(out, "v10_tiny_pad", tinyPad(strip(base, true), 2), null, null);
        write(out, "v11_rate_wrong", setKey(strip(base, false), "ORIGINALSAMPLERATE=192000"), null, null);
        write(out, "v12_id3v1_tail", strip(base, true), null, id3v1());
        write(out, "v13_rate_only_missing", stripOnly(base, "ORIGINALSAMPLERATE"), null, null);
        write(out, "v14_mqaencoder_only", setKey(strip(base, true), "MQAENCODER=MQAEncode v1.1, 2.4.0+0"), null, null);
        write(out, "v15_tail_bytes_in_vc", tailBytes(strip(base, true)), null, null);
        write(out, "v16_big_pad_far", bigPadFirst(strip(base, true)), null, null);
        write(out, "v17_utf8_junk_value", setKey(strip(base, true), "ENCODER=ÿþ bad utf8"), null, null);
        System.out.println("ok");
    }

    static FlacMeta.MetaBlock vc(String vendor, String... items) {
        FlacMeta.Comments c = new FlacMeta.Comments();
        c.vendor = vendor;
        c.items.addAll(Arrays.asList(items));
        FlacMeta.MetaBlock b = newBlock(FlacMeta.VORBIS_COMMENT, FlacMeta.buildComments(c));
        return b;
    }

    static FlacMeta.MetaBlock newBlock(int type, byte[] body) {
        List<FlacMeta.MetaBlock> one = FlacMeta.splitBlocks(join(type, body), 4 + body.length);
        return one.get(0);
    }

    static byte[] join(int type, byte[] body) {
        byte[] o = new byte[4 + body.length];
        o[0] = (byte) (0x80 | type);
        o[1] = (byte) (body.length >> 16);
        o[2] = (byte) (body.length >> 8);
        o[3] = (byte) body.length;
        System.arraycopy(body, 0, o, 4, body.length);
        return o;
    }

    static boolean isKey(String it, String... keys) {
        int eq = it.indexOf('=');
        if (eq < 0) return false;
        for (String k : keys) if (it.substring(0, eq).equalsIgnoreCase(k)) return true;
        return false;
    }

    static List<FlacMeta.MetaBlock> editComments(List<FlacMeta.MetaBlock> in, java.util.function.Consumer<FlacMeta.Comments> f) {
        List<FlacMeta.MetaBlock> o = new ArrayList<>();
        boolean done = false;
        for (FlacMeta.MetaBlock b : in) {
            if (b.type == FlacMeta.VORBIS_COMMENT && !done) {
                FlacMeta.Comments c = FlacMeta.parseComments(b.body, 0, b.body.length);
                f.accept(c);
                o.add(newBlock(FlacMeta.VORBIS_COMMENT, FlacMeta.buildComments(c)));
                done = true;
            } else o.add(newBlock(b.type, b.body.clone()));
        }
        return o;
    }

    static List<FlacMeta.MetaBlock> strip(List<FlacMeta.MetaBlock> in, boolean rateToo) {
        return editComments(in, c -> c.items.removeIf(it -> rateToo ? isKey(it, "ENCODER", "MQAENCODER", "ORIGINALSAMPLERATE") : isKey(it, "ENCODER", "MQAENCODER")));
    }

    static List<FlacMeta.MetaBlock> stripOnly(List<FlacMeta.MetaBlock> in, String key) {
        return editComments(in, c -> c.items.removeIf(it -> isKey(it, key)));
    }

    static List<FlacMeta.MetaBlock> setKey(List<FlacMeta.MetaBlock> in, String item) {
        return editComments(in, c -> c.items.add(item));
    }

    static List<FlacMeta.MetaBlock> tailBytes(List<FlacMeta.MetaBlock> in) {
        return editComments(in, c -> c.tail = new byte[] { 1 });
    }

    static List<FlacMeta.MetaBlock> noPad(List<FlacMeta.MetaBlock> in) {
        List<FlacMeta.MetaBlock> o = new ArrayList<>();
        for (FlacMeta.MetaBlock b : in) if (b.type != FlacMeta.PADDING) o.add(b);
        return o;
    }

    static List<FlacMeta.MetaBlock> noComments(List<FlacMeta.MetaBlock> in) {
        List<FlacMeta.MetaBlock> o = new ArrayList<>();
        for (FlacMeta.MetaBlock b : in) if (b.type != FlacMeta.VORBIS_COMMENT) o.add(b);
        return o;
    }

    static List<FlacMeta.MetaBlock> tinyPad(List<FlacMeta.MetaBlock> in, int n) {
        List<FlacMeta.MetaBlock> o = noPad(in);
        o.add(newBlock(FlacMeta.PADDING, new byte[n]));
        return o;
    }

    static List<FlacMeta.MetaBlock> bigPadFirst(List<FlacMeta.MetaBlock> in) {
        List<FlacMeta.MetaBlock> o = noPad(in);
        o.add(1, newBlock(FlacMeta.PADDING, new byte[9000]));
        return o;
    }

    static byte[] id3(int body) {
        byte[] h = new byte[10 + body];
        h[0] = 'I'; h[1] = 'D'; h[2] = '3'; h[3] = 3; h[4] = 0; h[5] = 0;
        h[6] = (byte) ((body >> 21) & 0x7F); h[7] = (byte) ((body >> 14) & 0x7F); h[8] = (byte) ((body >> 7) & 0x7F); h[9] = (byte) (body & 0x7F);
        return h;
    }

    static byte[] id3v1() {
        byte[] t = new byte[128];
        t[0] = 'T'; t[1] = 'A'; t[2] = 'G';
        return t;
    }

    static void write(File dir, String name, List<FlacMeta.MetaBlock> blocks, byte[] prefix, byte[] suffix) throws IOException {
        byte[] meta = FlacMeta.joinBlocks(blocks);
        try (OutputStream o = new BufferedOutputStream(new FileOutputStream(new File(dir, name + ".flac")))) {
            if (prefix != null) o.write(prefix);
            o.write(src, 0, metaAt - 4 + 4);       // "fLaC"
            o.write(meta);
            o.write(src, audioAt, src.length - audioAt);
            if (suffix != null) o.write(suffix);
        }
    }
}
