/*
 * geoEngine 0.1 (package aionjHungary.geoEngine) - a cut-down copy of the jMonkeyEngine3 math,
 * scene-graph and collision code (BSD licence, jmonkeyengine.org) repackaged by "aionjHungary".
 * Source recovered from gameserver/lib/geoEngine-0.1.jar with CFR 0.152 and repaired so that it
 * recompiles to the jar's API and (modulo javac-version differences) bytecode; see ../../README.md.
 */
package aionjHungary.geoEngine.scene;

import aionjHungary.geoEngine.bounding.BoundingBox;
import aionjHungary.geoEngine.bounding.BoundingVolume;
import aionjHungary.geoEngine.collision.Collidable;
import aionjHungary.geoEngine.collision.CollisionResults;
import aionjHungary.geoEngine.collision.bih.BIHTree;
import aionjHungary.geoEngine.math.Matrix4f;
import aionjHungary.geoEngine.math.Triangle;
import aionjHungary.geoEngine.math.Vector2f;
import aionjHungary.geoEngine.math.Vector3f;
import aionjHungary.geoEngine.scene.CollisionData;
import aionjHungary.geoEngine.scene.VertexBuffer;
import aionjHungary.geoEngine.scene.mesh.IndexBuffer;
import aionjHungary.geoEngine.scene.mesh.IndexByteBuffer;
import aionjHungary.geoEngine.scene.mesh.IndexIntBuffer;
import aionjHungary.geoEngine.scene.mesh.IndexShortBuffer;
import aionjHungary.geoEngine.utils.BufferUtils;
import aionjHungary.geoEngine.utils.IntMap;
import java.nio.Buffer;
import java.nio.ByteBuffer;
import java.nio.FloatBuffer;
import java.nio.IntBuffer;
import java.nio.ShortBuffer;
import java.util.ArrayList;

public class Mesh {
    private BoundingVolume meshBound = new BoundingBox();
    private CollisionData collisionTree = null;
    private IntMap<VertexBuffer> buffers = new IntMap();
    private float pointSize = 1.0f;
    private float lineWidth = 1.0f;
    private transient int vertexArrayID = -1;
    private int vertCount = -1;
    private int elementCount = -1;
    private int maxNumWeights = -1;
    private int[] modeStart;
    private Mode mode = Mode.Triangles;

    public int[] getModeStart() {
        return this.modeStart;
    }

    public void setModeStart(int[] modeStart) {
        this.modeStart = modeStart;
    }

    public Mode getMode() {
        return this.mode;
    }

    public void setMode(Mode mode) {
        this.mode = mode;
        this.updateCounts();
    }

    public int getMaxNumWeights() {
        return this.maxNumWeights;
    }

    public void setMaxNumWeights(int maxNumWeights) {
        this.maxNumWeights = maxNumWeights;
    }

    public float getPointSize() {
        return this.pointSize;
    }

    public void setPointSize(float pointSize) {
        this.pointSize = pointSize;
    }

    public float getLineWidth() {
        return this.lineWidth;
    }

    public void setLineWidth(float lineWidth) {
        this.lineWidth = lineWidth;
    }

    public void setStatic() {
        for (IntMap.Entry entry : this.buffers) {
            ((VertexBuffer)entry.getValue()).setUsage(VertexBuffer.Usage.Static);
        }
    }

    public void setStreamed() {
        for (IntMap.Entry entry : this.buffers) {
            ((VertexBuffer)entry.getValue()).setUsage(VertexBuffer.Usage.Stream);
        }
    }

    public void setInterleaved() {
        ArrayList<VertexBuffer> vbs = new ArrayList<VertexBuffer>();
        for (IntMap.Entry<VertexBuffer> entry : this.buffers) {
            vbs.add(entry.getValue());
        }
        vbs.remove(this.getBuffer(VertexBuffer.Type.Index));
        int stride = 0;
        for (int i = 0; i < vbs.size(); ++i) {
            VertexBuffer vb = vbs.get(i);
            stride += vb.componentsLength;
            vb.getData().clear();
        }
        VertexBuffer allData = new VertexBuffer(VertexBuffer.Type.InterleavedData);
        ByteBuffer dataBuf = BufferUtils.createByteBuffer(stride * this.getVertexCount());
        allData.setupData(VertexBuffer.Usage.Static, -1, VertexBuffer.Format.UnsignedByte, dataBuf);
        this.setBuffer(allData);
        for (int vert = 0; vert < this.getVertexCount(); ++vert) {
            for (int i = 0; i < vbs.size(); ++i) {
                VertexBuffer vb = vbs.get(i);
                switch (vb.getFormat()) {
                    case Float:
                        FloatBuffer fb = (FloatBuffer)vb.getData();
                        for (int comp = 0; comp < vb.components; ++comp) {
                            dataBuf.putFloat(fb.get());
                        }
                        break;
                    case Byte:
                    case UnsignedByte:
                        ByteBuffer bb = (ByteBuffer)vb.getData();
                        for (int comp = 0; comp < vb.components; ++comp) {
                            dataBuf.put(bb.get());
                        }
                        break;
                    case Half:
                    case Short:
                    case UnsignedShort:
                        ShortBuffer sb = (ShortBuffer)vb.getData();
                        for (int comp = 0; comp < vb.components; ++comp) {
                            dataBuf.putShort(sb.get());
                        }
                        break;
                    case Int:
                    case UnsignedInt:
                        IntBuffer ib = (IntBuffer)vb.getData();
                        for (int comp = 0; comp < vb.components; ++comp) {
                            dataBuf.putInt(ib.get());
                        }
                        break;
                }
            }
        }
        int offset = 0;
        for (VertexBuffer vb : vbs) {
            vb.setOffset(offset);
            vb.setStride(stride);
            vb.setupData(vb.usage, vb.components, vb.format, null);
            offset += vb.componentsLength;
        }
    }

    private int computeNumElements(int bufSize) {
        switch (this.mode) {
            case Triangles: {
                return bufSize / 3;
            }
            case TriangleFan: 
            case TriangleStrip: {
                return bufSize - 2;
            }
            case Points: {
                return bufSize;
            }
            case Lines: {
                return bufSize / 2;
            }
            case LineLoop: {
                return bufSize;
            }
            case LineStrip: {
                return bufSize - 1;
            }
        }
        throw new UnsupportedOperationException();
    }

    public void updateCounts() {
        if (this.getBuffer(VertexBuffer.Type.InterleavedData) != null) {
            throw new IllegalStateException("Should update counts before interleave");
        }
        VertexBuffer pb = this.getBuffer(VertexBuffer.Type.Position);
        VertexBuffer ib = this.getBuffer(VertexBuffer.Type.Index);
        if (pb != null) {
            this.vertCount = pb.getData().capacity() / pb.getNumComponents();
        }
        if (ib != null) {
            this.elementCount = this.computeNumElements(ib.getData().capacity());
        } else {
            this.elementCount = this.computeNumElements(this.vertCount);
        }
    }

    public int getTriangleCount(int lod) {
        return this.elementCount;
    }

    public int getTriangleCount() {
        return this.elementCount;
    }

    public int getVertexCount() {
        return this.vertCount;
    }

    public void setTriangleCount(int count) {
        this.elementCount = count;
    }

    public void setVertexCount(int count) {
        this.vertCount = count;
    }

    public void getTriangle(int index, Vector3f v1, Vector3f v2, Vector3f v3) {
        VertexBuffer pb = this.getBuffer(VertexBuffer.Type.Position);
        VertexBuffer ib = this.getBuffer(VertexBuffer.Type.Index);
        if (pb.getFormat() == VertexBuffer.Format.Float) {
            FloatBuffer fpb = (FloatBuffer)pb.getData();
            if (ib.getFormat() == VertexBuffer.Format.UnsignedShort) {
                ShortBuffer sib = (ShortBuffer)ib.getData();
                int vertIndex = index * 3;
                int vert1 = sib.get(vertIndex);
                int vert2 = sib.get(vertIndex + 1);
                int vert3 = sib.get(vertIndex + 2);
                BufferUtils.populateFromBuffer(v1, fpb, vert1);
                BufferUtils.populateFromBuffer(v2, fpb, vert2);
                BufferUtils.populateFromBuffer(v3, fpb, vert3);
            }
        }
    }

    public void getTriangle(int index, Triangle tri) {
        this.getTriangle(index, tri.get1(), tri.get2(), tri.get3());
        tri.setIndex(index);
    }

    public void getTriangle(int index, int[] indices) {
        VertexBuffer ib = this.getBuffer(VertexBuffer.Type.Index);
        if (ib.getFormat() == VertexBuffer.Format.UnsignedShort) {
            ShortBuffer sib = (ShortBuffer)ib.getData();
            int vertIndex = index * 3;
            indices[0] = sib.get(vertIndex);
            indices[1] = sib.get(vertIndex + 1);
            indices[2] = sib.get(vertIndex + 2);
        }
    }

    public int getId() {
        return this.vertexArrayID;
    }

    public void setId(int id) {
        if (this.vertexArrayID != -1) {
            throw new IllegalStateException("ID has already been set.");
        }
        this.vertexArrayID = id;
    }

    public void createCollisionData() {
        if (this.collisionTree != null) {
            return;
        }
        BIHTree tree = new BIHTree(this);
        tree.construct();
        this.collisionTree = tree;
    }

    public int collideWith(Collidable other, Matrix4f worldMatrix, BoundingVolume worldBound, CollisionResults results) {
        if (this.collisionTree == null) {
            this.createCollisionData();
        }
        return this.collisionTree.collideWith(other, worldMatrix, worldBound, results);
    }

    public void setBuffer(VertexBuffer.Type type, int components, FloatBuffer buf) {
        VertexBuffer vb = this.buffers.get(type.ordinal());
        if (vb == null) {
            if (buf == null) {
                return;
            }
            vb = new VertexBuffer(type);
            vb.setupData(VertexBuffer.Usage.Dynamic, components, VertexBuffer.Format.Float, buf);
            this.buffers.put(type.ordinal(), vb);
        } else {
            vb.setupData(VertexBuffer.Usage.Dynamic, components, VertexBuffer.Format.Float, buf);
        }
        this.updateCounts();
    }

    public void setBuffer(VertexBuffer.Type type, int components, float[] buf) {
        this.setBuffer(type, components, BufferUtils.createFloatBuffer(buf));
    }

    public void setBuffer(VertexBuffer.Type type, int components, IntBuffer buf) {
        VertexBuffer vb = this.buffers.get(type.ordinal());
        if (vb == null) {
            vb = new VertexBuffer(type);
            vb.setupData(VertexBuffer.Usage.Dynamic, components, VertexBuffer.Format.UnsignedInt, buf);
            this.buffers.put(type.ordinal(), vb);
            this.updateCounts();
        }
    }

    public void setBuffer(VertexBuffer.Type type, int components, int[] buf) {
        this.setBuffer(type, components, BufferUtils.createIntBuffer(buf));
    }

    public void setBuffer(VertexBuffer.Type type, int components, ShortBuffer buf) {
        VertexBuffer vb = this.buffers.get(type.ordinal());
        if (vb == null) {
            vb = new VertexBuffer(type);
            vb.setupData(VertexBuffer.Usage.Dynamic, components, VertexBuffer.Format.UnsignedShort, buf);
            this.buffers.put(type.ordinal(), vb);
            this.updateCounts();
        }
    }

    public void setBuffer(VertexBuffer.Type type, int components, byte[] buf) {
        this.setBuffer(type, components, BufferUtils.createByteBuffer(buf));
    }

    public void setBuffer(VertexBuffer.Type type, int components, ByteBuffer buf) {
        VertexBuffer vb = this.buffers.get(type.ordinal());
        if (vb == null) {
            vb = new VertexBuffer(type);
            vb.setupData(VertexBuffer.Usage.Dynamic, components, VertexBuffer.Format.UnsignedByte, buf);
            this.buffers.put(type.ordinal(), vb);
            this.updateCounts();
        }
    }

    public void setBuffer(VertexBuffer vb) {
        if (this.buffers.containsKey(vb.getBufferType().ordinal())) {
            throw new IllegalArgumentException("Buffer type already set: " + vb.getBufferType());
        }
        this.buffers.put(vb.getBufferType().ordinal(), vb);
    }

    public void clearBuffer(VertexBuffer.Type type) {
        this.buffers.remove(type.ordinal());
    }

    public void setBuffer(VertexBuffer.Type type, int components, short[] buf) {
        this.setBuffer(type, components, BufferUtils.createShortBuffer(buf));
    }

    public VertexBuffer getBuffer(VertexBuffer.Type type) {
        return this.buffers.get(type.ordinal());
    }

    public FloatBuffer getFloatBuffer(VertexBuffer.Type type) {
        VertexBuffer vb = this.getBuffer(type);
        if (vb == null) {
            return null;
        }
        return (FloatBuffer)vb.getData();
    }

    public ShortBuffer getShortBuffer(VertexBuffer.Type type) {
        VertexBuffer vb = this.getBuffer(type);
        if (vb == null) {
            return null;
        }
        return (ShortBuffer)vb.getData();
    }

    public IndexBuffer getIndexBuffer() {
        VertexBuffer vb = this.getBuffer(VertexBuffer.Type.Index);
        if (vb == null) {
            return null;
        }
        Buffer buf = vb.getData();
        if (buf instanceof ByteBuffer) {
            return new IndexByteBuffer((ByteBuffer)buf);
        }
        if (buf instanceof ShortBuffer) {
            return new IndexShortBuffer((ShortBuffer)buf);
        }
        if (buf instanceof IntBuffer) {
            return new IndexIntBuffer((IntBuffer)buf);
        }
        throw new UnsupportedOperationException("Index buffer type unsupported: " + buf.getClass());
    }

    public void scaleTextureCoordinates(Vector2f scaleFactor) {
        VertexBuffer tc = this.getBuffer(VertexBuffer.Type.TexCoord);
        if (tc == null) {
            throw new IllegalStateException("The mesh has no texture coordinates");
        }
        if (tc.getFormat() != VertexBuffer.Format.Float) {
            throw new UnsupportedOperationException("Only float texture coord format is supported");
        }
        if (tc.getNumComponents() != 2) {
            throw new UnsupportedOperationException("Only 2D texture coords are supported");
        }
        FloatBuffer fb = (FloatBuffer)tc.getData();
        fb.clear();
        for (int i = 0; i < fb.capacity() / 2; ++i) {
            float x = fb.get();
            float y = fb.get();
            fb.position(fb.position() - 2);
            x *= scaleFactor.getX();
            y *= scaleFactor.getY();
            fb.put(x).put(y);
        }
        fb.clear();
    }

    public void updateBound() {
        VertexBuffer posBuf = this.getBuffer(VertexBuffer.Type.Position);
        this.meshBound = new BoundingBox();
        if (posBuf != null) {
            this.meshBound.computeFromPoints((FloatBuffer)posBuf.getData());
        }
    }

    public BoundingVolume getBound() {
        return this.meshBound;
    }

    public void setBound(BoundingVolume modelBound) {
        this.meshBound = modelBound;
    }

    public IntMap<VertexBuffer> getBuffers() {
        return this.buffers;
    }

    public static enum Mode {
        Points,
        Lines,
        LineLoop,
        LineStrip,
        Triangles,
        TriangleStrip,
        TriangleFan,
        Hybrid;

    }
}

