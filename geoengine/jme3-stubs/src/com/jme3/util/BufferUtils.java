/*
 * COMPILE-ONLY STUB. Not part of geoEngine and never shipped or loaded at run time.
 * geoEngine-0.1.jar contains two dead classes (aionjHungary.geoEngine.Meshs and ModelList) whose
 * bytecode references jMonkeyEngine3 (com.jme3.*), which is not in the jar nor on the gameserver
 * classpath. This stub declares exactly the members those two classes reference (taken from
 * their constant pools) so that they compile with identical signatures and bytecode. At run time,
 * as with the original jar, touching Meshs/ModelList fails with NoClassDefFoundError.
 */
package com.jme3.util;

import com.jme3.math.Vector3f;
import java.nio.FloatBuffer;
import java.nio.ShortBuffer;

public final class BufferUtils {
    public static FloatBuffer createFloatBuffer(Vector3f... data) {
        throw new UnsupportedOperationException("jME3 compile-only stub");
    }

    public static ShortBuffer createShortBuffer(short... data) {
        throw new UnsupportedOperationException("jME3 compile-only stub");
    }
}
