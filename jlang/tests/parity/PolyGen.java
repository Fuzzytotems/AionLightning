import java.awt.*;
public class PolyGen {
  static long seed = 12345;
  static int next() { seed = seed * 6364136223846793005L + 1442695040888963407L; return (int)(seed >>> 33); }
  static int rnd(int lo, int hi) { return lo + (int)((next() & 0x7fffffffL) % (hi - lo + 1)); }
  public static void main(String[] a) {
    StringBuilder bits = new StringBuilder();
    int acc = 0, nb = 0, count = 0, trues = 0;
    for (int p = 0; p < 3000; p++) {
      int mode = p % 4;
      int n = rnd(0, 9);
      Polygon poly = new Polygon();
      int span = mode == 0 ? 4 : mode == 1 ? 100 : mode == 2 ? 1000000000 : 2147483647;
      for (int i = 0; i < n; i++) {
        int x, y;
        if (mode == 3) { x = next(); y = next(); if ((next() & 3) == 0) x = (next() & 1) == 0 ? Integer.MAX_VALUE : Integer.MIN_VALUE; }
        else { x = rnd(-span, span); y = rnd(-span, span); }
        poly.addPoint(x, y);
      }
      // interleave contains calls with addPoint to exercise bounds caching
      for (int t = 0; t < 12; t++) {
        boolean r;
        int kind = t % 3;
        if (kind == 0) {
          int x, y;
          if (mode == 0) { x = rnd(-6, 6); y = rnd(-6, 6); }
          else if (mode == 1) { x = rnd(-110, 110); y = rnd(-110, 110); }
          else if (n > 0 && (next() & 1) == 0) { int k = rnd(0, n - 1); x = poly.xpoints[k] + rnd(-1, 1); y = poly.ypoints[k] + rnd(-1, 1); }
          else { x = next(); y = next(); }
          r = poly.contains(x, y);
        } else if (kind == 1) {
          double x, y;
          if (mode == 0) { x = rnd(-24, 24) / 4.0; y = rnd(-24, 24) / 4.0; }
          else if (mode == 1) { x = rnd(-440, 440) / 4.0; y = rnd(-440, 440) / 4.0; }
          else { x = (double) next() + rnd(0, 3) / 4.0; y = (double) next() + rnd(0, 3) / 4.0; }
          r = poly.contains(x, y);
        } else {
          Point pt = new Point(rnd(-6, 6), rnd(-6, 6));
          if (mode != 0) pt = new Point(rnd(-110, 110), rnd(-110, 110));
          r = poly.contains(pt);
        }
        if (r) { acc |= 1 << nb; trues++; }
        nb++; count++;
        if (nb == 4) { bits.append(Integer.toHexString(acc)); acc = 0; nb = 0; }
      }
      if (p % 7 == 0) { // add a point after contains() was called (bounds updated incrementally)
        poly.addPoint(rnd(-span/2, span/2), rnd(-span/2, span/2));
        for (int t = 0; t < 4; t++) {
          boolean r = poly.contains(rnd(-6, 6), rnd(-6, 6));
          if (r) { acc |= 1 << nb; trues++; }
          nb++; count++;
          if (nb == 4) { bits.append(Integer.toHexString(acc)); acc = 0; nb = 0; }
        }
        Rectangle b = poly.getBounds();
        bits.append("|" + b.x + "," + b.y + "," + b.width + "," + b.height + "|");
      }
    }
    if (nb > 0) bits.append(Integer.toHexString(acc));
    System.err.println("count=" + count + " trues=" + trues);
    System.out.print(bits);
  }
}
