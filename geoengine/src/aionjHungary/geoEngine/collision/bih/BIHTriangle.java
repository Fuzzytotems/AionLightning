/*
 * geoEngine 0.1 (package aionjHungary.geoEngine) - a cut-down copy of the jMonkeyEngine3 math,
 * scene-graph and collision code (BSD licence, jmonkeyengine.org) repackaged by "aionjHungary".
 * Source recovered from gameserver/lib/geoEngine-0.1.jar with CFR 0.152 and repaired so that it
 * recompiles to the jar's API and (modulo javac-version differences) bytecode; see ../../README.md.
 */
package aionjHungary.geoEngine.collision.bih;

import aionjHungary.geoEngine.math.Vector3f;

public final class BIHTriangle {
    private final Vector3f pointa = new Vector3f();
    private final Vector3f pointb = new Vector3f();
    private final Vector3f pointc = new Vector3f();
    private final Vector3f center = new Vector3f();

    public BIHTriangle(Vector3f p1, Vector3f p2, Vector3f p3) {
        this.pointa.set(p1);
        this.pointb.set(p2);
        this.pointc.set(p3);
        this.center.set(this.pointa);
        this.center.addLocal(this.pointb).addLocal(this.pointc).multLocal(0.33333334f);
    }

    public Vector3f get1() {
        return this.pointa;
    }

    public Vector3f get2() {
        return this.pointb;
    }

    public Vector3f get3() {
        return this.pointc;
    }

    public Vector3f getCenter() {
        return this.center;
    }

    public Vector3f getNormal() {
        Vector3f normal = new Vector3f(this.pointb);
        normal.subtractLocal(this.pointa).crossLocal(this.pointc.x - this.pointa.x, this.pointc.y - this.pointa.y, this.pointc.z - this.pointa.z);
        normal.normalizeLocal();
        return normal;
    }

    public float getExtreme(int axis, boolean left) {
        float v3;
        float v2;
        float v1;
        switch (axis) {
            case 0: {
                v1 = this.pointa.x;
                v2 = this.pointb.x;
                v3 = this.pointc.x;
                break;
            }
            case 1: {
                v1 = this.pointa.y;
                v2 = this.pointb.y;
                v3 = this.pointc.y;
                break;
            }
            case 2: {
                v1 = this.pointa.z;
                v2 = this.pointb.z;
                v3 = this.pointc.z;
                break;
            }
            default: {
                assert (false);
                return 0.0f;
            }
        }
        if (left) {
            if (v1 < v2) {
                if (v1 < v3) {
                    return v1;
                }
                return v3;
            }
            if (v2 < v3) {
                return v2;
            }
            return v3;
        }
        if (v1 > v2) {
            if (v1 > v3) {
                return v1;
            }
            return v3;
        }
        if (v2 > v3) {
            return v2;
        }
        return v3;
    }
}

