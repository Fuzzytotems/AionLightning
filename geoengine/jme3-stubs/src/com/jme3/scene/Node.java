/*
 * COMPILE-ONLY STUB. Not part of geoEngine and never shipped or loaded at run time.
 * geoEngine-0.1.jar contains two dead classes (aionjHungary.geoEngine.Meshs and ModelList) whose
 * bytecode references jMonkeyEngine3 (com.jme3.*), which is not in the jar nor on the gameserver
 * classpath. This stub declares exactly the members those two classes reference (taken from
 * their constant pools) so that they compile with identical signatures and bytecode. At run time,
 * as with the original jar, touching Meshs/ModelList fails with NoClassDefFoundError.
 */
package com.jme3.scene;

import com.jme3.bounding.BoundingVolume;

public class Node extends Spatial {
    public Node(String name) {
        throw new UnsupportedOperationException("jME3 compile-only stub");
    }

    public int attachChild(Spatial child) {
        throw new UnsupportedOperationException("jME3 compile-only stub");
    }

    public void setModelBound(BoundingVolume modelBound) {
        throw new UnsupportedOperationException("jME3 compile-only stub");
    }

    public void updateGeometricState() {
        throw new UnsupportedOperationException("jME3 compile-only stub");
    }
}
