/*
 * Differential test: original geoEngine-0.1.jar vs the classes recompiled from geoengine/src.
 *
 * Generates synthetic geodata (meshs.geo + several <worldId>.geo files, see ../README.md for the
 * formats), loads it with BOTH implementations in isolated class loaders, and compares:
 *   - the result of GeoWorldLoader.loadMeshs()/loadWorld() (return value or exception),
 *   - a dump of the whole loaded scene graph (node names, child counts, world bounds, mesh sizes),
 *   - GeoMap.getZ(x, y, z) (bit-exact float, or exception class + message),
 *   - GeoMap.canSee(x, y, z, tx, ty, tz) (boolean, or exception class + message)
 * over many random and targeted queries.  Exits with status 1 on the first mismatch.
 *
 * Usage:
 *   javac -d <out> GeoDiffTest.java
 *   java -cp <out> GeoDiffTest <referenceClasspath> <candidateClasspath> <scratchDataDir> [queriesPerKind] [seed]
 *
 * The geoEngine classes are never on this program's own class path; they are only reached through
 * reflection, so both implementations can be loaded side by side in one JVM.
 */
import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.lang.reflect.Constructor;
import java.lang.reflect.InvocationTargetException;
import java.lang.reflect.Method;
import java.net.URL;
import java.net.URLClassLoader;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;
import java.util.Random;

public class GeoDiffTest {

    // ------------------------------------------------------------------ implementation wrapper

    static final class Impl {
        final String label;
        final ClassLoader cl;
        final Method setGeoDir, loadMeshs, loadWorld, getZ, canSee, getChildren, getWorldBound, getName, getMesh,
                meshTriangleCount, meshVertexCount;
        final Constructor<?> geoMapCtor;
        final Method terrainZ;
        final Class<?> nodeClass, geometryClass;

        Impl(String label, String classpath) throws Exception {
            this.label = label;
            String[] parts = classpath.split(File.pathSeparator);
            URL[] urls = new URL[parts.length];
            for (int i = 0; i < parts.length; i++) {
                urls[i] = new File(parts[i]).toURI().toURL();
            }
            cl = new URLClassLoader(urls, ClassLoader.getPlatformClassLoader());
            Class<?> loader = Class.forName("aionjHungary.geoEngine.GeoWorldLoader", true, cl);
            Class<?> geoMap = Class.forName("aionjHungary.geoEngine.models.GeoMap", true, cl);
            nodeClass = Class.forName("aionjHungary.geoEngine.scene.Node", true, cl);
            geometryClass = Class.forName("aionjHungary.geoEngine.scene.Geometry", true, cl);
            Class<?> spatial = Class.forName("aionjHungary.geoEngine.scene.Spatial", true, cl);
            Class<?> mesh = Class.forName("aionjHungary.geoEngine.scene.Mesh", true, cl);
            if (geoMap.getClassLoader() != cl) {
                throw new IllegalStateException(label + ": geoEngine leaked onto the application class path");
            }
            setGeoDir = loader.getMethod("setGeoDir", String.class);
            loadMeshs = loader.getMethod("loadMeshs");
            loadWorld = loader.getMethod("loadWorld", int.class, Map.class, geoMap);
            geoMapCtor = geoMap.getConstructor(String.class, int.class);
            getZ = geoMap.getMethod("getZ", float.class, float.class, float.class);
            terrainZ = geoMap.getDeclaredMethod("getZ", float.class, float.class); // private: terrain height only
            terrainZ.setAccessible(true);
            canSee = geoMap.getMethod("canSee", float.class, float.class, float.class, float.class, float.class, float.class);
            getChildren = nodeClass.getMethod("getChildren");
            getWorldBound = spatial.getMethod("getWorldBound");
            getName = spatial.getMethod("getName");
            getMesh = geometryClass.getMethod("getMesh");
            meshTriangleCount = mesh.getMethod("getTriangleCount");
            meshVertexCount = mesh.getMethod("getVertexCount");
        }

        Object call(Method m, Object target, Object... args) throws Throwable {
            try {
                return m.invoke(target, args);
            } catch (InvocationTargetException e) {
                throw e.getCause();
            }
        }

        /** Deterministic text dump of a loaded scene graph. */
        void dump(Object spatial, String indent, StringBuilder sb) throws Throwable {
            Object bound = call(getWorldBound, spatial);
            sb.append(indent).append(spatial.getClass().getSimpleName()).append(" '").append(call(getName, spatial))
              .append("' bound=").append(bound);
            if (geometryClass.isInstance(spatial)) {
                Object mesh = call(getMesh, spatial);
                sb.append(" tris=").append(call(meshTriangleCount, mesh)).append(" verts=").append(call(meshVertexCount, mesh));
            }
            sb.append('\n');
            if (nodeClass.isInstance(spatial)) {
                for (Object child : (List<?>) call(getChildren, spatial)) {
                    dump(child, indent + "  ", sb);
                }
            }
        }
    }

    /*
     * HotSpot replaces frequently thrown implicit exceptions (e.g. ArrayIndexOutOfBoundsException) by a
     * preallocated instance without message once the throwing code is JIT-compiled, which makes
     * messages depend on JIT timing.  Run with -XX:-OmitStackTraceInFastThrow to compare messages too;
     * otherwise only the exception classes are compared.
     */
    static final boolean WITH_MESSAGES = java.lang.management.ManagementFactory.getRuntimeMXBean().getInputArguments()
            .contains("-XX:-OmitStackTraceInFastThrow");

    static String describe(Throwable t) {
        return "EXC " + t.getClass().getName() + (WITH_MESSAGES ? ": " + t.getMessage() : "");
    }

    static final java.util.TreeMap<String, Integer> EXCEPTIONS = new java.util.TreeMap<String, Integer>();

    static void countException(String what, String result) {
        if (result.startsWith("EXC ")) {
            String cls = result.substring(4).split(":")[0];
            String key = what + " " + cls.substring(cls.lastIndexOf('.') + 1);
            Integer n = EXCEPTIONS.get(key);
            EXCEPTIONS.put(key, n == null ? 1 : n + 1);
        }
    }

    // ------------------------------------------------------------------ synthetic data

    static final class Writer {
        final ByteBuffer b = ByteBuffer.allocate(8 << 20).order(ByteOrder.LITTLE_ENDIAN);

        void name(String s) {
            byte[] n = s.getBytes(java.nio.charset.StandardCharsets.US_ASCII);
            b.putShort((short) n.length);
            b.put(n);
        }

        void save(File f) throws IOException {
            try (FileOutputStream out = new FileOutputStream(f)) {
                out.write(b.array(), 0, b.position());
            }
        }
    }

    /** One mesh: vertices (x,y,z triples) and a triangle index list. */
    static final class MeshData {
        final List<float[]> verts = new ArrayList<float[]>();
        final List<Integer> idx = new ArrayList<Integer>();

        int v(float x, float y, float z) {
            verts.add(new float[] {x, y, z});
            return verts.size() - 1;
        }

        void tri(int a, int b, int c) {
            idx.add(a);
            idx.add(b);
            idx.add(c);
        }

        void write(Writer w) {
            w.b.putShort((short) verts.size());
            for (float[] p : verts) {
                w.b.putFloat(p[0]).putFloat(p[1]).putFloat(p[2]);
            }
            w.b.putShort((short) idx.size());
            for (int i : idx) {
                w.b.putShort((short) i);
            }
        }
    }

    static MeshData box(float x0, float y0, float z0, float x1, float y1, float z1) {
        MeshData m = new MeshData();
        int[] c = new int[8];
        for (int i = 0; i < 8; i++) {
            c[i] = m.v((i & 1) == 0 ? x0 : x1, (i & 2) == 0 ? y0 : y1, (i & 4) == 0 ? z0 : z1);
        }
        int[][] faces = {{0, 1, 3, 2}, {4, 6, 7, 5}, {0, 4, 5, 1}, {2, 3, 7, 6}, {0, 2, 6, 4}, {1, 5, 7, 3}};
        for (int[] f : faces) {
            m.tri(c[f[0]], c[f[1]], c[f[2]]);
            m.tri(c[f[0]], c[f[2]], c[f[3]]);
        }
        return m;
    }

    static MeshData quad(float x0, float y0, float x1, float y1, float z) {
        MeshData m = new MeshData();
        int a = m.v(x0, y0, z), b = m.v(x1, y0, z), c = m.v(x1, y1, z), d = m.v(x0, y1, z);
        m.tri(a, b, c);
        m.tri(a, c, d);
        return m;
    }

    static MeshData soup(Random r, int tris, float extent, float height, float size) {
        MeshData m = new MeshData();
        for (int i = 0; i < tris; i++) {
            float cx = (r.nextFloat() * 2 - 1) * extent, cy = (r.nextFloat() * 2 - 1) * extent, cz = r.nextFloat() * height;
            int a = m.v(cx + (r.nextFloat() - 0.5f) * size, cy + (r.nextFloat() - 0.5f) * size, cz + (r.nextFloat() - 0.5f) * size);
            int b = m.v(cx + (r.nextFloat() - 0.5f) * size, cy + (r.nextFloat() - 0.5f) * size, cz + (r.nextFloat() - 0.5f) * size);
            int c = m.v(cx + (r.nextFloat() - 0.5f) * size, cy + (r.nextFloat() - 0.5f) * size, cz + (r.nextFloat() - 0.5f) * size);
            m.tri(a, b, c);
        }
        return m;
    }

    static MeshData grid(Random r, int n, float cell, float amp) {
        MeshData m = new MeshData();
        float off = n * cell / 2;
        for (int i = 0; i <= n; i++) {
            for (int j = 0; j <= n; j++) {
                m.v(i * cell - off, j * cell - off, (float) (amp * Math.sin(i * 0.37) * Math.cos(j * 0.23)) + r.nextFloat());
            }
        }
        for (int i = 0; i < n; i++) {
            for (int j = 0; j < n; j++) {
                int a = i * (n + 1) + j, b = a + 1, c = a + n + 1, d = c + 1;
                m.tri(a, c, d);
                m.tri(a, d, b);
            }
        }
        return m;
    }

    static MeshData walls(Random r) {
        MeshData m = new MeshData();
        // vertical walls (parallel to the vertical getZ rays)
        for (int i = 0; i < 20; i++) {
            float x = (r.nextFloat() - 0.5f) * 30, y = (r.nextFloat() - 0.5f) * 30;
            int a = m.v(x, y, 0), b = m.v(x + 5, y, 0), c = m.v(x + 5, y, 10), d = m.v(x, y, 10);
            m.tri(a, b, c);
            m.tri(a, c, d);
        }
        // degenerate triangles: repeated vertex, collinear points, and tiny slivers
        for (int i = 0; i < 20; i++) {
            float x = (r.nextFloat() - 0.5f) * 30, y = (r.nextFloat() - 0.5f) * 30, z = r.nextFloat() * 10;
            int a = m.v(x, y, z), b = m.v(x + 1, y + 1, z + 1), c = m.v(x + 2, y + 2, z + 2);
            m.tri(a, a, b);
            m.tri(a, b, c);
            int d = m.v(x, y, z + 1e-6f), e = m.v(x + 1e-6f, y, z), f = m.v(x, y + 1e-6f, z);
            m.tri(d, e, f);
        }
        // axis-aligned horizontal plates at integer heights (exact hits)
        for (int i = 0; i < 10; i++) {
            float x = Math.round((r.nextFloat() - 0.5f) * 30), y = Math.round((r.nextFloat() - 0.5f) * 30);
            int a = m.v(x, y, i), b = m.v(x + 4, y, i), c = m.v(x + 4, y + 4, i), d = m.v(x, y + 4, i);
            m.tri(a, b, c);
            m.tri(a, c, d);
        }
        return m;
    }

    static void model(Writer w, String name, MeshData... meshes) {
        w.name(name);
        w.b.putShort((short) meshes.length);
        for (MeshData m : meshes) {
            m.write(w);
        }
    }

    static final String[] PLACEABLE = {"box", "plate", "pyramid", "multi", "soup", "ground", "walls", "tower", "dup", "bigsoup",
            "BOX", "Pyramid", "caps", "nosuchmodel", "empty"};

    static void writeMeshs(File dir, Random r) throws IOException {
        Writer w = new Writer();
        model(w, "box", box(-5, -5, -5, 5, 5, 5));
        model(w, "plate", quad(-10, -10, 10, 10, 0));
        MeshData pyr = new MeshData();
        int p0 = pyr.v(-6, -6, 0), p1 = pyr.v(6, -6, 0), p2 = pyr.v(6, 6, 0), p3 = pyr.v(-6, 6, 0), apex = pyr.v(0, 0, 12);
        pyr.tri(p0, p1, apex);
        pyr.tri(p1, p2, apex);
        pyr.tri(p2, p3, apex);
        pyr.tri(p3, p0, apex);
        pyr.tri(p0, p2, p1);
        pyr.tri(p0, p3, p2);
        model(w, "pyramid", pyr);
        MeshData tilted = new MeshData();
        int t0 = tilted.v(-8, -8, 2), t1 = tilted.v(8, -8, 6), t2 = tilted.v(8, 8, 9), t3 = tilted.v(-8, 8, 4);
        tilted.tri(t0, t1, t2);
        tilted.tri(t0, t2, t3);
        model(w, "multi", box(-3, -3, 0, 3, 3, 4), quad(-12, -12, -4, -4, 1), tilted);
        model(w, "soup", soup(r, 1500, 25, 30, 8));
        model(w, "ground", grid(r, 30, 2, 3));
        model(w, "walls", walls(r));
        model(w, "tower", box(-1, -1, 0, 1, 1, 60));
        model(w, "dup", box(-2, -2, -2, 2, 2, 2));
        model(w, "dup", soup(r, 50, 6, 6, 3)); // a later record with the same name replaces the earlier one
        model(w, "empty");                    // meshCount 0: no entry is created
        model(w, "Caps", box(-4, -4, 0, 4, 4, 4)); // names are not lowercased on load: unreachable from worlds
        model(w, "bigsoup", soup(r, 3600, 40, 40, 5)); // 10800 vertices (the int16 loop limit is 10922)
        w.save(new File(dir, "meshs.geo"));
    }

    static float[] rotation(Random r, int kind) {
        if (kind == 0) {
            return new float[] {1, 0, 0, 0, 1, 0, 0, 0, 1};
        }
        if (kind == 1) { // arbitrary (non-orthonormal) matrix
            float[] m = new float[9];
            for (int i = 0; i < 9; i++) {
                m[i] = (r.nextFloat() - 0.5f) * 3;
            }
            return m;
        }
        // proper rotation from a random unit quaternion
        double x = r.nextGaussian(), y = r.nextGaussian(), z = r.nextGaussian(), w = r.nextGaussian();
        double n = Math.sqrt(x * x + y * y + z * z + w * w);
        x /= n; y /= n; z /= n; w /= n;
        if (kind == 3) { // rotation about the vertical axis only (the common case in real data)
            double a = r.nextDouble() * 2 * Math.PI;
            return new float[] {(float) Math.cos(a), (float) -Math.sin(a), 0, (float) Math.sin(a), (float) Math.cos(a), 0, 0, 0, 1};
        }
        return new float[] {
            (float) (1 - 2 * (y * y + z * z)), (float) (2 * (x * y - z * w)), (float) (2 * (x * z + y * w)),
            (float) (2 * (x * y + z * w)), (float) (1 - 2 * (x * x + z * z)), (float) (2 * (y * z - x * w)),
            (float) (2 * (x * z - y * w)), (float) (2 * (y * z + x * w)), (float) (1 - 2 * (x * x + y * y))};
    }

    static final class World {
        final int id;
        final int worldSize;
        short[] terrain;           // null: flat terrain
        short flatHeight;
        final List<float[]> placements = new ArrayList<float[]>(); // x, y, z for targeted queries
        boolean writeFile = true;
        boolean aligned = false;   // identity-rotation, unit-scale, integer placements + integer-offset queries
        boolean truncate = false;

        World(int id, int worldSize) {
            this.id = id;
            this.worldSize = worldSize;
        }

        float terrainZ(float x, float y) {
            if (terrain == null) {
                return flatHeight / 32f;
            }
            int size = (int) Math.sqrt(terrain.length);
            int xi = Math.max(0, Math.min(size - 1, (int) (x / 2))), yi = Math.max(0, Math.min(size - 1, (int) (y / 2)));
            return terrain[yi + xi * size] / 32f;
        }
    }

    static void writeWorld(File dir, World wd, Random r, int placements, boolean exotic) throws IOException {
        Writer w = new Writer();
        if (wd.terrain == null) {
            w.b.put((byte) 0);
            w.b.putShort(wd.flatHeight);
        } else {
            w.b.put((byte) 1);
            w.b.putInt(wd.terrain.length);
            for (short h : wd.terrain) {
                w.b.putShort(h);
            }
        }
        for (int i = 0; i < placements; i++) {
            String name = PLACEABLE[r.nextInt(PLACEABLE.length)];
            float x = -20 + r.nextFloat() * (wd.worldSize + 40), y = -20 + r.nextFloat() * (wd.worldSize + 40);
            float z = wd.terrainZ(x, y) + (r.nextFloat() - 0.2f) * 20;
            int kind = r.nextInt(10);
            float[] rot = rotation(r, kind < 2 ? 0 : kind < 5 ? 3 : kind < 9 ? 2 : exotic ? 1 : 2);
            float scale = 0.25f + r.nextFloat() * 3.75f;
            if (exotic && i % 50 == 7) {
                scale = 0f; // singular world matrix: Matrix4f.invert throws ArithmeticException on ray queries
            }
            if (exotic && i % 50 == 13) {
                scale = -1.5f; // mirrored
            }
            w.name(name);
            w.b.putFloat(x).putFloat(y).putFloat(z);
            for (float f : rot) {
                w.b.putFloat(f);
            }
            w.b.putFloat(scale);
            wd.placements.add(new float[] {x, y, z});
        }
        if (wd.truncate) {
            w.b.position(w.b.position() - 7); // cut the last placement record short
        }
        if (wd.writeFile) {
            w.save(new File(dir, wd.id + ".geo"));
        }
    }

    static void writeWorldAligned(File dir, World wd, Random r, int placements) throws IOException {
        String[] models = {"box", "walls", "multi", "tower", "plate", "pyramid", "dup"};
        Writer w = new Writer();
        w.b.put((byte) 0);
        w.b.putShort(wd.flatHeight);
        for (int i = 0; i < placements; i++) {
            float x = 16 + r.nextInt(wd.worldSize - 32), y = 16 + r.nextInt(wd.worldSize - 32), z = r.nextInt(8);
            w.name(models[i % models.length]);
            w.b.putFloat(x).putFloat(y).putFloat(z);
            for (float f : rotation(r, 0)) {
                w.b.putFloat(f);
            }
            w.b.putFloat(i % 5 == 0 ? 2f : 1f);
            wd.placements.add(new float[] {x, y, z});
        }
        w.save(new File(dir, wd.id + ".geo"));
    }

    static void writeWorldSparse(File dir, World wd, Random r, int placements) throws IOException {
        String[] small = {"box", "pyramid", "tower", "plate", "multi", "dup"};
        Writer w = new Writer();
        w.b.put((byte) 1);
        w.b.putInt(wd.terrain.length);
        for (short h : wd.terrain) {
            w.b.putShort(h);
        }
        for (int i = 0; i < placements; i++) {
            float x = 10 + r.nextFloat() * (wd.worldSize - 20), y = 10 + r.nextFloat() * (wd.worldSize - 20);
            float z = wd.terrainZ(x, y);
            w.name(small[r.nextInt(small.length)]);
            w.b.putFloat(x).putFloat(y).putFloat(z);
            for (float f : rotation(r, r.nextBoolean() ? 3 : 2)) {
                w.b.putFloat(f);
            }
            w.b.putFloat(0.5f + r.nextFloat() * 1.5f);
            wd.placements.add(new float[] {x, y, z});
        }
        w.save(new File(dir, wd.id + ".geo"));
    }

    static short[] heightmap(Random r, int size, float base, float amp) {
        short[] h = new short[size * size];
        double f1 = 0.02 + r.nextDouble() * 0.03, f2 = 0.05 + r.nextDouble() * 0.05;
        for (int xi = 0; xi < size; xi++) {
            for (int yi = 0; yi < size; yi++) {
                double v = base + amp * (Math.sin(xi * f1) * Math.cos(yi * f2) + 0.3 * Math.sin((xi + yi) * 0.11)) + r.nextDouble() * 0.5;
                h[yi + xi * size] = (short) Math.round(v * 32);
            }
        }
        return h;
    }

    // ------------------------------------------------------------------ test driver

    static int mismatches = 0;
    static long compared = 0;

    static void check(String what, String a, String b) {
        compared++;
        if (!a.equals(b)) {
            mismatches++;
            if (mismatches <= 20) {
                System.out.println("MISMATCH " + what + "\n   ref: " + a + "\n   new: " + b);
            }
        }
    }

    public static void main(String[] args) throws Throwable {
        if (args.length < 3) {
            System.err.println("usage: GeoDiffTest <referenceClasspath> <candidateClasspath> <dataDir> [queriesPerKind] [seed]");
            System.exit(2);
        }
        int queries = args.length > 3 ? Integer.parseInt(args[3]) : 20000;
        long seed = args.length > 4 ? Long.parseLong(args[4]) : 20110425L;
        File dir = new File(args[2]);
        dir.mkdirs();
        String geoDir = dir.getPath() + File.separator;
        Random r = new Random(seed);

        // --- data
        writeMeshs(dir, r);
        List<World> worlds = new ArrayList<World>();
        World w1 = new World(101, 512);             // heightmap terrain (260x260 samples, 2 units apart), 4 cells
        w1.terrain = heightmap(r, 260, 60, 25);
        writeWorld(dir, w1, r, 400, false);
        worlds.add(w1);
        World w2 = new World(102, 256);             // flat terrain, exotic placements (zero/negative scale, skew)
        w2.flatHeight = (short) (50 * 32);
        writeWorld(dir, w2, r, 150, true);
        worlds.add(w2);
        World w3 = new World(103, 256);             // flat terrain, no models: worldBound stays null
        w3.flatHeight = (short) (-7 * 32 - 5);
        writeWorld(dir, w3, r, 0, false);
        worlds.add(w3);
        World w4 = new World(104, 256);             // tiny heightmap: most queries fall outside it
        w4.terrain = heightmap(r, 12, 10, 5);
        writeWorld(dir, w4, r, 30, false);
        worlds.add(w4);
        World w5 = new World(105, 1024);            // non-square sample count, 16 cells
        w5.terrain = new short[1000];
        for (int i = 0; i < w5.terrain.length; i++) {
            w5.terrain[i] = (short) (r.nextInt(4000) - 1000);
        }
        writeWorld(dir, w5, r, 200, true);
        worlds.add(w5);
        World w8 = new World(108, 512);             // gentle terrain, sparse small models: mostly clear lines of sight
        w8.terrain = heightmap(r, 258, 20, 4);
        writeWorldSparse(dir, w8, r, 60);
        worlds.add(w8);
        World w9 = new World(109, 256);             // axis-aligned, integer-positioned models; queries exactly on
        w9.flatHeight = 0;                          // BIH split planes (0 * inf = NaN inside the BIH traversal)
        w9.aligned = true;
        writeWorldAligned(dir, w9, r, 40);
        worlds.add(w9);
        World w6 = new World(106, 256);             // no file at all: loadWorld returns false
        w6.writeFile = false;
        w6.flatHeight = 0;
        writeWorld(dir, w6, r, 5, false);
        worlds.add(w6);
        World w7 = new World(107, 256);             // truncated last record: BufferUnderflowException escapes
        w7.flatHeight = 3200;
        w7.truncate = true;
        writeWorld(dir, w7, r, 20, false);
        worlds.add(w7);

        // --- load both implementations
        Impl ref = new Impl("ref", args[0]);
        Impl cand = new Impl("new", args[1]);
        ref.call(ref.setGeoDir, null, geoDir);
        cand.call(cand.setGeoDir, null, geoDir);
        Map<?, ?> refModels = (Map<?, ?>) ref.call(ref.loadMeshs, null);
        Map<?, ?> newModels = (Map<?, ?>) cand.call(cand.loadMeshs, null);
        check("model names", String.valueOf(new java.util.TreeSet<Object>(refModels.keySet())),
                String.valueOf(new java.util.TreeSet<Object>(newModels.keySet())));
        for (Object key : new java.util.TreeSet<Object>(refModels.keySet())) {
            StringBuilder a = new StringBuilder(), b = new StringBuilder();
            ref.dump(refModels.get(key), "", a);
            cand.dump(newModels.get(key), "", b);
            check("model " + key, a.toString(), b.toString());
        }
        System.out.println("meshs.geo: " + refModels.size() + " models loaded by both implementations");

        long start = System.currentTimeMillis();
        for (World wd : worlds) {
            Object refMap = ref.geoMapCtor.newInstance(Integer.toString(wd.id), wd.worldSize);
            Object newMap = cand.geoMapCtor.newInstance(Integer.toString(wd.id), wd.worldSize);
            String la, lb;
            try {
                la = String.valueOf(ref.call(ref.loadWorld, null, wd.id, refModels, refMap));
            } catch (Throwable t) {
                la = describe(t);
            }
            try {
                lb = String.valueOf(cand.call(cand.loadWorld, null, wd.id, newModels, newMap));
            } catch (Throwable t) {
                lb = describe(t);
            }
            check("loadWorld " + wd.id, la, lb);
            StringBuilder da = new StringBuilder(), db = new StringBuilder();
            ref.dump(refMap, "", da);
            cand.dump(newMap, "", db);
            check("scene " + wd.id, da.toString(), db.toString());
            int cells = ((List<?>) ref.call(ref.getChildren, refMap)).size();

            // queries
            Random q = new Random(seed * 31 + wd.id);
            int zCount = 0, zExc = 0, zHitModel = 0, sCount = 0, sTrue = 0, sExc = 0;
            for (int i = 0; i < queries; i++) {
                float x, y, z;
                int mode = q.nextInt(10);
                if (wd.aligned && mode < 7) {                      // integer / half-integer offsets from a model
                    float[] p = wd.placements.get(q.nextInt(wd.placements.size()));
                    x = p[0] + (q.nextInt(49) - 24) * 0.5f;
                    y = p[1] + (q.nextInt(49) - 24) * 0.5f;
                } else if (mode < 6 || wd.placements.isEmpty()) {  // uniform over (slightly beyond) the map
                    x = -8 + q.nextFloat() * (wd.worldSize + 16);
                    y = -8 + q.nextFloat() * (wd.worldSize + 16);
                } else if (mode < 9) {                              // near a placed model
                    float[] p = wd.placements.get(q.nextInt(wd.placements.size()));
                    x = p[0] + (q.nextFloat() - 0.5f) * 30;
                    y = p[1] + (q.nextFloat() - 0.5f) * 30;
                } else {                                            // exactly on grid lines / model centres
                    if (!wd.placements.isEmpty() && q.nextBoolean()) {
                        float[] p = wd.placements.get(q.nextInt(wd.placements.size()));
                        x = p[0];
                        y = p[1];
                    } else {
                        x = 2 * q.nextInt(wd.worldSize / 2 + 1);
                        y = q.nextInt(wd.worldSize + 1);
                    }
                }
                z = wd.terrainZ(x, y) + (q.nextFloat() - 0.25f) * 60;
                String a, b;
                try {
                    a = "Z " + Integer.toHexString(Float.floatToRawIntBits((Float) ref.call(ref.getZ, refMap, x, y, z)));
                } catch (Throwable t) {
                    a = describe(t);
                }
                try {
                    b = "Z " + Integer.toHexString(Float.floatToRawIntBits((Float) cand.call(cand.getZ, newMap, x, y, z)));
                } catch (Throwable t) {
                    b = describe(t);
                }
                check("world " + wd.id + " getZ(" + x + ", " + y + ", " + z + ")", a, b);
                zCount++;
                countException("getZ", a);
                if (a.startsWith("EXC")) {
                    zExc++;
                } else {
                    float t = (Float) ref.call(ref.terrainZ, refMap, x, y);
                    if (!a.equals("Z " + Integer.toHexString(Float.floatToRawIntBits(t)))) {
                        zHitModel++;
                    }
                }

                // canSee: mostly within the 80-unit limit
                float dist = q.nextInt(10) < 7 ? q.nextFloat() * 80 : 80 + q.nextFloat() * 40;
                double ang = q.nextDouble() * 2 * Math.PI;
                float tx = x + (float) (dist * Math.cos(ang)), ty = y + (float) (dist * Math.sin(ang));
                float tz = wd.terrainZ(tx, ty) + (q.nextFloat() - 0.1f) * 30;
                if (q.nextInt(4) != 0) { // usually both eyes above ground
                    z = wd.terrainZ(x, y) + 1 + q.nextFloat() * 25;
                }
                try {
                    a = "S " + ref.call(ref.canSee, refMap, x, y, z, tx, ty, tz);
                } catch (Throwable t) {
                    a = describe(t);
                }
                try {
                    b = "S " + cand.call(cand.canSee, newMap, x, y, z, tx, ty, tz);
                } catch (Throwable t) {
                    b = describe(t);
                }
                check("world " + wd.id + " canSee(" + x + ", " + y + ", " + z + ", " + tx + ", " + ty + ", " + tz + ")", a, b);
                sCount++;
                countException("canSee", a);
                if (a.startsWith("EXC")) {
                    sExc++;
                } else if (a.equals("S true")) {
                    sTrue++;
                }
            }
            System.out.println("world " + wd.id + ": load=" + la + " cells=" + cells + " placements=" + wd.placements.size()
                    + " | getZ " + zCount + " (" + zHitModel + " answered by a model, " + zExc + " exceptions)"
                    + " | canSee " + sCount + " (" + sTrue + " true, " + sExc + " exceptions) " + EXCEPTIONS);
            EXCEPTIONS.clear();
        }
        if (!WITH_MESSAGES) {
            System.out.println("note: exception messages not compared (run with -XX:-OmitStackTraceInFastThrow)");
        }
        System.out.println("compared " + compared + " results in " + (System.currentTimeMillis() - start) + " ms: "
                + (mismatches == 0 ? "ALL IDENTICAL" : mismatches + " MISMATCHES"));
        System.exit(mismatches == 0 ? 0 : 1);
    }
}
