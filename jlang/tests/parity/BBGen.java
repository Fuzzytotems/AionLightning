import java.nio.*;
public class BBGen {
  static long seed = 777;
  static int next() { seed = seed * 6364136223846793005L + 1442695040888963407L; return (int)(seed >>> 33); }
  static int rnd(int lo, int hi) { return lo + (int)((next() & 0x7fffffffL) % (hi - lo + 1)); }
  static StringBuilder out = new StringBuilder();
  static ByteBuffer[] bufs = new ByteBuffer[6];
  static String st(ByteBuffer b) { return "p" + b.position() + "l" + b.limit() + "c" + b.capacity() + (b.order() == ByteOrder.BIG_ENDIAN ? "B" : "L"); }
  static String arr(byte[] a) { StringBuilder s = new StringBuilder(); for (byte x : a) s.append(Integer.toHexString(x & 0xff)).append('.'); return s.toString(); }
  public static void main(String[] args) {
    bufs[0] = ByteBuffer.allocate(24);
    for (int i = 1; i < bufs.length; i++) bufs[i] = null;
    for (int step = 0; step < 4000; step++) {
      int bi = rnd(0, bufs.length - 1);
      if (bufs[bi] == null) bi = 0;
      ByteBuffer b = bufs[bi];
      int op = rnd(0, 44);
      String res;
      try {
        switch (op) {
          case 0: res = "" + b.position(); break;
          case 1: { int n = rnd(-2, b.capacity() + 2); b.position(n); res = "pos" + n; break; }
          case 2: res = "" + b.limit(); break;
          case 3: { int n = rnd(-2, b.capacity() + 2); b.limit(n); res = "lim" + n; break; }
          case 4: b.mark(); res = "mark"; break;
          case 5: b.reset(); res = "reset"; break;
          case 6: b.clear(); res = "clear"; break;
          case 7: b.flip(); res = "flip"; break;
          case 8: b.rewind(); res = "rewind"; break;
          case 9: res = "rem" + b.remaining() + b.hasRemaining(); break;
          case 10: b.compact(); res = "compact"; break;
          case 11: res = "g" + b.get(); break;
          case 12: { int i = rnd(-1, b.capacity()); res = "g" + i + "=" + b.get(i); break; }
          case 13: { byte v = (byte) next(); b.put(v); res = "p" + v; break; }
          case 14: { int i = rnd(-1, b.capacity()); byte v = (byte) next(); b.put(i, v); res = "p" + i + "=" + v; break; }
          case 15: res = "s" + b.getShort(); break;
          case 16: { int i = rnd(-1, b.capacity()); res = "s" + i + "=" + b.getShort(i); break; }
          case 17: { short v = (short) next(); b.putShort(v); res = "ps" + v; break; }
          case 18: { int i = rnd(-1, b.capacity()); short v = (short) next(); b.putShort(i, v); res = "ps" + i + "=" + v; break; }
          case 19: res = "i" + b.getInt(); break;
          case 20: { int i = rnd(-1, b.capacity()); res = "i" + i + "=" + b.getInt(i); break; }
          case 21: { int v = next(); b.putInt(v); res = "pi" + v; break; }
          case 22: { int i = rnd(-1, b.capacity()); int v = next(); b.putInt(i, v); res = "pi" + i + "=" + v; break; }
          case 23: res = "L" + b.getLong(); break;
          case 24: { long v = ((long) next() << 32) ^ next(); b.putLong(v); res = "pL" + v; break; }
          case 25: res = "c" + (int) b.getChar(); break;
          case 26: { char v = (char) next(); b.putChar(v); res = "pc" + (int) v; break; }
          case 27: res = "f" + Integer.toHexString(Float.floatToIntBits(b.getFloat())); break;
          case 28: { float v = next() / 7.0f; b.putFloat(v); res = "pf"; break; }
          case 29: res = "d" + Long.toHexString(Double.doubleToLongBits(b.getDouble())); break;
          case 30: { double v = next() / 3.0; b.putDouble(v); res = "pd"; break; }
          case 31: b.order(ByteOrder.LITTLE_ENDIAN); res = "LE"; break;
          case 32: b.order(ByteOrder.BIG_ENDIAN); res = "BE"; break;
          case 33: { int t = rnd(1, bufs.length - 1); bufs[t] = b.slice(); res = "slice>" + t + ":" + st(bufs[t]) + " ao" + bufs[t].arrayOffset(); break; }
          case 34: { int t = rnd(1, bufs.length - 1); bufs[t] = b.duplicate(); res = "dup>" + t + ":" + st(bufs[t]); break; }
          case 35: { int len = rnd(0, 6); byte[] d = new byte[len + 2]; int off = rnd(-1, 3); int l = rnd(-1, len); b.get(d, off, l); res = "ga" + off + "," + l + ":" + arr(d); break; }
          case 36: { int len = rnd(0, 6); byte[] s = new byte[len]; for (int i = 0; i < len; i++) s[i] = (byte) next(); int off = rnd(-1, 2); int l = rnd(-1, len); b.put(s, off, l); res = "pa" + off + "," + l; break; }
          case 37: { int t = rnd(0, bufs.length - 1); ByteBuffer o = bufs[t] == null ? b : bufs[t]; b.put(o); res = "pb" + t + ":" + st(o); break; }
          case 38: res = "arr" + arr(b.array()) + " ao" + b.arrayOffset(); break;
          case 39: res = b.toString(); break;
          case 40: { int t = rnd(0, bufs.length - 1); ByteBuffer o = bufs[t] == null ? bufs[0] : bufs[t]; res = "eq" + b.equals(o) + " cmp" + Integer.signum(b.compareTo(o)) + " h" + b.hashCode(); break; }
          case 41: { int t = rnd(1, bufs.length - 1); byte[] a = new byte[rnd(0, 12)]; for (int i = 0; i < a.length; i++) a[i] = (byte) i; int off = rnd(-1, a.length); int l = rnd(-1, a.length); bufs[t] = ByteBuffer.wrap(a, off, l); res = "wrap>" + t + ":" + st(bufs[t]); break; }
          case 42: { ByteBuffer r = b.asReadOnlyBuffer(); res = "ro" + st(r) + r.isReadOnly() + r.hasArray(); r.put((byte)1); break; }
          case 43: { CharBuffer cb = b.asCharBuffer(); String s = "ABé中".substring(0, rnd(0, 4)); res = "cb" + cb.position() + "," + cb.limit() + "," + cb.capacity(); cb.put(s); res += "ok" + cb.position(); break; }
          default: { int t = rnd(1, bufs.length - 1); ByteBuffer o = ByteBuffer.allocate(rnd(0, 20)); bufs[t] = o; res = "alloc>" + t + ":" + st(o); break; }
        }
      } catch (Exception e) {
        res = "EX:" + e.getClass().getSimpleName();
      }
      out.append(step).append(' ').append(bi).append(' ').append(op).append(' ').append(res).append(' ').append(st(b)).append('\n');
    }
    System.out.print(out);
  }
}
