/*
 * geoEngine 0.1 (package aionjHungary.geoEngine) - a cut-down copy of the jMonkeyEngine3 math,
 * scene-graph and collision code (BSD licence, jmonkeyengine.org) repackaged by "aionjHungary".
 * Source recovered from gameserver/lib/geoEngine-0.1.jar with CFR 0.152 and repaired so that it
 * recompiles to the jar's API and (modulo javac-version differences) bytecode; see ../../README.md.
 */
package aionjHungary.geoEngine.bounding;

import aionjHungary.geoEngine.bounding.BoundingBox;
import aionjHungary.geoEngine.collision.Collidable;
import aionjHungary.geoEngine.math.Matrix4f;
import aionjHungary.geoEngine.math.Plane;
import aionjHungary.geoEngine.math.Ray;
import aionjHungary.geoEngine.math.Transform;
import aionjHungary.geoEngine.math.Vector3f;
import java.nio.FloatBuffer;

public abstract class BoundingVolume
implements Collidable {
    protected int checkPlane = 0;
    Vector3f center = new Vector3f();

    public BoundingVolume() {
    }

    public BoundingVolume(Vector3f center) {
        this.center.set(center);
    }

    public int getCheckPlane() {
        return this.checkPlane;
    }

    public final void setCheckPlane(int value) {
        this.checkPlane = value;
    }

    public abstract Type getType();

    public final BoundingVolume transform(Transform trans) {
        return this.transform(trans, null);
    }

    public abstract BoundingVolume transform(Transform var1, BoundingVolume var2);

    public abstract BoundingVolume transform(Matrix4f var1, BoundingVolume var2);

    public abstract Plane.Side whichSide(Plane var1);

    public abstract void computeFromPoints(FloatBuffer var1);

    public abstract BoundingVolume merge(BoundingVolume var1);

    public abstract BoundingVolume mergeLocal(BoundingVolume var1);

    public abstract BoundingVolume clone(BoundingVolume var1);

    public final Vector3f getCenter() {
        return this.center;
    }

    public final Vector3f getCenter(Vector3f store) {
        store.set(this.center);
        return store;
    }

    public final void setCenter(Vector3f newCenter) {
        this.center = newCenter;
    }

    public final float distanceTo(Vector3f point) {
        return this.center.distance(point);
    }

    public final float distanceSquaredTo(Vector3f point) {
        return this.center.distanceSquared(point);
    }

    public abstract float distanceToEdge(Vector3f var1);

    public abstract boolean intersects(BoundingVolume var1);

    public abstract boolean intersects(Ray var1);

    public abstract boolean intersectsBoundingBox(BoundingBox var1);

    public abstract boolean contains(Vector3f var1);

    public abstract float getVolume();

    public BoundingVolume clone() {
        try {
            BoundingVolume clone = (BoundingVolume)super.clone();
            clone.center = this.center.clone();
            return clone;
        }
        catch (CloneNotSupportedException ex) {
            throw new AssertionError();
        }
    }

    public static enum Type {
        Sphere,
        AABB,
        OBB,
        Capsule;

    }
}

