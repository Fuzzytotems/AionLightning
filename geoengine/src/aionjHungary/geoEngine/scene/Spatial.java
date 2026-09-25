/*
 * geoEngine 0.1 (package aionjHungary.geoEngine) - a cut-down copy of the jMonkeyEngine3 math,
 * scene-graph and collision code (BSD licence, jmonkeyengine.org) repackaged by "aionjHungary".
 * Source recovered from gameserver/lib/geoEngine-0.1.jar with CFR 0.152 and repaired so that it
 * recompiles to the jar's API and (modulo javac-version differences) bytecode; see ../../README.md.
 */
package aionjHungary.geoEngine.scene;

import aionjHungary.geoEngine.bounding.BoundingVolume;
import aionjHungary.geoEngine.collision.Collidable;
import aionjHungary.geoEngine.math.Matrix3f;
import aionjHungary.geoEngine.math.Vector3f;
import aionjHungary.geoEngine.scene.Node;

public abstract class Spatial
implements Collidable,
Cloneable {
    protected BoundingVolume worldBound;
    protected String name;
    protected transient Node parent;

    public Spatial() {
    }

    public Spatial(String name) {
        this();
        this.name = name;
    }

    public void setName(String name) {
        this.name = name;
    }

    public String getName() {
        return this.name;
    }

    public Node getParent() {
        return this.parent;
    }

    protected void setParent(Node parent) {
        this.parent = parent;
    }

    public boolean removeFromParent() {
        if (this.parent != null) {
            this.parent.detachChild(this);
            return true;
        }
        return false;
    }

    public boolean hasAncestor(Node ancestor) {
        if (this.parent == null) {
            return false;
        }
        if (this.parent.equals(ancestor)) {
            return true;
        }
        return this.parent.hasAncestor(ancestor);
    }

    public abstract void updateModelBound();

    public abstract void setModelBound(BoundingVolume var1);

    public abstract int getVertexCount();

    public abstract int getTriangleCount();

    public boolean matches(Class<? extends Spatial> spatialSubclass, String nameRegex) {
        if (spatialSubclass != null && !spatialSubclass.isInstance(this)) {
            return false;
        }
        if (nameRegex != null && (this.name == null || !this.name.matches(nameRegex))) {
            return false;
        }
        return true;
    }

    public BoundingVolume getWorldBound() {
        return this.worldBound;
    }

    public String toString() {
        return this.name + " (" + this.getClass().getSimpleName() + ')';
    }

    public abstract void setTransform(Matrix3f var1, Vector3f var2, float var3);

    public Spatial clone() throws CloneNotSupportedException {
        return (Spatial)super.clone();
    }

    public static enum CullHint {
        Inherit,
        Dynamic,
        Always,
        Never;

    }
}

