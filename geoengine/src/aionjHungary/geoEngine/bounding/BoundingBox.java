/*
 * geoEngine 0.1 (package aionjHungary.geoEngine) - a cut-down copy of the jMonkeyEngine3 math,
 * scene-graph and collision code (BSD licence, jmonkeyengine.org) repackaged by "aionjHungary".
 * Source recovered from gameserver/lib/geoEngine-0.1.jar with CFR 0.152 and repaired so that it
 * recompiles to the jar's API and (modulo javac-version differences) bytecode; see ../../README.md.
 */
package aionjHungary.geoEngine.bounding;

import aionjHungary.geoEngine.bounding.BoundingVolume;
import aionjHungary.geoEngine.bounding.Intersection;
import aionjHungary.geoEngine.collision.Collidable;
import aionjHungary.geoEngine.collision.CollisionResult;
import aionjHungary.geoEngine.collision.CollisionResults;
import aionjHungary.geoEngine.collision.UnsupportedCollisionException;
import aionjHungary.geoEngine.math.FastMath;
import aionjHungary.geoEngine.math.Matrix3f;
import aionjHungary.geoEngine.math.Matrix4f;
import aionjHungary.geoEngine.math.Plane;
import aionjHungary.geoEngine.math.Ray;
import aionjHungary.geoEngine.math.Transform;
import aionjHungary.geoEngine.math.Triangle;
import aionjHungary.geoEngine.math.Vector3f;
import aionjHungary.geoEngine.scene.Mesh;
import aionjHungary.geoEngine.utils.BufferUtils;
import aionjHungary.geoEngine.utils.TempVars;
import java.nio.FloatBuffer;

public class BoundingBox
extends BoundingVolume {
    float xExtent;
    float yExtent;
    float zExtent;

    public BoundingBox() {
    }

    public BoundingBox(Vector3f c, float x, float y, float z) {
        this.center.set(c);
        this.xExtent = x;
        this.yExtent = y;
        this.zExtent = z;
    }

    public BoundingBox(BoundingBox source) {
        this.center.set(source.center);
        this.xExtent = source.xExtent;
        this.yExtent = source.yExtent;
        this.zExtent = source.zExtent;
    }

    public BoundingBox(Vector3f min, Vector3f max) {
        this.setMinMax(min, max);
    }

    @Override
    public BoundingVolume.Type getType() {
        return BoundingVolume.Type.AABB;
    }

    @Override
    public void computeFromPoints(FloatBuffer points) {
        this.containAABB(points);
    }

    public void computeFromTris(Triangle[] tris, int start, int end) {
        if (end - start <= 0) {
            return;
        }
        TempVars vars = TempVars.get();
        assert (vars.lock());
        Vector3f min = vars.vect1.set(new Vector3f(Float.POSITIVE_INFINITY, Float.POSITIVE_INFINITY, Float.POSITIVE_INFINITY));
        Vector3f max = vars.vect2.set(new Vector3f(Float.NEGATIVE_INFINITY, Float.NEGATIVE_INFINITY, Float.NEGATIVE_INFINITY));
        for (int i = start; i < end; ++i) {
            Vector3f point = tris[i].get(0);
            BoundingBox.checkMinMax(min, max, point);
            point = tris[i].get(1);
            BoundingBox.checkMinMax(min, max, point);
            point = tris[i].get(2);
            BoundingBox.checkMinMax(min, max, point);
        }
        this.center.set(min.addLocal(max));
        this.center.multLocal(0.5f);
        this.xExtent = max.x - this.center.x;
        this.yExtent = max.y - this.center.y;
        this.zExtent = max.z - this.center.z;
        assert (vars.unlock());
    }

    public void computeFromTris(int[] indices, Mesh mesh, int start, int end) {
        if (end - start <= 0) {
            return;
        }
        TempVars vars = TempVars.get();
        assert (vars.lock());
        Vector3f vect1 = vars.vect1;
        Vector3f vect2 = vars.vect2;
        Triangle triangle = vars.triangle;
        Vector3f min = vect1.set(Float.POSITIVE_INFINITY, Float.POSITIVE_INFINITY, Float.POSITIVE_INFINITY);
        Vector3f max = vect2.set(Float.NEGATIVE_INFINITY, Float.NEGATIVE_INFINITY, Float.NEGATIVE_INFINITY);
        for (int i = start; i < end; ++i) {
            mesh.getTriangle(indices[i], triangle);
            Vector3f point = triangle.get(0);
            BoundingBox.checkMinMax(min, max, point);
            point = triangle.get(1);
            BoundingBox.checkMinMax(min, max, point);
            point = triangle.get(2);
            BoundingBox.checkMinMax(min, max, point);
        }
        this.center.set(min.addLocal(max));
        this.center.multLocal(0.5f);
        this.xExtent = max.x - this.center.x;
        this.yExtent = max.y - this.center.y;
        this.zExtent = max.z - this.center.z;
        assert (vars.unlock());
    }

    public static final void checkMinMax(Vector3f min, Vector3f max, Vector3f point) {
        if (point.x < min.x) {
            min.x = point.x;
        }
        if (point.x > max.x) {
            max.x = point.x;
        }
        if (point.y < min.y) {
            min.y = point.y;
        }
        if (point.y > max.y) {
            max.y = point.y;
        }
        if (point.z < min.z) {
            min.z = point.z;
        }
        if (point.z > max.z) {
            max.z = point.z;
        }
    }

    public void containAABB(FloatBuffer points) {
        if (points == null) {
            return;
        }
        points.rewind();
        if (points.remaining() <= 2) {
            return;
        }
        TempVars vars = TempVars.get();
        assert (vars.lock());
        BufferUtils.populateFromBuffer(vars.vect1, points, 0);
        float minX = vars.vect1.x;
        float minY = vars.vect1.y;
        float minZ = vars.vect1.z;
        float maxX = vars.vect1.x;
        float maxY = vars.vect1.y;
        float maxZ = vars.vect1.z;
        for (int i = 1, len = points.remaining() / 3; i < len; ++i) {
            BufferUtils.populateFromBuffer(vars.vect1, points, i);
            if (vars.vect1.x < minX) {
                minX = vars.vect1.x;
            } else if (vars.vect1.x > maxX) {
                maxX = vars.vect1.x;
            }
            if (vars.vect1.y < minY) {
                minY = vars.vect1.y;
            } else if (vars.vect1.y > maxY) {
                maxY = vars.vect1.y;
            }
            if (vars.vect1.z < minZ) {
                minZ = vars.vect1.z;
                continue;
            }
            if (!(vars.vect1.z > maxZ)) continue;
            maxZ = vars.vect1.z;
        }
        assert (vars.unlock());
        this.center.set(minX + maxX, minY + maxY, minZ + maxZ);
        this.center.multLocal(0.5f);
        this.xExtent = maxX - this.center.x;
        this.yExtent = maxY - this.center.y;
        this.zExtent = maxZ - this.center.z;
    }

    @Override
    public BoundingVolume transform(Transform trans, BoundingVolume store) {
        BoundingBox box;
        if (store == null || store.getType() != BoundingVolume.Type.AABB) {
            box = new BoundingBox();
        } else {
            box = (BoundingBox)store;
        }
        this.center.mult(trans.getScale(), box.center);
        trans.getRotation().mult(box.center, box.center);
        box.center.addLocal(trans.getTranslation());
        TempVars vars = TempVars.get();
        assert (vars.lock());
        Matrix3f transMatrix = vars.tempMat3;
        transMatrix.set(trans.getRotation());
        transMatrix.absoluteLocal();
        Vector3f scale = trans.getScale();
        vars.vect1.set(this.xExtent * scale.x, this.yExtent * scale.y, this.zExtent * scale.z);
        transMatrix.mult(vars.vect1, vars.vect2);
        box.xExtent = FastMath.abs(vars.vect2.getX());
        box.yExtent = FastMath.abs(vars.vect2.getY());
        box.zExtent = FastMath.abs(vars.vect2.getZ());
        assert (vars.unlock());
        return box;
    }

    @Override
    public BoundingVolume transform(Matrix4f trans, BoundingVolume store) {
        BoundingBox box;
        if (store == null || store.getType() != BoundingVolume.Type.AABB) {
            box = new BoundingBox();
        } else {
            box = (BoundingBox)store;
        }
        TempVars vars = TempVars.get();
        assert (vars.lock());
        float w = trans.multProj(this.center, box.center);
        box.center.divideLocal(w);
        Matrix3f transMatrix = vars.tempMat3;
        trans.toRotationMatrix(transMatrix);
        transMatrix.absoluteLocal();
        vars.vect1.set(this.xExtent, this.yExtent, this.zExtent);
        transMatrix.mult(vars.vect1, vars.vect1);
        box.xExtent = FastMath.abs(vars.vect1.getX());
        box.yExtent = FastMath.abs(vars.vect1.getY());
        box.zExtent = FastMath.abs(vars.vect1.getZ());
        assert (vars.unlock());
        return box;
    }

    @Override
    public Plane.Side whichSide(Plane plane) {
        float radius = FastMath.abs(this.xExtent * plane.getNormal().getX()) + FastMath.abs(this.yExtent * plane.getNormal().getY()) + FastMath.abs(this.zExtent * plane.getNormal().getZ());
        float distance = plane.pseudoDistance(this.center);
        if (distance < -radius) {
            return Plane.Side.Negative;
        }
        if (distance > radius) {
            return Plane.Side.Positive;
        }
        return Plane.Side.None;
    }

    @Override
    public BoundingVolume merge(BoundingVolume volume) {
        if (volume == null) {
            return this;
        }
        switch (volume.getType()) {
            case AABB: {
                BoundingBox vBox = (BoundingBox)volume;
                return this.merge(vBox.center, vBox.xExtent, vBox.yExtent, vBox.zExtent, new BoundingBox(new Vector3f(0.0f, 0.0f, 0.0f), 0.0f, 0.0f, 0.0f));
            }
        }
        return null;
    }

    @Override
    public BoundingVolume mergeLocal(BoundingVolume volume) {
        if (volume == null) {
            return this;
        }
        switch (volume.getType()) {
            case AABB: {
                BoundingBox vBox = (BoundingBox)volume;
                return this.merge(vBox.center, vBox.xExtent, vBox.yExtent, vBox.zExtent, this);
            }
        }
        return null;
    }

    private BoundingBox merge(Vector3f boxCenter, float boxX, float boxY, float boxZ, BoundingBox rVal) {
        TempVars vars = TempVars.get();
        assert (vars.lock());
        vars.vect1.x = this.center.x - this.xExtent;
        if (vars.vect1.x > boxCenter.x - boxX) {
            vars.vect1.x = boxCenter.x - boxX;
        }
        vars.vect1.y = this.center.y - this.yExtent;
        if (vars.vect1.y > boxCenter.y - boxY) {
            vars.vect1.y = boxCenter.y - boxY;
        }
        vars.vect1.z = this.center.z - this.zExtent;
        if (vars.vect1.z > boxCenter.z - boxZ) {
            vars.vect1.z = boxCenter.z - boxZ;
        }
        vars.vect2.x = this.center.x + this.xExtent;
        if (vars.vect2.x < boxCenter.x + boxX) {
            vars.vect2.x = boxCenter.x + boxX;
        }
        vars.vect2.y = this.center.y + this.yExtent;
        if (vars.vect2.y < boxCenter.y + boxY) {
            vars.vect2.y = boxCenter.y + boxY;
        }
        vars.vect2.z = this.center.z + this.zExtent;
        if (vars.vect2.z < boxCenter.z + boxZ) {
            vars.vect2.z = boxCenter.z + boxZ;
        }
        this.center.set(vars.vect2).addLocal(vars.vect1).multLocal(0.5f);
        this.xExtent = vars.vect2.x - this.center.x;
        this.yExtent = vars.vect2.y - this.center.y;
        this.zExtent = vars.vect2.z - this.center.z;
        assert (vars.unlock());
        return rVal;
    }

    @Override
    public BoundingBox clone(BoundingVolume store) {
        if (store != null && store.getType() == BoundingVolume.Type.AABB) {
            BoundingBox rVal = (BoundingBox)store;
            rVal.center.set(this.center);
            rVal.xExtent = this.xExtent;
            rVal.yExtent = this.yExtent;
            rVal.zExtent = this.zExtent;
            rVal.checkPlane = this.checkPlane;
            return rVal;
        }
        BoundingBox rVal = new BoundingBox(this.center.clone(), this.xExtent, this.yExtent, this.zExtent);
        return rVal;
    }

    public String toString() {
        return this.getClass().getSimpleName() + " [Center: " + this.center + "  xExtent: " + this.xExtent + "  yExtent: " + this.yExtent + "  zExtent: " + this.zExtent + "]";
    }

    @Override
    public boolean intersects(BoundingVolume bv) {
        return bv.intersectsBoundingBox(this);
    }

    @Override
    public boolean intersectsBoundingBox(BoundingBox bb) {
        assert (Vector3f.isValidVector(this.center) && Vector3f.isValidVector(bb.center));
        if (this.center.x + this.xExtent < bb.center.x - bb.xExtent || this.center.x - this.xExtent > bb.center.x + bb.xExtent) {
            return false;
        }
        if (this.center.y + this.yExtent < bb.center.y - bb.yExtent || this.center.y - this.yExtent > bb.center.y + bb.yExtent) {
            return false;
        }
        if (this.center.z + this.zExtent < bb.center.z - bb.zExtent || this.center.z - this.zExtent > bb.center.z + bb.zExtent) {
            return false;
        }
        return true;
    }

    @Override
    public boolean intersects(Ray ray) {
        assert (Vector3f.isValidVector(this.center));
        TempVars vars = TempVars.get();
        assert (vars.lock());
        Vector3f diff = ray.origin.subtract(this.getCenter(vars.vect2), vars.vect1);
        float[] fWdU = vars.fWdU;
        float[] fAWdU = vars.fAWdU;
        float[] fDdU = vars.fDdU;
        float[] fADdU = vars.fADdU;
        float[] fAWxDdU = vars.fAWxDdU;
        fWdU[0] = ray.getDirection().dot(Vector3f.UNIT_X);
        fAWdU[0] = FastMath.abs(fWdU[0]);
        fDdU[0] = diff.dot(Vector3f.UNIT_X);
        fADdU[0] = FastMath.abs(fDdU[0]);
        if (fADdU[0] > this.xExtent && (double)(fDdU[0] * fWdU[0]) >= 0.0) {
            assert (vars.unlock());
            return false;
        }
        fWdU[1] = ray.getDirection().dot(Vector3f.UNIT_Y);
        fAWdU[1] = FastMath.abs(fWdU[1]);
        fDdU[1] = diff.dot(Vector3f.UNIT_Y);
        fADdU[1] = FastMath.abs(fDdU[1]);
        if (fADdU[1] > this.yExtent && (double)(fDdU[1] * fWdU[1]) >= 0.0) {
            assert (vars.unlock());
            return false;
        }
        fWdU[2] = ray.getDirection().dot(Vector3f.UNIT_Z);
        fAWdU[2] = FastMath.abs(fWdU[2]);
        fDdU[2] = diff.dot(Vector3f.UNIT_Z);
        fADdU[2] = FastMath.abs(fDdU[2]);
        if (fADdU[2] > this.zExtent && (double)(fDdU[2] * fWdU[2]) >= 0.0) {
            assert (vars.unlock());
            return false;
        }
        Vector3f wCrossD = ray.getDirection().cross(diff, vars.vect2);
        fAWxDdU[0] = FastMath.abs(wCrossD.dot(Vector3f.UNIT_X));
        float rhs = this.yExtent * fAWdU[2] + this.zExtent * fAWdU[1];
        if (fAWxDdU[0] > rhs) {
            assert (vars.unlock());
            return false;
        }
        fAWxDdU[1] = FastMath.abs(wCrossD.dot(Vector3f.UNIT_Y));
        rhs = this.xExtent * fAWdU[2] + this.zExtent * fAWdU[0];
        if (fAWxDdU[1] > rhs) {
            assert (vars.unlock());
            return false;
        }
        fAWxDdU[2] = FastMath.abs(wCrossD.dot(Vector3f.UNIT_Z));
        rhs = this.xExtent * fAWdU[1] + this.yExtent * fAWdU[0];
        if (fAWxDdU[2] > rhs) {
            assert (vars.unlock());
            return false;
        }
        assert (vars.unlock());
        return true;
    }

    private int collideWithRay(Ray ray, CollisionResults results) {
        TempVars vars = TempVars.get();
        assert (vars.lock());
        Vector3f diff = vars.vect1.set(ray.origin).subtractLocal(this.center);
        Vector3f direction = vars.vect2.set(ray.direction);
        float[] t = new float[]{0.0f, Float.POSITIVE_INFINITY};
        float saveT0 = t[0];
        float saveT1 = t[1];
        boolean notEntirelyClipped = this.clip(direction.x, -diff.x - this.xExtent, t) && this.clip(-direction.x, diff.x - this.xExtent, t) && this.clip(direction.y, -diff.y - this.yExtent, t) && this.clip(-direction.y, diff.y - this.yExtent, t) && this.clip(direction.z, -diff.z - this.zExtent, t) && this.clip(-direction.z, diff.z - this.zExtent, t);
        assert (vars.unlock());
        if (notEntirelyClipped && (t[0] != saveT0 || t[1] != saveT1)) {
            if (t[1] > t[0]) {
                float[] distances = t;
                Vector3f[] points = new Vector3f[]{new Vector3f(ray.direction).multLocal(distances[0]).addLocal(ray.origin), new Vector3f(ray.direction).multLocal(distances[1]).addLocal(ray.origin)};
                CollisionResult result = new CollisionResult(points[0], distances[0]);
                results.addCollision(result);
                result = new CollisionResult(points[1], distances[1]);
                results.addCollision(result);
                return 2;
            }
            Vector3f point = new Vector3f(ray.direction).multLocal(t[0]).addLocal(ray.origin);
            CollisionResult result = new CollisionResult(point, t[0]);
            results.addCollision(result);
            return 1;
        }
        return 0;
    }

    @Override
    public int collideWith(Collidable other, CollisionResults results) {
        if (other instanceof Ray) {
            Ray ray = (Ray)other;
            return this.collideWithRay(ray, results);
        }
        if (other instanceof Triangle) {
            Triangle t = (Triangle)other;
            if (this.intersects(t.get1(), t.get2(), t.get3())) {
                CollisionResult r = new CollisionResult();
                results.addCollision(r);
                return 1;
            }
            return 0;
        }
        throw new UnsupportedCollisionException("With: " + other.getClass().getSimpleName());
    }

    public boolean intersects(Vector3f v1, Vector3f v2, Vector3f v3) {
        return Intersection.intersect(this, v1, v2, v3);
    }

    @Override
    public boolean contains(Vector3f point) {
        return FastMath.abs(this.center.x - point.x) < this.xExtent && FastMath.abs(this.center.y - point.y) < this.yExtent && FastMath.abs(this.center.z - point.z) < this.zExtent;
    }

    @Override
    public float distanceToEdge(Vector3f point) {
        float delta;
        Vector3f closest = point.subtract(this.center);
        float sqrDistance = 0.0f;
        if (closest.x < -this.xExtent) {
            delta = closest.x + this.xExtent;
            sqrDistance += delta * delta;
            closest.x = -this.xExtent;
        } else if (closest.x > this.xExtent) {
            delta = closest.x - this.xExtent;
            sqrDistance += delta * delta;
            closest.x = this.xExtent;
        }
        if (closest.y < -this.yExtent) {
            delta = closest.y + this.yExtent;
            sqrDistance += delta * delta;
            closest.y = -this.yExtent;
        } else if (closest.y > this.yExtent) {
            delta = closest.y - this.yExtent;
            sqrDistance += delta * delta;
            closest.y = this.yExtent;
        }
        if (closest.z < -this.zExtent) {
            delta = closest.z + this.zExtent;
            sqrDistance += delta * delta;
            closest.z = -this.zExtent;
        } else if (closest.z > this.zExtent) {
            delta = closest.z - this.zExtent;
            sqrDistance += delta * delta;
            closest.z = this.zExtent;
        }
        return FastMath.sqrt(sqrDistance);
    }

    private boolean clip(float denom, float numer, float[] t) {
        if (denom > 0.0f) {
            if (numer > denom * t[1]) {
                return false;
            }
            if (numer > denom * t[0]) {
                t[0] = numer / denom;
            }
            return true;
        }
        if (denom < 0.0f) {
            if (numer > denom * t[0]) {
                return false;
            }
            if (numer > denom * t[1]) {
                t[1] = numer / denom;
            }
            return true;
        }
        return (double)numer <= 0.0;
    }

    public Vector3f getExtent(Vector3f store) {
        if (store == null) {
            store = new Vector3f();
        }
        store.set(this.xExtent, this.yExtent, this.zExtent);
        return store;
    }

    public float getXExtent() {
        return this.xExtent;
    }

    public float getYExtent() {
        return this.yExtent;
    }

    public float getZExtent() {
        return this.zExtent;
    }

    public void setXExtent(float xExtent) {
        if (xExtent < 0.0f) {
            throw new IllegalArgumentException();
        }
        this.xExtent = xExtent;
    }

    public void setYExtent(float yExtent) {
        if (yExtent < 0.0f) {
            throw new IllegalArgumentException();
        }
        this.yExtent = yExtent;
    }

    public void setZExtent(float zExtent) {
        if (zExtent < 0.0f) {
            throw new IllegalArgumentException();
        }
        this.zExtent = zExtent;
    }

    public Vector3f getMin(Vector3f store) {
        if (store == null) {
            store = new Vector3f();
        }
        store.set(this.center).subtractLocal(this.xExtent, this.yExtent, this.zExtent);
        return store;
    }

    public Vector3f getMax(Vector3f store) {
        if (store == null) {
            store = new Vector3f();
        }
        store.set(this.center).addLocal(this.xExtent, this.yExtent, this.zExtent);
        return store;
    }

    public void setMinMax(Vector3f min, Vector3f max) {
        this.center.set(max).addLocal(min).multLocal(0.5f);
        this.xExtent = FastMath.abs(max.x - this.center.x);
        this.yExtent = FastMath.abs(max.y - this.center.y);
        this.zExtent = FastMath.abs(max.z - this.center.z);
    }

    @Override
    public float getVolume() {
        return 8.0f * this.xExtent * this.yExtent * this.zExtent;
    }
}

