/*
 * geoEngine 0.1 (package aionjHungary.geoEngine) - a cut-down copy of the jMonkeyEngine3 math,
 * scene-graph and collision code (BSD licence, jmonkeyengine.org) repackaged by "aionjHungary".
 * Source recovered from gameserver/lib/geoEngine-0.1.jar with CFR 0.152 and repaired so that it
 * recompiles to the jar's API and (modulo javac-version differences) bytecode; see ../../README.md.
 */
package aionjHungary.geoEngine.utils;

import aionjHungary.geoEngine.math.Quaternion;
import aionjHungary.geoEngine.math.Vector2f;
import aionjHungary.geoEngine.math.Vector3f;
import aionjHungary.geoEngine.utils.TempVars;
import java.nio.Buffer;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.DoubleBuffer;
import java.nio.FloatBuffer;
import java.nio.IntBuffer;
import java.nio.ShortBuffer;
import java.util.ArrayList;
import java.util.Collections;
import java.util.Map;
import java.util.WeakHashMap;

public final class BufferUtils {
    private static final Map<Buffer, Object> trackingHash = Collections.synchronizedMap(new WeakHashMap());
    private static final Object ref = new Object();
    private static final boolean trackDirectMemory = false;

    public static Buffer clone(Buffer buf) {
        if (buf instanceof FloatBuffer) {
            return BufferUtils.clone((FloatBuffer)buf);
        }
        if (buf instanceof ShortBuffer) {
            return BufferUtils.clone((ShortBuffer)buf);
        }
        if (buf instanceof ByteBuffer) {
            return BufferUtils.clone((ByteBuffer)buf);
        }
        if (buf instanceof IntBuffer) {
            return BufferUtils.clone((IntBuffer)buf);
        }
        if (buf instanceof DoubleBuffer) {
            return BufferUtils.clone((DoubleBuffer)buf);
        }
        throw new UnsupportedOperationException();
    }

    public static FloatBuffer createFloatBuffer(Vector3f ... data) {
        if (data == null) {
            return null;
        }
        FloatBuffer buff = BufferUtils.createFloatBuffer(3 * data.length);
        for (int x = 0; x < data.length; ++x) {
            if (data[x] != null) {
                buff.put(data[x].x).put(data[x].y).put(data[x].z);
                continue;
            }
            buff.put(0.0f).put(0.0f).put(0.0f);
        }
        buff.flip();
        return buff;
    }

    public static FloatBuffer createFloatBuffer(Quaternion ... data) {
        if (data == null) {
            return null;
        }
        FloatBuffer buff = BufferUtils.createFloatBuffer(4 * data.length);
        for (int x = 0; x < data.length; ++x) {
            if (data[x] != null) {
                buff.put(data[x].getX()).put(data[x].getY()).put(data[x].getZ()).put(data[x].getW());
                continue;
            }
            buff.put(0.0f).put(0.0f).put(0.0f);
        }
        buff.flip();
        return buff;
    }

    public static FloatBuffer createFloatBuffer(float ... data) {
        if (data == null) {
            return null;
        }
        FloatBuffer buff = BufferUtils.createFloatBuffer(data.length);
        buff.clear();
        buff.put(data);
        buff.flip();
        return buff;
    }

    public static FloatBuffer createVector3Buffer(int vertices) {
        FloatBuffer vBuff = BufferUtils.createFloatBuffer(3 * vertices);
        return vBuff;
    }

    public static FloatBuffer createVector3Buffer(FloatBuffer buf, int vertices) {
        if (buf != null && buf.limit() == 3 * vertices) {
            buf.rewind();
            return buf;
        }
        return BufferUtils.createFloatBuffer(3 * vertices);
    }

    public static void setInBuffer(Quaternion quat, FloatBuffer buf, int index) {
        buf.position(index * 4);
        buf.put(quat.getX());
        buf.put(quat.getY());
        buf.put(quat.getZ());
        buf.put(quat.getW());
    }

    public static void setInBuffer(Vector3f vector, FloatBuffer buf, int index) {
        if (buf == null) {
            return;
        }
        if (vector == null) {
            buf.put(index * 3, 0.0f);
            buf.put(index * 3 + 1, 0.0f);
            buf.put(index * 3 + 2, 0.0f);
        } else {
            buf.put(index * 3, vector.x);
            buf.put(index * 3 + 1, vector.y);
            buf.put(index * 3 + 2, vector.z);
        }
    }

    public static void populateFromBuffer(Vector3f vector, FloatBuffer buf, int index) {
        vector.x = buf.get(index * 3);
        vector.y = buf.get(index * 3 + 1);
        vector.z = buf.get(index * 3 + 2);
    }

    public static Vector3f[] getVector3Array(FloatBuffer buff) {
        buff.clear();
        Vector3f[] verts = new Vector3f[buff.limit() / 3];
        for (int x = 0; x < verts.length; ++x) {
            Vector3f v = new Vector3f(buff.get(), buff.get(), buff.get());
            verts[x] = v;
        }
        return verts;
    }

    public static void copyInternalVector3(FloatBuffer buf, int fromPos, int toPos) {
        BufferUtils.copyInternal(buf, fromPos * 3, toPos * 3, 3);
    }

    public static void normalizeVector3(FloatBuffer buf, int index) {
        assert (TempVars.get().lock());
        Vector3f tempVec3 = TempVars.get().vect1;
        BufferUtils.populateFromBuffer(tempVec3, buf, index);
        tempVec3.normalizeLocal();
        BufferUtils.setInBuffer(tempVec3, buf, index);
        assert (TempVars.get().unlock());
    }

    public static void addInBuffer(Vector3f toAdd, FloatBuffer buf, int index) {
        assert (TempVars.get().lock());
        Vector3f tempVec3 = TempVars.get().vect1;
        BufferUtils.populateFromBuffer(tempVec3, buf, index);
        tempVec3.addLocal(toAdd);
        BufferUtils.setInBuffer(tempVec3, buf, index);
        assert (TempVars.get().unlock());
    }

    public static void multInBuffer(Vector3f toMult, FloatBuffer buf, int index) {
        assert (TempVars.get().lock());
        Vector3f tempVec3 = TempVars.get().vect1;
        BufferUtils.populateFromBuffer(tempVec3, buf, index);
        tempVec3.multLocal(toMult);
        BufferUtils.setInBuffer(tempVec3, buf, index);
        assert (TempVars.get().unlock());
    }

    public static boolean equals(Vector3f check, FloatBuffer buf, int index) {
        assert (TempVars.get().lock());
        Vector3f tempVec3 = TempVars.get().vect1;
        BufferUtils.populateFromBuffer(tempVec3, buf, index);
        boolean eq = tempVec3.equals(check);
        assert (TempVars.get().unlock());
        return eq;
    }

    public static FloatBuffer createFloatBuffer(Vector2f ... data) {
        if (data == null) {
            return null;
        }
        FloatBuffer buff = BufferUtils.createFloatBuffer(2 * data.length);
        for (int x = 0; x < data.length; ++x) {
            if (data[x] != null) {
                buff.put(data[x].x).put(data[x].y);
                continue;
            }
            buff.put(0.0f).put(0.0f);
        }
        buff.flip();
        return buff;
    }

    public static FloatBuffer createVector2Buffer(int vertices) {
        FloatBuffer vBuff = BufferUtils.createFloatBuffer(2 * vertices);
        return vBuff;
    }

    public static FloatBuffer createVector2Buffer(FloatBuffer buf, int vertices) {
        if (buf != null && buf.limit() == 2 * vertices) {
            buf.rewind();
            return buf;
        }
        return BufferUtils.createFloatBuffer(2 * vertices);
    }

    public static void setInBuffer(Vector2f vector, FloatBuffer buf, int index) {
        buf.put(index * 2, vector.x);
        buf.put(index * 2 + 1, vector.y);
    }

    public static void populateFromBuffer(Vector2f vector, FloatBuffer buf, int index) {
        vector.x = buf.get(index * 2);
        vector.y = buf.get(index * 2 + 1);
    }

    public static Vector2f[] getVector2Array(FloatBuffer buff) {
        buff.clear();
        Vector2f[] verts = new Vector2f[buff.limit() / 2];
        for (int x = 0; x < verts.length; ++x) {
            Vector2f v = new Vector2f(buff.get(), buff.get());
            verts[x] = v;
        }
        return verts;
    }

    public static void copyInternalVector2(FloatBuffer buf, int fromPos, int toPos) {
        BufferUtils.copyInternal(buf, fromPos * 2, toPos * 2, 2);
    }

    public static void normalizeVector2(FloatBuffer buf, int index) {
        assert (TempVars.get().lock());
        Vector2f tempVec2 = TempVars.get().vect2d;
        BufferUtils.populateFromBuffer(tempVec2, buf, index);
        tempVec2.normalizeLocal();
        BufferUtils.setInBuffer(tempVec2, buf, index);
        assert (TempVars.get().unlock());
    }

    public static void addInBuffer(Vector2f toAdd, FloatBuffer buf, int index) {
        assert (TempVars.get().lock());
        Vector2f tempVec2 = TempVars.get().vect2d;
        BufferUtils.populateFromBuffer(tempVec2, buf, index);
        tempVec2.addLocal(toAdd);
        BufferUtils.setInBuffer(tempVec2, buf, index);
        assert (TempVars.get().unlock());
    }

    public static void multInBuffer(Vector2f toMult, FloatBuffer buf, int index) {
        assert (TempVars.get().lock());
        Vector2f tempVec2 = TempVars.get().vect2d;
        BufferUtils.populateFromBuffer(tempVec2, buf, index);
        tempVec2.multLocal(toMult);
        BufferUtils.setInBuffer(tempVec2, buf, index);
        assert (TempVars.get().unlock());
    }

    public static boolean equals(Vector2f check, FloatBuffer buf, int index) {
        assert (TempVars.get().lock());
        Vector2f tempVec2 = TempVars.get().vect2d;
        BufferUtils.populateFromBuffer(tempVec2, buf, index);
        boolean eq = tempVec2.equals(check);
        assert (TempVars.get().unlock());
        return eq;
    }

    public static IntBuffer createIntBuffer(int ... data) {
        if (data == null) {
            return null;
        }
        IntBuffer buff = BufferUtils.createIntBuffer(data.length);
        buff.clear();
        buff.put(data);
        buff.flip();
        return buff;
    }

    public static int[] getIntArray(IntBuffer buff) {
        if (buff == null) {
            return null;
        }
        buff.clear();
        int[] inds = new int[buff.limit()];
        for (int x = 0; x < inds.length; ++x) {
            inds[x] = buff.get();
        }
        return inds;
    }

    public static float[] getFloatArray(FloatBuffer buff) {
        if (buff == null) {
            return null;
        }
        buff.clear();
        float[] inds = new float[buff.limit()];
        for (int x = 0; x < inds.length; ++x) {
            inds[x] = buff.get();
        }
        return inds;
    }

    public static DoubleBuffer createDoubleBuffer(int size) {
        DoubleBuffer buf = ByteBuffer.allocateDirect(8 * size).order(ByteOrder.nativeOrder()).asDoubleBuffer();
        buf.clear();
        return buf;
    }

    public static DoubleBuffer createDoubleBuffer(DoubleBuffer buf, int size) {
        if (buf != null && buf.limit() == size) {
            buf.rewind();
            return buf;
        }
        buf = BufferUtils.createDoubleBuffer(size);
        return buf;
    }

    public static DoubleBuffer clone(DoubleBuffer buf) {
        if (buf == null) {
            return null;
        }
        buf.rewind();
        DoubleBuffer copy;
        if (buf.isDirect()) {
            copy = BufferUtils.createDoubleBuffer(buf.limit());
        } else {
            copy = DoubleBuffer.allocate(buf.limit());
        }
        copy.put(buf);
        return copy;
    }

    public static FloatBuffer createFloatBuffer(int size) {
        FloatBuffer buf = ByteBuffer.allocateDirect(4 * size).order(ByteOrder.nativeOrder()).asFloatBuffer();
        buf.clear();
        return buf;
    }

    public static void copyInternal(FloatBuffer buf, int fromPos, int toPos, int length) {
        float[] data = new float[length];
        buf.position(fromPos);
        buf.get(data);
        buf.position(toPos);
        buf.put(data);
    }

    public static FloatBuffer clone(FloatBuffer buf) {
        if (buf == null) {
            return null;
        }
        buf.rewind();
        FloatBuffer copy;
        if (buf.isDirect()) {
            copy = BufferUtils.createFloatBuffer(buf.limit());
        } else {
            copy = FloatBuffer.allocate(buf.limit());
        }
        copy.put(buf);
        return copy;
    }

    public static IntBuffer createIntBuffer(int size) {
        IntBuffer buf = ByteBuffer.allocateDirect(4 * size).order(ByteOrder.nativeOrder()).asIntBuffer();
        buf.clear();
        return buf;
    }

    public static IntBuffer createIntBuffer(IntBuffer buf, int size) {
        if (buf != null && buf.limit() == size) {
            buf.rewind();
            return buf;
        }
        buf = BufferUtils.createIntBuffer(size);
        return buf;
    }

    public static IntBuffer clone(IntBuffer buf) {
        if (buf == null) {
            return null;
        }
        buf.rewind();
        IntBuffer copy;
        if (buf.isDirect()) {
            copy = BufferUtils.createIntBuffer(buf.limit());
        } else {
            copy = IntBuffer.allocate(buf.limit());
        }
        copy.put(buf);
        return copy;
    }

    public static ByteBuffer createByteBuffer(int size) {
        ByteBuffer buf = ByteBuffer.allocateDirect(size).order(ByteOrder.nativeOrder());
        buf.clear();
        return buf;
    }

    public static ByteBuffer createByteBuffer(ByteBuffer buf, int size) {
        if (buf != null && buf.limit() == size) {
            buf.rewind();
            return buf;
        }
        buf = BufferUtils.createByteBuffer(size);
        return buf;
    }

    public static ByteBuffer createByteBuffer(byte ... data) {
        ByteBuffer bb = BufferUtils.createByteBuffer(data.length);
        bb.put(data);
        bb.flip();
        return bb;
    }

    public static ByteBuffer createByteBuffer(String data) {
        byte[] bytes = data.getBytes();
        ByteBuffer bb = BufferUtils.createByteBuffer(bytes.length);
        bb.put(bytes);
        bb.flip();
        return bb;
    }

    public static ByteBuffer clone(ByteBuffer buf) {
        if (buf == null) {
            return null;
        }
        buf.rewind();
        ByteBuffer copy;
        if (buf.isDirect()) {
            copy = BufferUtils.createByteBuffer(buf.limit());
        } else {
            copy = ByteBuffer.allocate(buf.limit());
        }
        copy.put(buf);
        return copy;
    }

    public static ShortBuffer createShortBuffer(int size) {
        ShortBuffer buf = ByteBuffer.allocateDirect(2 * size).order(ByteOrder.nativeOrder()).asShortBuffer();
        buf.clear();
        return buf;
    }

    public static ShortBuffer createShortBuffer(ShortBuffer buf, int size) {
        if (buf != null && buf.limit() == size) {
            buf.rewind();
            return buf;
        }
        buf = BufferUtils.createShortBuffer(size);
        return buf;
    }

    public static ShortBuffer createShortBuffer(short ... data) {
        if (data == null) {
            return null;
        }
        ShortBuffer buff = BufferUtils.createShortBuffer(data.length);
        buff.clear();
        buff.put(data);
        buff.flip();
        return buff;
    }

    public static ShortBuffer clone(ShortBuffer buf) {
        if (buf == null) {
            return null;
        }
        buf.rewind();
        ShortBuffer copy;
        if (buf.isDirect()) {
            copy = BufferUtils.createShortBuffer(buf.limit());
        } else {
            copy = ShortBuffer.allocate(buf.limit());
        }
        copy.put(buf);
        return copy;
    }

    public static FloatBuffer ensureLargeEnough(FloatBuffer buffer, int required) {
        if (buffer == null || buffer.remaining() < required) {
            int position = buffer != null ? buffer.position() : 0;
            FloatBuffer newVerts = BufferUtils.createFloatBuffer(position + required);
            if (buffer != null) {
                buffer.rewind();
                newVerts.put(buffer);
                newVerts.position(position);
            }
            buffer = newVerts;
        }
        return buffer;
    }

    public static ShortBuffer ensureLargeEnough(ShortBuffer buffer, int required) {
        if (buffer == null || buffer.remaining() < required) {
            int position = buffer != null ? buffer.position() : 0;
            ShortBuffer newVerts = BufferUtils.createShortBuffer(position + required);
            if (buffer != null) {
                buffer.rewind();
                newVerts.put(buffer);
                newVerts.position(position);
            }
            buffer = newVerts;
        }
        return buffer;
    }

    public static ByteBuffer ensureLargeEnough(ByteBuffer buffer, int required) {
        if (buffer == null || buffer.remaining() < required) {
            int position = buffer != null ? buffer.position() : 0;
            ByteBuffer newVerts = BufferUtils.createByteBuffer(position + required);
            if (buffer != null) {
                buffer.rewind();
                newVerts.put(buffer);
                newVerts.position(position);
            }
            buffer = newVerts;
        }
        return buffer;
    }

    public static void printCurrentDirectMemory(StringBuilder store) {
        long totalHeld = 0L;
        ArrayList<Buffer> bufs = new ArrayList<Buffer>(trackingHash.keySet());
        int fBufs = 0;
        int bBufs = 0;
        int iBufs = 0;
        int sBufs = 0;
        int dBufs = 0;
        int fBufsM = 0;
        int bBufsM = 0;
        int iBufsM = 0;
        int sBufsM = 0;
        int dBufsM = 0;
        for (Buffer b : bufs) {
            if (b instanceof ByteBuffer) {
                totalHeld += (long)b.capacity();
                bBufsM += b.capacity();
                ++bBufs;
                continue;
            }
            if (b instanceof FloatBuffer) {
                totalHeld += (long)(b.capacity() * 4);
                fBufsM += b.capacity() * 4;
                ++fBufs;
                continue;
            }
            if (b instanceof IntBuffer) {
                totalHeld += (long)(b.capacity() * 4);
                iBufsM += b.capacity() * 4;
                ++iBufs;
                continue;
            }
            if (b instanceof ShortBuffer) {
                totalHeld += (long)(b.capacity() * 2);
                sBufsM += b.capacity() * 2;
                ++sBufs;
                continue;
            }
            if (!(b instanceof DoubleBuffer)) continue;
            totalHeld += (long)(b.capacity() * 8);
            dBufsM += b.capacity() * 8;
            ++dBufs;
        }
        long heapMem = Runtime.getRuntime().totalMemory() - Runtime.getRuntime().freeMemory();
        boolean printStout = store == null;
        if (store == null) {
            store = new StringBuilder();
        }
        store.append("Existing buffers: ").append(bufs.size()).append("\n");
        store.append("(b: ").append(bBufs).append("  f: ").append(fBufs).append("  i: ").append(iBufs).append("  s: ").append(sBufs).append("  d: ").append(dBufs).append(")").append("\n");
        store.append("Total   heap memory held: ").append(heapMem / 1024L).append("kb\n");
        store.append("Total direct memory held: ").append(totalHeld / 1024L).append("kb\n");
        store.append("(b: ").append(bBufsM / 1024).append("kb  f: ").append(fBufsM / 1024).append("kb  i: ").append(iBufsM / 1024).append("kb  s: ").append(sBufsM / 1024).append("kb  d: ").append(dBufsM / 1024).append("kb)").append("\n");
        if (printStout) {
            System.out.println(store.toString());
        }
    }
}

