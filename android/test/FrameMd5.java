import com.omni.tagrepair.core.*;
import java.io.RandomAccessFile;
/** 每帧样本的 MD5(与 ffmpeg -f framemd5 对照找第一个不同的帧) */
public class FrameMd5 {
    public static void main(String[] a) throws Exception {
        RandomAccessFile raf = new RandomAccessFile(a[0], "r");
        Rio rio = new ChannelRio(raf.getChannel(), false);
        Probe.Info info = Probe.inspect(rio);
        final int[] idx = {0};
        final byte[] buf = new byte[1 << 20];
        new FlacDecoder(rio, info.layout).decodeAll(new FlacDecoder.Sink() {
            public boolean frame(int[] lr, int n, int c) {
                java.security.MessageDigest md = Crc.md("MD5");
                Crc.updateInt32(md, lr, n * c, buf);
                System.out.println(idx[0]++ + " " + n + " " + Crc.hex(md.digest()));
                return true;
            }
        }, null);
    }
}
