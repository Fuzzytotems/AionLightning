/*
 * geoEngine 0.1 (package aionjHungary.geoEngine) - a cut-down copy of the jMonkeyEngine3 math,
 * scene-graph and collision code (BSD licence, jmonkeyengine.org) repackaged by "aionjHungary".
 * Source recovered from gameserver/lib/geoEngine-0.1.jar with CFR 0.152 and repaired so that it
 * recompiles to the jar's API and (modulo javac-version differences) bytecode; see ../../README.md.
 */
package aionjHungary.geoEngine.scene.mesh;

import aionjHungary.geoEngine.scene.mesh.IndexBuffer;
import java.nio.Buffer;
import java.nio.IntBuffer;

public class IndexIntBuffer
extends IndexBuffer {
    private IntBuffer buf;

    public IndexIntBuffer(IntBuffer buffer) {
        this.buf = buffer;
    }

    @Override
    public int get(int i) {
        return this.buf.get(i);
    }

    @Override
    public void put(int i, int value) {
        this.buf.put(i, value);
    }

    @Override
    public int size() {
        return this.buf.limit();
    }

    @Override
    public Buffer getBuffer() {
        return this.buf;
    }
}

