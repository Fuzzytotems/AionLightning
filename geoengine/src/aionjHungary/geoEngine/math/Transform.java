/*
 * geoEngine 0.1 (package aionjHungary.geoEngine) - a cut-down copy of the jMonkeyEngine3 math,
 * scene-graph and collision code (BSD licence, jmonkeyengine.org) repackaged by "aionjHungary".
 * Source recovered from gameserver/lib/geoEngine-0.1.jar with CFR 0.152 and repaired so that it
 * recompiles to the jar's API and (modulo javac-version differences) bytecode; see ../../README.md.
 */
package aionjHungary.geoEngine.math;

import aionjHungary.geoEngine.math.Quaternion;
import aionjHungary.geoEngine.math.Vector3f;

public final class Transform
implements Cloneable {
    public static final Transform Identity = new Transform();
    private Quaternion rot = new Quaternion();
    private Vector3f translation = new Vector3f();
    private Vector3f scale = new Vector3f(1.0f, 1.0f, 1.0f);

    public Transform(Vector3f translation, Quaternion rot) {
        this.translation.set(translation);
        this.rot.set(rot);
    }

    public Transform(Vector3f translation) {
        this(translation, Quaternion.IDENTITY);
    }

    public Transform(Quaternion rot) {
        this(Vector3f.ZERO, rot);
    }

    public Transform() {
        this(Vector3f.ZERO, Quaternion.IDENTITY);
    }

    public Transform setRotation(Quaternion rot) {
        this.rot.set(rot);
        return this;
    }

    public Transform setTranslation(Vector3f trans) {
        this.translation.set(trans);
        return this;
    }

    public Vector3f getTranslation() {
        return this.translation;
    }

    public Transform setScale(Vector3f scale) {
        this.scale.set(scale);
        return this;
    }

    public Transform setScale(float scale) {
        this.scale.set(scale, scale, scale);
        return this;
    }

    public Vector3f getScale() {
        return this.scale;
    }

    public Vector3f getTranslation(Vector3f trans) {
        if (trans == null) {
            trans = new Vector3f();
        }
        trans.set(this.translation);
        return trans;
    }

    public Quaternion getRotation(Quaternion quat) {
        if (quat == null) {
            quat = new Quaternion();
        }
        quat.set(this.rot);
        return quat;
    }

    public Quaternion getRotation() {
        return this.rot;
    }

    public Vector3f getScale(Vector3f scale) {
        if (scale == null) {
            scale = new Vector3f();
        }
        scale.set(this.scale);
        return scale;
    }

    public void interpolateTransforms(Transform t1, Transform t2, float delta) {
        this.rot.slerp(t1.rot, t2.rot, delta);
        this.translation.interpolate(t1.translation, t2.translation, delta);
        this.scale.interpolate(t1.scale, t2.scale, delta);
    }

    public Transform combineWithParent(Transform parent) {
        this.scale.multLocal(parent.scale);
        parent.rot.mult(this.rot, this.rot);
        parent.rot.multLocal(this.translation).multLocal(parent.scale).addLocal(parent.translation);
        return this;
    }

    public Transform setTranslation(float x, float y, float z) {
        this.translation.set(x, y, z);
        return this;
    }

    public Transform setScale(float x, float y, float z) {
        this.scale.set(x, y, z);
        return this;
    }

    public Vector3f transformVector(Vector3f in, Vector3f store) {
        if (store == null) {
            store = new Vector3f();
        }
        return this.rot.mult(store.set(in).multLocal(this.scale), store).addLocal(this.translation);
    }

    public Vector3f transformInverseVector(Vector3f in, Vector3f store) {
        if (store == null) {
            store = new Vector3f();
        }
        in.subtract(this.translation, store).divideLocal(this.scale);
        this.rot.inverse().mult(store, store);
        return store;
    }

    public void loadIdentity() {
        this.translation.set(0.0f, 0.0f, 0.0f);
        this.scale.set(1.0f, 1.0f, 1.0f);
        this.rot.set(0.0f, 0.0f, 0.0f, 1.0f);
    }

    public String toString() {
        return this.getClass().getSimpleName() + "[ " + this.translation.x + ", " + this.translation.y + ", " + this.translation.z + "]\n" + "[ " + this.rot.x + ", " + this.rot.y + ", " + this.rot.z + ", " + this.rot.w + "]";
    }

    public Transform set(Transform matrixQuat) {
        this.translation.set(matrixQuat.translation);
        this.rot.set(matrixQuat.rot);
        this.scale.set(matrixQuat.scale);
        return this;
    }

    public Class<? extends Transform> getClassTag() {
        return this.getClass();
    }

    public Transform clone() {
        try {
            Transform tq = (Transform)super.clone();
            tq.rot = this.rot.clone();
            tq.scale = this.scale.clone();
            tq.translation = this.translation.clone();
            return tq;
        }
        catch (CloneNotSupportedException e) {
            throw new AssertionError();
        }
    }
}

