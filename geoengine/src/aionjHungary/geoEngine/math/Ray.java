/*
 * geoEngine 0.1 (package aionjHungary.geoEngine) - a cut-down copy of the jMonkeyEngine3 math,
 * scene-graph and collision code (BSD licence, jmonkeyengine.org) repackaged by "aionjHungary".
 * Source recovered from gameserver/lib/geoEngine-0.1.jar with CFR 0.152 and repaired so that it
 * recompiles to the jar's API and (modulo javac-version differences) bytecode; see ../../README.md.
 */
package aionjHungary.geoEngine.math;

import aionjHungary.geoEngine.bounding.BoundingVolume;
import aionjHungary.geoEngine.collision.Collidable;
import aionjHungary.geoEngine.collision.CollisionResult;
import aionjHungary.geoEngine.collision.CollisionResults;
import aionjHungary.geoEngine.collision.UnsupportedCollisionException;
import aionjHungary.geoEngine.math.AbstractTriangle;
import aionjHungary.geoEngine.math.Plane;
import aionjHungary.geoEngine.math.Triangle;
import aionjHungary.geoEngine.math.Vector3f;
import aionjHungary.geoEngine.utils.TempVars;

public final class Ray
implements Cloneable,
Collidable {
    private static final long serialVersionUID = 1L;
    public Vector3f origin;
    public Vector3f direction;
    public float limit = Float.POSITIVE_INFINITY;

    public Ray() {
        this.origin = new Vector3f();
        this.direction = new Vector3f();
    }

    public Ray(Vector3f origin, Vector3f direction) {
        this.origin = origin;
        this.direction = direction;
    }

    public boolean intersectWhere(Triangle t, Vector3f loc) {
        return this.intersectWhere(t.get(0), t.get(1), t.get(2), loc);
    }

    public boolean intersectWhere(Vector3f v0, Vector3f v1, Vector3f v2, Vector3f loc) {
        return this.intersects(v0, v1, v2, loc, false, false);
    }

    public boolean intersectWherePlanar(Triangle t, Vector3f loc) {
        return this.intersectWherePlanar(t.get(0), t.get(1), t.get(2), loc);
    }

    public boolean intersectWherePlanar(Vector3f v0, Vector3f v1, Vector3f v2, Vector3f loc) {
        return this.intersects(v0, v1, v2, loc, true, false);
    }

    private boolean intersects(Vector3f v0, Vector3f v1, Vector3f v2, Vector3f store, boolean doPlanar, boolean quad) {
        TempVars vars = TempVars.get();
        assert (vars.lock());
        Vector3f tempVa = vars.vect1;
        Vector3f tempVb = vars.vect2;
        Vector3f tempVc = vars.vect3;
        Vector3f tempVd = vars.vect4;
        Vector3f diff = this.origin.subtract(v0, tempVa);
        Vector3f edge1 = v1.subtract(v0, tempVb);
        Vector3f edge2 = v2.subtract(v0, tempVc);
        Vector3f norm = edge1.cross(edge2, tempVd);
        float dirDotNorm = this.direction.dot(norm);
        float sign;
        if (dirDotNorm > 1.1920929E-7f) {
            sign = 1.0f;
        } else if (dirDotNorm < -1.1920929E-7f) {
            sign = -1.0f;
            dirDotNorm = -dirDotNorm;
        } else {
            return false;
        }
        float dirDotDiffxEdge2 = sign * this.direction.dot(diff.cross(edge2, edge2));
        if (dirDotDiffxEdge2 >= 0.0f) {
            float dirDotEdge1xDiff = sign * this.direction.dot(edge1.crossLocal(diff));
            if (dirDotEdge1xDiff >= 0.0f) {
                if (!quad ? dirDotDiffxEdge2 + dirDotEdge1xDiff <= dirDotNorm : dirDotEdge1xDiff <= dirDotNorm) {
                    float diffDotNorm = -sign * diff.dot(norm);
                    if (diffDotNorm >= 0.0f) {
                        assert (vars.unlock());
                        if (store == null) {
                            return true;
                        }
                        float inv = 1.0f / dirDotNorm;
                        float t = diffDotNorm * inv;
                        if (!doPlanar) {
                            store.set(this.origin).addLocal(this.direction.x * t, this.direction.y * t, this.direction.z * t);
                        } else {
                            float w1 = dirDotDiffxEdge2 * inv;
                            float w2 = dirDotEdge1xDiff * inv;
                            store.set(t, w1, w2);
                        }
                        return true;
                    }
                }
            }
        }
        assert (vars.unlock());
        return false;
    }

    public float intersects(Vector3f v0, Vector3f v1, Vector3f v2) {
        float edge1X = v1.x - v0.x;
        float edge1Y = v1.y - v0.y;
        float edge1Z = v1.z - v0.z;
        float edge2X = v2.x - v0.x;
        float edge2Y = v2.y - v0.y;
        float edge2Z = v2.z - v0.z;
        float normX = edge1Y * edge2Z - edge1Z * edge2Y;
        float normY = edge1Z * edge2X - edge1X * edge2Z;
        float normZ = edge1X * edge2Y - edge1Y * edge2X;
        float dirDotNorm = this.direction.x * normX + this.direction.y * normY + this.direction.z * normZ;
        float diffX = this.origin.x - v0.x;
        float diffY = this.origin.y - v0.y;
        float diffZ = this.origin.z - v0.z;
        float sign;
        if (dirDotNorm > 1.1920929E-7f) {
            sign = 1.0f;
        } else if (dirDotNorm < -1.1920929E-7f) {
            sign = -1.0f;
            dirDotNorm = -dirDotNorm;
        } else {
            return Float.POSITIVE_INFINITY;
        }
        float diffEdge2X = diffY * edge2Z - diffZ * edge2Y;
        float diffEdge2Y = diffZ * edge2X - diffX * edge2Z;
        float diffEdge2Z = diffX * edge2Y - diffY * edge2X;
        float dirDotDiffxEdge2 = sign * (this.direction.x * diffEdge2X + this.direction.y * diffEdge2Y + this.direction.z * diffEdge2Z);
        if (dirDotDiffxEdge2 >= 0.0f) {
            diffEdge2X = edge1Y * diffZ - edge1Z * diffY;
            diffEdge2Y = edge1Z * diffX - edge1X * diffZ;
            diffEdge2Z = edge1X * diffY - edge1Y * diffX;
            float dirDotEdge1xDiff = sign * (this.direction.x * diffEdge2X + this.direction.y * diffEdge2Y + this.direction.z * diffEdge2Z);
            if (dirDotEdge1xDiff >= 0.0f) {
                if (dirDotDiffxEdge2 + dirDotEdge1xDiff <= dirDotNorm) {
                    float diffDotNorm = -sign * (diffX * normX + diffY * normY + diffZ * normZ);
                    if (diffDotNorm >= 0.0f) {
                        float inv = 1.0f / dirDotNorm;
                        float t = diffDotNorm * inv;
                        return t;
                    }
                }
            }
        }
        return Float.POSITIVE_INFINITY;
    }

    public boolean intersectWherePlanarQuad(Vector3f v0, Vector3f v1, Vector3f v2, Vector3f loc) {
        return this.intersects(v0, v1, v2, loc, true, true);
    }

    public boolean intersectsWherePlane(Plane p, Vector3f loc) {
        float denominator = p.getNormal().dot(this.direction);
        if (denominator > -1.1920929E-7f && denominator < 1.1920929E-7f) {
            return false;
        }
        float numerator = -(p.getNormal().dot(this.origin) - p.getConstant());
        float ratio = numerator / denominator;
        if (ratio < 1.1920929E-7f) {
            return false;
        }
        loc.set(this.direction).multLocal(ratio).addLocal(this.origin);
        return true;
    }

    @Override
    public int collideWith(Collidable other, CollisionResults results) {
        if (other instanceof BoundingVolume) {
            BoundingVolume bv = (BoundingVolume)other;
            return bv.collideWith(this, results);
        }
        if (other instanceof AbstractTriangle) {
            AbstractTriangle tri = (AbstractTriangle)other;
            float d = this.intersects(tri.get1(), tri.get2(), tri.get3());
            if (Float.isInfinite(d) || Float.isNaN(d)) {
                return 0;
            }
            Vector3f point = new Vector3f(this.direction).multLocal(d).addLocal(this.origin);
            results.addCollision(new CollisionResult(point, d));
            return 1;
        }
        throw new UnsupportedCollisionException();
    }

    public float distanceSquared(Vector3f point) {
        TempVars vars = TempVars.get();
        assert (vars.lock());
        Vector3f tempVa = vars.vect1;
        Vector3f tempVb = vars.vect2;
        point.subtract(this.origin, tempVa);
        float rayParam = this.direction.dot(tempVa);
        if (rayParam > 0.0f) {
            this.origin.add(this.direction.mult(rayParam, tempVb), tempVb);
        } else {
            tempVb.set(this.origin);
            rayParam = 0.0f;
        }
        tempVb.subtract(point, tempVa);
        float len = tempVa.lengthSquared();
        assert (vars.unlock());
        return len;
    }

    public Vector3f getOrigin() {
        return this.origin;
    }

    public void setOrigin(Vector3f origin) {
        this.origin.set(origin);
    }

    public float getLimit() {
        return this.limit;
    }

    public void setLimit(float limit) {
        this.limit = limit;
    }

    public Vector3f getDirection() {
        return this.direction;
    }

    public void setDirection(Vector3f direction) {
        this.direction.set(direction);
    }

    public void set(Ray source) {
        this.origin.set(source.getOrigin());
        this.direction.set(source.getDirection());
    }

    public String toString() {
        return this.getClass().getSimpleName() + " [Origin: " + this.origin + ", Direction: " + this.direction + "]";
    }

    public Class<? extends Ray> getClassTag() {
        return this.getClass();
    }

    public Ray clone() {
        try {
            Ray r = (Ray)super.clone();
            r.direction = this.direction.clone();
            r.origin = this.origin.clone();
            return r;
        }
        catch (CloneNotSupportedException e) {
            throw new AssertionError();
        }
    }
}

