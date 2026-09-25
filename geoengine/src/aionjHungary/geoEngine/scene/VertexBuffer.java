/*
 * geoEngine 0.1 (package aionjHungary.geoEngine) - a cut-down copy of the jMonkeyEngine3 math,
 * scene-graph and collision code (BSD licence, jmonkeyengine.org) repackaged by "aionjHungary".
 * Source recovered from gameserver/lib/geoEngine-0.1.jar with CFR 0.152 and repaired so that it
 * recompiles to the jar's API and (modulo javac-version differences) bytecode; see ../../README.md.
 */
package aionjHungary.geoEngine.scene;

import aionjHungary.geoEngine.math.FastMath;
import aionjHungary.geoEngine.scene.GLObject;
import aionjHungary.geoEngine.utils.BufferUtils;
import java.nio.Buffer;
import java.nio.ByteBuffer;
import java.nio.FloatBuffer;
import java.nio.IntBuffer;
import java.nio.ShortBuffer;

public class VertexBuffer
extends GLObject
implements Cloneable {
    protected int offset = 0;
    protected int stride = 0;
    protected int components = 0;
    protected transient int componentsLength = 0;
    protected Buffer data = null;
    protected transient ByteBuffer mappedData;
    protected Usage usage;
    protected Type bufType;
    protected Format format;
    protected boolean normalized = false;
    protected transient boolean dataSizeChanged = false;

    public VertexBuffer(Type type) {
        super(GLObject.Type.VertexBuffer);
        this.bufType = type;
    }

    public VertexBuffer() {
        super(GLObject.Type.VertexBuffer);
    }

    protected VertexBuffer(int id) {
        super(GLObject.Type.VertexBuffer, id);
    }

    public int getOffset() {
        return this.offset;
    }

    public void setOffset(int offset) {
        this.offset = offset;
    }

    public int getStride() {
        return this.stride;
    }

    public void setStride(int stride) {
        this.stride = stride;
    }

    public Buffer getData() {
        return this.data;
    }

    public ByteBuffer getMappedData() {
        return this.mappedData;
    }

    public void setMappedData(ByteBuffer mappedData) {
        this.mappedData = mappedData;
    }

    public Usage getUsage() {
        return this.usage;
    }

    public void setUsage(Usage usage) {
        this.usage = usage;
    }

    public void setNormalized(boolean normalized) {
        this.normalized = normalized;
    }

    public boolean isNormalized() {
        return this.normalized;
    }

    public Type getBufferType() {
        return this.bufType;
    }

    public Format getFormat() {
        return this.format;
    }

    public int getNumComponents() {
        return this.components;
    }

    public int getNumElements() {
        int elements = this.data.capacity() / this.components;
        if (this.format == Format.Half) {
            elements /= 2;
        }
        return elements;
    }

    public void setupData(Usage usage, int components, Format format, Buffer data) {
        if (this.id != -1) {
            throw new UnsupportedOperationException("Data has already been sent. Cannot setupData again.");
        }
        this.data = data;
        this.components = components;
        this.usage = usage;
        this.format = format;
        this.componentsLength = components * format.getComponentSize();
        this.setUpdateNeeded();
    }

    public void updateData(Buffer data) {
        if (this.id != -1) {
            // empty if block
        }
        if (this.data.capacity() != data.capacity()) {
            this.dataSizeChanged = true;
        }
        this.data = data;
        this.setUpdateNeeded();
    }

    public boolean hasDataSizeChanged() {
        return this.dataSizeChanged;
    }

    @Override
    public void clearUpdateNeeded() {
        super.clearUpdateNeeded();
        this.dataSizeChanged = false;
    }

    public void convertToHalf() {
        if (this.id != -1) {
            throw new UnsupportedOperationException("Data has already been sent.");
        }
        if (this.format != Format.Float) {
            throw new IllegalStateException("Format must be float!");
        }
        int numElements = this.data.capacity() / this.components;
        this.format = Format.Half;
        this.componentsLength = this.components * this.format.getComponentSize();
        ByteBuffer halfData = BufferUtils.createByteBuffer(this.componentsLength * numElements);
        halfData.rewind();
        FloatBuffer floatData = (FloatBuffer)this.data;
        floatData.rewind();
        for (int i = 0; i < floatData.capacity(); ++i) {
            float f = floatData.get(i);
            short half = FastMath.convertFloatToHalf(f);
            halfData.putShort(half);
        }
        this.data = halfData;
        this.setUpdateNeeded();
        this.dataSizeChanged = true;
    }

    public void compact(int numElements) {
        int total = this.components * numElements;
        this.data.clear();
        switch (this.format) {
            case Byte:
            case UnsignedByte:
            case Half:
                ByteBuffer bbuf = (ByteBuffer)this.data;
                bbuf.limit(total);
                ByteBuffer bnewBuf = BufferUtils.createByteBuffer(total);
                bnewBuf.put(bbuf);
                this.data = bnewBuf;
                break;
            case Short:
            case UnsignedShort:
                ShortBuffer sbuf = (ShortBuffer)this.data;
                sbuf.limit(total);
                ShortBuffer snewBuf = BufferUtils.createShortBuffer(total);
                snewBuf.put(sbuf);
                this.data = snewBuf;
                break;
            case Int:
            case UnsignedInt:
                IntBuffer ibuf = (IntBuffer)this.data;
                ibuf.limit(total);
                IntBuffer inewBuf = BufferUtils.createIntBuffer(total);
                inewBuf.put(ibuf);
                this.data = inewBuf;
                break;
            case Float:
                FloatBuffer fbuf = (FloatBuffer)this.data;
                fbuf.limit(total);
                FloatBuffer fnewBuf = BufferUtils.createFloatBuffer(total);
                fnewBuf.put(fbuf);
                this.data = fnewBuf;
                break;
            default:
                throw new UnsupportedOperationException("Unrecognized buffer format: " + this.format);
        }
        this.data.clear();
        this.setUpdateNeeded();
        this.dataSizeChanged = true;
    }

    public void copyElement(int inIndex, VertexBuffer outVb, int outIndex) {
        if (outVb.format != this.format || outVb.components != this.components) {
            throw new IllegalArgumentException("Buffer format mismatch. Cannot copy");
        }
        int inPos = inIndex * this.components;
        int outPos = outIndex * this.components;
        int elementSz = this.components;
        if (this.format == Format.Half) {
            inPos *= 2;
            outPos *= 2;
            elementSz *= 2;
        }
        this.data.clear();
        outVb.data.clear();
        switch (this.format) {
            case Byte:
            case UnsignedByte:
            case Half:
                ByteBuffer bin = (ByteBuffer)this.data;
                ByteBuffer bout = (ByteBuffer)outVb.data;
                bin.position(inPos).limit(inPos + elementSz);
                bout.position(outPos).limit(outPos + elementSz);
                bout.put(bin);
                break;
            case Short:
            case UnsignedShort:
                ShortBuffer sin = (ShortBuffer)this.data;
                ShortBuffer sout = (ShortBuffer)outVb.data;
                sin.position(inPos).limit(inPos + elementSz);
                sout.position(outPos).limit(outPos + elementSz);
                sout.put(sin);
                break;
            case Int:
            case UnsignedInt:
                IntBuffer iin = (IntBuffer)this.data;
                IntBuffer iout = (IntBuffer)outVb.data;
                iin.position(inPos).limit(inPos + elementSz);
                iout.position(outPos).limit(outPos + elementSz);
                iout.put(iin);
                break;
            case Float:
                FloatBuffer fin = (FloatBuffer)this.data;
                FloatBuffer fout = (FloatBuffer)outVb.data;
                fin.position(inPos).limit(inPos + elementSz);
                fout.position(outPos).limit(outPos + elementSz);
                fout.put(fin);
                break;
            default:
                throw new UnsupportedOperationException("Unrecognized buffer format: " + this.format);
        }
        this.data.clear();
        outVb.data.clear();
    }

    public static final Buffer createBuffer(Format format, int components, int numElements) {
        if (components < 1 || components > 4) {
            throw new IllegalArgumentException("Num components must be between 1 and 4");
        }
        int total = numElements * components;
        switch (format) {
            case Byte: 
            case UnsignedByte: {
                return BufferUtils.createByteBuffer(total);
            }
            case Half: {
                return BufferUtils.createByteBuffer(total * 2);
            }
            case Short: 
            case UnsignedShort: {
                return BufferUtils.createShortBuffer(total);
            }
            case Int: 
            case UnsignedInt: {
                return BufferUtils.createIntBuffer(total);
            }
            case Float: {
                return BufferUtils.createFloatBuffer(total);
            }
            case Double: {
                return BufferUtils.createDoubleBuffer(total);
            }
        }
        throw new UnsupportedOperationException("Unrecoginized buffer format: " + format);
    }

    @Override
    public VertexBuffer clone() {
        VertexBuffer vb = (VertexBuffer)super.clone();
        if (this.data != null) {
            vb.updateData(BufferUtils.clone(this.data));
        }
        return vb;
    }

    public VertexBuffer clone(Type overrideType) {
        VertexBuffer vb = new VertexBuffer(overrideType);
        vb.components = this.components;
        vb.componentsLength = this.componentsLength;
        vb.data = BufferUtils.clone(this.data);
        vb.format = this.format;
        vb.handleRef = new Object();
        vb.id = -1;
        vb.normalized = this.normalized;
        vb.offset = this.offset;
        vb.stride = this.stride;
        vb.updateNeeded = true;
        vb.usage = this.usage;
        return vb;
    }

    @Override
    public String toString() {
        String dataTxt = null;
        if (this.data != null) {
            dataTxt = ", elements=" + this.data.capacity();
        }
        return this.getClass().getSimpleName() + "[fmt=" + this.format.name() + ", type=" + this.bufType.name() + ", usage=" + this.usage.name() + dataTxt + "]";
    }

    @Override
    public void resetObject() {
        this.id = -1;
        this.setUpdateNeeded();
    }

    @Override
    public GLObject createDestructableClone() {
        return new VertexBuffer(this.id);
    }

    public static enum Format {
        Half(2),
        Float(4),
        Double(8),
        Byte(1),
        UnsignedByte(1),
        Short(2),
        UnsignedShort(2),
        Int(4),
        UnsignedInt(4);

        private int componentSize = 0;

        private Format(int componentSize) {
            this.componentSize = componentSize;
        }

        public int getComponentSize() {
            return this.componentSize;
        }
    }

    public static enum Usage {
        Static,
        Dynamic,
        Stream,
        CpuOnly;

    }

    public static enum Type {
        Position,
        Size,
        Normal,
        TexCoord,
        Color,
        Tangent,
        Binormal,
        InterleavedData,
        @Deprecated
        MiscAttrib,
        Index,
        BindPosePosition,
        BindPoseNormal,
        BoneWeight,
        BoneIndex,
        TexCoord2;

    }
}

