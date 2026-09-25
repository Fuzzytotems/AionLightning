/*
 * geoEngine 0.1 (package aionjHungary.geoEngine) - a cut-down copy of the jMonkeyEngine3 math,
 * scene-graph and collision code (BSD licence, jmonkeyengine.org) repackaged by "aionjHungary".
 * Source recovered from gameserver/lib/geoEngine-0.1.jar with CFR 0.152 and repaired so that it
 * recompiles to the jar's API and (modulo javac-version differences) bytecode; see ../../README.md.
 */
package aionjHungary.geoEngine.bounding;

import aionjHungary.geoEngine.bounding.BoundingBox;
import aionjHungary.geoEngine.bounding.BoundingVolume;
import aionjHungary.geoEngine.collision.Collidable;
import aionjHungary.geoEngine.collision.CollisionResult;
import aionjHungary.geoEngine.collision.CollisionResults;
import aionjHungary.geoEngine.collision.UnsupportedCollisionException;
import aionjHungary.geoEngine.math.FastMath;
import aionjHungary.geoEngine.math.Matrix4f;
import aionjHungary.geoEngine.math.Plane;
import aionjHungary.geoEngine.math.Ray;
import aionjHungary.geoEngine.math.Transform;
import aionjHungary.geoEngine.math.Triangle;
import aionjHungary.geoEngine.math.Vector3f;
import aionjHungary.geoEngine.utils.BufferUtils;
import aionjHungary.geoEngine.utils.TempVars;
import java.nio.FloatBuffer;
import java.util.logging.Logger;

public class BoundingSphere
extends BoundingVolume {
    private static final Logger logger = Logger.getLogger(BoundingSphere.class.getName());
    float radius;
    private static final float RADIUS_EPSILON = 1.00001f;

    public BoundingSphere() {
    }

    public BoundingSphere(float r, Vector3f c) {
        this.center.set(c);
        this.radius = r;
    }

    @Override
    public BoundingVolume.Type getType() {
        return BoundingVolume.Type.Sphere;
    }

    public float getRadius() {
        return this.radius;
    }

    public void setRadius(float radius) {
        this.radius = radius;
    }

    @Override
    public void computeFromPoints(FloatBuffer points) {
        this.calcWelzl(points);
    }

    public void computeFromTris(Triangle[] tris, int start, int end) {
        if (end - start <= 0) {
            return;
        }
        Vector3f[] vertList = new Vector3f[(end - start) * 3];
        int count = 0;
        for (int i = start; i < end; ++i) {
            vertList[count++] = tris[i].get(0);
            vertList[count++] = tris[i].get(1);
            vertList[count++] = tris[i].get(2);
        }
        this.averagePoints(vertList);
    }

    public void calcWelzl(FloatBuffer points) {
        if (this.center == null) {
            this.center = new Vector3f();
        }
        FloatBuffer buf = BufferUtils.createFloatBuffer(points.limit());
        points.rewind();
        buf.put(points);
        buf.flip();
        this.recurseMini(buf, buf.limit() / 3, 0, 0);
    }

    private void recurseMini(FloatBuffer points, int p, int b, int ap) {
        TempVars vars = TempVars.get();
        assert (vars.lock());
        Vector3f tempA = vars.vect1;
        Vector3f tempB = vars.vect2;
        Vector3f tempC = vars.vect3;
        Vector3f tempD = vars.vect4;
        switch (b) {
            case 0: {
                this.radius = 0.0f;
                this.center.set(0.0f, 0.0f, 0.0f);
                break;
            }
            case 1: {
                this.radius = -1.001358E-5f;
                BufferUtils.populateFromBuffer(this.center, points, ap - 1);
                break;
            }
            case 2: {
                BufferUtils.populateFromBuffer(tempA, points, ap - 1);
                BufferUtils.populateFromBuffer(tempB, points, ap - 2);
                this.setSphere(tempA, tempB);
                break;
            }
            case 3: {
                BufferUtils.populateFromBuffer(tempA, points, ap - 1);
                BufferUtils.populateFromBuffer(tempB, points, ap - 2);
                BufferUtils.populateFromBuffer(tempC, points, ap - 3);
                this.setSphere(tempA, tempB, tempC);
                break;
            }
            case 4: {
                BufferUtils.populateFromBuffer(tempA, points, ap - 1);
                BufferUtils.populateFromBuffer(tempB, points, ap - 2);
                BufferUtils.populateFromBuffer(tempC, points, ap - 3);
                BufferUtils.populateFromBuffer(tempD, points, ap - 4);
                this.setSphere(tempA, tempB, tempC, tempD);
                assert (vars.unlock());
                return;
            }
        }
        for (int i = 0; i < p; ++i) {
            BufferUtils.populateFromBuffer(tempA, points, i + ap);
            if (!(tempA.distanceSquared(this.center) - this.radius * this.radius > 1.001358E-5f)) continue;
            for (int j = i; j > 0; --j) {
                BufferUtils.populateFromBuffer(tempB, points, j + ap);
                BufferUtils.populateFromBuffer(tempC, points, j - 1 + ap);
                BufferUtils.setInBuffer(tempC, points, j + ap);
                BufferUtils.setInBuffer(tempB, points, j - 1 + ap);
            }
            assert (vars.unlock());
            this.recurseMini(points, i, b + 1, ap + 1);
            assert (vars.lock());
        }
        assert (vars.unlock());
    }

    private void setSphere(Vector3f O, Vector3f A, Vector3f B, Vector3f C) {
        Vector3f a = A.subtract(O);
        Vector3f b = B.subtract(O);
        Vector3f c = C.subtract(O);
        float Denominator = 2.0f * (a.x * (b.y * c.z - c.y * b.z) - b.x * (a.y * c.z - c.y * a.z) + c.x * (a.y * b.z - b.y * a.z));
        if (Denominator == 0.0f) {
            this.center.set(0.0f, 0.0f, 0.0f);
            this.radius = 0.0f;
        } else {
            Vector3f o = a.cross(b).multLocal(c.lengthSquared()).addLocal(c.cross(a).multLocal(b.lengthSquared())).addLocal(b.cross(c).multLocal(a.lengthSquared())).divideLocal(Denominator);
            this.radius = o.length() * 1.00001f;
            O.add(o, this.center);
        }
    }

    private void setSphere(Vector3f O, Vector3f A, Vector3f B) {
        Vector3f a = A.subtract(O);
        Vector3f b = B.subtract(O);
        Vector3f acrossB = a.cross(b);
        float Denominator = 2.0f * acrossB.dot(acrossB);
        if (Denominator == 0.0f) {
            this.center.set(0.0f, 0.0f, 0.0f);
            this.radius = 0.0f;
        } else {
            Vector3f o = acrossB.cross(a).multLocal(b.lengthSquared()).addLocal(b.cross(acrossB).multLocal(a.lengthSquared())).divideLocal(Denominator);
            this.radius = o.length() * 1.00001f;
            O.add(o, this.center);
        }
    }

    private void setSphere(Vector3f O, Vector3f A) {
        this.radius = FastMath.sqrt(((A.x - O.x) * (A.x - O.x) + (A.y - O.y) * (A.y - O.y) + (A.z - O.z) * (A.z - O.z)) / 4.0f) + 1.00001f - 1.0f;
        this.center.interpolate(O, A, 0.5f);
    }

    public void averagePoints(Vector3f[] points) {
        logger.info("Bounding Sphere calculated using average points.");
        this.center = points[0];
        for (int i = 1; i < points.length; ++i) {
            this.center.addLocal(points[i]);
        }
        float quantity = 1.0f / (float)points.length;
        this.center.multLocal(quantity);
        float maxRadiusSqr = 0.0f;
        for (int i = 0; i < points.length; ++i) {
            Vector3f diff = points[i].subtract(this.center);
            float radiusSqr = diff.lengthSquared();
            if (!(radiusSqr > maxRadiusSqr)) continue;
            maxRadiusSqr = radiusSqr;
        }
        this.radius = (float)Math.sqrt(maxRadiusSqr) + 1.00001f - 1.0f;
    }

    @Override
    public BoundingVolume transform(Transform trans, BoundingVolume store) {
        BoundingSphere sphere;
        if (store == null || store.getType() != BoundingVolume.Type.Sphere) {
            sphere = new BoundingSphere(1.0f, new Vector3f(0.0f, 0.0f, 0.0f));
        } else {
            sphere = (BoundingSphere)store;
        }
        this.center.mult(trans.getScale(), sphere.center);
        trans.getRotation().mult(sphere.center, sphere.center);
        sphere.center.addLocal(trans.getTranslation());
        sphere.radius = FastMath.abs(this.getMaxAxis(trans.getScale()) * this.radius) + 1.00001f - 1.0f;
        return sphere;
    }

    @Override
    public BoundingVolume transform(Matrix4f trans, BoundingVolume store) {
        BoundingSphere sphere;
        if (store == null || store.getType() != BoundingVolume.Type.Sphere) {
            sphere = new BoundingSphere(1.0f, new Vector3f(0.0f, 0.0f, 0.0f));
        } else {
            sphere = (BoundingSphere)store;
        }
        trans.mult(this.center, sphere.center);
        Vector3f axes = new Vector3f(1.0f, 1.0f, 1.0f);
        trans.mult(axes, axes);
        float ax = this.getMaxAxis(axes);
        sphere.radius = FastMath.abs(ax * this.radius) + 1.00001f - 1.0f;
        return sphere;
    }

    private float getMaxAxis(Vector3f scale) {
        float x = FastMath.abs(scale.x);
        float y = FastMath.abs(scale.y);
        float z = FastMath.abs(scale.z);
        if (x >= y) {
            if (x >= z) {
                return x;
            }
            return z;
        }
        if (y >= z) {
            return y;
        }
        return z;
    }

    @Override
    public Plane.Side whichSide(Plane plane) {
        float distance = plane.pseudoDistance(this.center);
        if (distance <= -this.radius) {
            return Plane.Side.Negative;
        }
        if (distance >= this.radius) {
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
            case Sphere: {
                BoundingSphere sphere = (BoundingSphere)volume;
                float temp_radius = sphere.getRadius();
                Vector3f temp_center = sphere.center;
                BoundingSphere rVal = new BoundingSphere();
                return this.merge(temp_radius, temp_center, rVal);
            }
            case AABB: {
                BoundingBox box = (BoundingBox)volume;
                Vector3f radVect = new Vector3f(box.xExtent, box.yExtent, box.zExtent);
                Vector3f temp_center = box.center;
                BoundingSphere rVal = new BoundingSphere();
                return this.merge(radVect.length(), temp_center, rVal);
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
            case Sphere: {
                BoundingSphere sphere = (BoundingSphere)volume;
                float temp_radius = sphere.getRadius();
                Vector3f temp_center = sphere.center;
                return this.merge(temp_radius, temp_center, this);
            }
            case AABB: {
                BoundingBox box = (BoundingBox)volume;
                assert (TempVars.get().lock());
                Vector3f radVect = TempVars.get().vect1;
                radVect.set(box.xExtent, box.yExtent, box.zExtent);
                Vector3f temp_center = box.center;
                float len = radVect.length();
                assert (TempVars.get().unlock());
                return this.merge(len, temp_center, this);
            }
        }
        return null;
    }

    private BoundingVolume merge(float temp_radius, Vector3f temp_center, BoundingSphere rVal) {
        TempVars vars = TempVars.get();
        assert (vars.lock());
        Vector3f diff = temp_center.subtract(this.center, vars.vect1);
        float lengthSquared = diff.lengthSquared();
        float radiusDiff = temp_radius - this.radius;
        float fRDiffSqr = radiusDiff * radiusDiff;
        if (fRDiffSqr >= lengthSquared) {
            if (radiusDiff <= 0.0f) {
                assert (vars.unlock());
                return this;
            }
            Vector3f rCenter = rVal.center;
            if (rCenter == null) {
                rVal.setCenter(rCenter = new Vector3f());
            }
            rCenter.set(temp_center);
            rVal.setRadius(temp_radius);
            assert (vars.unlock());
            return rVal;
        }
        float length = (float)Math.sqrt(lengthSquared);
        Vector3f rCenter = rVal.center;
        if (rCenter == null) {
            rVal.setCenter(rCenter = new Vector3f());
        }
        if (length > 1.00001f) {
            float coeff = (length + radiusDiff) / (2.0f * length);
            rCenter.set(this.center.addLocal(diff.multLocal(coeff)));
        } else {
            rCenter.set(this.center);
        }
        rVal.setRadius(0.5f * (length + this.radius + temp_radius));
        assert (vars.unlock());
        return rVal;
    }

    @Override
    public BoundingVolume clone(BoundingVolume store) {
        if (store != null && store.getType() == BoundingVolume.Type.Sphere) {
            BoundingSphere rVal = (BoundingSphere)store;
            if (null == rVal.center) {
                rVal.center = new Vector3f();
            }
            rVal.center.set(this.center);
            rVal.radius = this.radius;
            rVal.checkPlane = this.checkPlane;
            return rVal;
        }
        return new BoundingSphere(this.radius, this.center != null ? this.center.clone() : null);
    }

    public String toString() {
        return this.getClass().getSimpleName() + " [Radius: " + this.radius + " Center: " + this.center + "]";
    }

    @Override
    public boolean intersects(BoundingVolume bv) {
        /*
         * REPAIR NOTE: BoundingSphere.class in geoEngine-0.1.jar is stale. This method's bytecode is
         * 'return bv.intersectsSphere(this)' (invokevirtual BoundingVolume.intersectsSphere), but
         * this library's BoundingVolume has no intersectsSphere method, so on the JVM the call
         * always fails with NoSuchMethodError (link-time resolution precedes the null check on bv).
         * The explicit throw reproduces that behaviour.
         */
        throw new NoSuchMethodError("aionjHungary.geoEngine.bounding.BoundingVolume.intersectsSphere(LaionjHungary/geoEngine/bounding/BoundingSphere;)Z");
    }

    public boolean intersectsSphere(BoundingSphere bs) {
        assert (Vector3f.isValidVector(this.center) && Vector3f.isValidVector(bs.center));
        TempVars vars = TempVars.get();
        assert (vars.lock());
        Vector3f diff = this.center.subtract(bs.center, vars.vect1);
        float rsum = this.getRadius() + bs.getRadius();
        boolean eq = diff.dot(diff) <= rsum * rsum;
        assert (vars.unlock());
        return eq;
    }

    @Override
    public boolean intersectsBoundingBox(BoundingBox bb) {
        assert (Vector3f.isValidVector(this.center) && Vector3f.isValidVector(bb.center));
        if (FastMath.abs(bb.center.x - this.center.x) < this.getRadius() + bb.xExtent && FastMath.abs(bb.center.y - this.center.y) < this.getRadius() + bb.yExtent && FastMath.abs(bb.center.z - this.center.z) < this.getRadius() + bb.zExtent) {
            return true;
        }
        return false;
    }

    @Override
    public boolean intersects(Ray ray) {
        assert (Vector3f.isValidVector(this.center));
        TempVars vars = TempVars.get();
        assert (vars.lock());
        Vector3f diff = vars.vect1.set(ray.getOrigin()).subtractLocal(this.center);
        float radiusSquared = this.getRadius() * this.getRadius();
        float a = diff.dot(diff) - radiusSquared;
        if ((double)a <= 0.0) {
            return true;
        }
        float b = ray.getDirection().dot(diff);
        assert (vars.unlock());
        if ((double)b >= 0.0) {
            return false;
        }
        return b * b >= a;
    }

    public int collideWithRay(Ray ray, CollisionResults results) {
        TempVars vars = TempVars.get();
        assert (vars.lock());
        Vector3f diff = vars.vect1.set(ray.getOrigin()).subtractLocal(this.center);
        float a = diff.dot(diff) - this.getRadius() * this.getRadius();
        float a1;
        float discr;
        float root;
        if ((double)a <= 0.0) {
            a1 = ray.direction.dot(diff);
            discr = a1 * a1 - a;
            root = FastMath.sqrt(discr);
            float distance = root - a1;
            Vector3f point = new Vector3f(ray.direction).multLocal(distance).addLocal(ray.origin);
            CollisionResult result = new CollisionResult(point, distance);
            results.addCollision(result);
            return 1;
        }
        a1 = ray.direction.dot(diff);
        assert (vars.unlock());
        if ((double)a1 >= 0.0) {
            return 0;
        }
        discr = a1 * a1 - a;
        if ((double)discr < 0.0) {
            return 0;
        }
        if (discr >= 1.0E-4f) {
            root = FastMath.sqrt(discr);
            float dist = -a1 - root;
            Vector3f point = new Vector3f(ray.direction).multLocal(dist).addLocal(ray.origin);
            results.addCollision(new CollisionResult(point, dist));
            dist = -a1 + root;
            point = new Vector3f(ray.direction).multLocal(dist).addLocal(ray.origin);
            results.addCollision(new CollisionResult(point, dist));
            return 2;
        }
        float dist = -a1;
        Vector3f point = new Vector3f(ray.direction).multLocal(dist).addLocal(ray.origin);
        results.addCollision(new CollisionResult(point, dist));
        return 1;
    }

    @Override
    public int collideWith(Collidable other, CollisionResults results) {
        if (other instanceof Ray) {
            Ray ray = (Ray)other;
            return this.collideWithRay(ray, results);
        }
        throw new UnsupportedCollisionException();
    }

    @Override
    public boolean contains(Vector3f point) {
        return this.center.distanceSquared(point) < this.getRadius() * this.getRadius();
    }

    @Override
    public float distanceToEdge(Vector3f point) {
        return this.center.distance(point) - this.radius;
    }

    @Override
    public float getVolume() {
        return 4.1887903f * this.radius * this.radius * this.radius;
    }
}

