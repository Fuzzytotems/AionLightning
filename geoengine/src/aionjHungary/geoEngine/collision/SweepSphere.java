/*
 * geoEngine 0.1 (package aionjHungary.geoEngine) - a cut-down copy of the jMonkeyEngine3 math,
 * scene-graph and collision code (BSD licence, jmonkeyengine.org) repackaged by "aionjHungary".
 * Source recovered from gameserver/lib/geoEngine-0.1.jar with CFR 0.152 and repaired so that it
 * recompiles to the jar's API and (modulo javac-version differences) bytecode; see ../../README.md.
 */
package aionjHungary.geoEngine.collision;

import aionjHungary.geoEngine.collision.Collidable;
import aionjHungary.geoEngine.collision.CollisionResult;
import aionjHungary.geoEngine.collision.CollisionResults;
import aionjHungary.geoEngine.collision.UnsupportedCollisionException;
import aionjHungary.geoEngine.math.AbstractTriangle;
import aionjHungary.geoEngine.math.FastMath;
import aionjHungary.geoEngine.math.Plane;
import aionjHungary.geoEngine.math.Triangle;
import aionjHungary.geoEngine.math.Vector3f;

public class SweepSphere
implements Collidable {
    private Vector3f velocity = new Vector3f();
    private Vector3f center = new Vector3f();
    private Vector3f dimension = new Vector3f();
    private Vector3f invDim = new Vector3f();
    private final Triangle scaledTri = new Triangle();
    private final Plane triPlane = new Plane();
    private final Vector3f temp1 = new Vector3f();
    private final Vector3f temp2 = new Vector3f();
    private final Vector3f temp3 = new Vector3f();
    private final Vector3f sVelocity = new Vector3f();
    private final Vector3f sCenter = new Vector3f();

    public Vector3f getCenter() {
        return this.center;
    }

    public void setCenter(Vector3f center) {
        this.center.set(center);
    }

    public Vector3f getDimension() {
        return this.dimension;
    }

    public void setDimension(Vector3f dimension) {
        this.dimension.set(dimension);
        this.invDim.set(1.0f, 1.0f, 1.0f).divideLocal(dimension);
    }

    public void setDimension(float x, float y, float z) {
        this.dimension.set(x, y, z);
        this.invDim.set(1.0f, 1.0f, 1.0f).divideLocal(this.dimension);
    }

    public void setDimension(float dim) {
        this.dimension.set(dim, dim, dim);
        this.invDim.set(1.0f, 1.0f, 1.0f).divideLocal(this.dimension);
    }

    public Vector3f getVelocity() {
        return this.velocity;
    }

    public void setVelocity(Vector3f velocity) {
        this.velocity.set(velocity);
    }

    private boolean pointsOnSameSide(Vector3f p1, Vector3f p2, Vector3f line1, Vector3f line2) {
        this.temp1.set(line2).subtractLocal(line1);
        this.temp3.set(this.temp1);
        this.temp2.set(p1).subtractLocal(line1);
        this.temp1.crossLocal(this.temp2);
        this.temp2.set(p2).subtractLocal(line1);
        this.temp3.crossLocal(this.temp2);
        return this.temp1.dot(this.temp3) >= 0.0f;
    }

    private boolean isPointInTriangle(Vector3f point, AbstractTriangle tri) {
        if (this.pointsOnSameSide(point, tri.get1(), tri.get2(), tri.get3()) && this.pointsOnSameSide(point, tri.get2(), tri.get1(), tri.get3()) && this.pointsOnSameSide(point, tri.get3(), tri.get1(), tri.get2())) {
            return true;
        }
        return false;
    }

    private static float getLowestRoot(float a, float b, float c, float maxR) {
        float determinant = b * b - 4.0f * a * c;
        if (determinant < 0.0f) {
            return Float.NaN;
        }
        float sqrtd = FastMath.sqrt(determinant);
        float r1 = (-b - sqrtd) / (2.0f * a);
        float r2 = (-b + sqrtd) / (2.0f * a);
        if (r1 > r2) {
            float temp = r2;
            r2 = r1;
            r1 = temp;
        }
        if (r1 > 0.0f && r1 < maxR) {
            return r1;
        }
        if (r2 > 0.0f && r2 < maxR) {
            return r2;
        }
        return Float.NaN;
    }

    private float collideWithVertex(Vector3f sCenter, Vector3f sVelocity, float velocitySquared, Vector3f vertex, float t) {
        this.temp1.set(sCenter).subtractLocal(vertex);
        float a = velocitySquared;
        float b = 2.0f * sVelocity.dot(this.temp1);
        float c = this.temp1.negateLocal().lengthSquared() - 1.0f;
        float newT = SweepSphere.getLowestRoot(a, b, c, t);
        return newT;
    }

    private float collideWithSegment(Vector3f sCenter, Vector3f sVelocity, float velocitySquared, Vector3f l1, Vector3f l2, float t, Vector3f store) {
        Vector3f edge = this.temp1.set(l2).subtractLocal(l1);
        Vector3f base = this.temp2.set(l1).subtractLocal(sCenter);
        float edgeSquared = edge.lengthSquared();
        float baseSquared = base.lengthSquared();
        float EdotV = edge.dot(sVelocity);
        float EdotB = edge.dot(base);
        float a = edgeSquared * -velocitySquared + EdotV * EdotV;
        float b = edgeSquared * 2.0f * sVelocity.dot(base) - 2.0f * EdotV * EdotB;
        float c = edgeSquared * (1.0f - baseSquared) + EdotB * EdotB;
        float newT = SweepSphere.getLowestRoot(a, b, c, t);
        if (!Float.isNaN(newT)) {
            float f = (EdotV * newT - EdotB) / edgeSquared;
            if (f >= 0.0f && f < 1.0f) {
                store.scaleAdd(f, edge, l1);
                return newT;
            }
        }
        return Float.NaN;
    }

    private CollisionResult collideWithTriangle(AbstractTriangle tri) {
        this.scaledTri.get1().set(tri.get1()).multLocal(this.invDim);
        this.scaledTri.get2().set(tri.get2()).multLocal(this.invDim);
        this.scaledTri.get3().set(tri.get3()).multLocal(this.invDim);
        this.velocity.mult(this.invDim, this.sVelocity);
        this.center.mult(this.invDim, this.sCenter);
        this.triPlane.setPlanePoints(this.scaledTri);
        float normalDotVelocity = this.triPlane.getNormal().dot(this.sVelocity);
        if (normalDotVelocity > 0.0f) {
            return null;
        }
        boolean embedded = false;
        float signedDistanceToPlane = this.triPlane.pseudoDistance(this.sCenter);
        if (normalDotVelocity == 0.0f) {
            if (FastMath.abs(signedDistanceToPlane) >= 1.0f) {
                return null;
            }
            float t0 = 0.0f;
            float t1 = 1.0f;
            embedded = true;
            System.out.println("EMBEDDED");
            return null;
        }
        float t0 = (-1.0f - signedDistanceToPlane) / normalDotVelocity;
        float t1 = (1.0f - signedDistanceToPlane) / normalDotVelocity;
        if (t0 > t1) {
            float tf = t1;
            t1 = t0;
            t0 = tf;
        }
        if (t0 > 1.0f || t1 < 0.0f) {
            return null;
        }
        t0 = Math.max(t0, 0.0f);
        t1 = Math.min(t1, 1.0f);
        boolean foundCollision = false;
        float minT = 1.0f;
        Vector3f contactPoint = new Vector3f();
        Vector3f contactNormal = new Vector3f();
        contactPoint.set(this.sVelocity);
        contactPoint.multLocal(t0);
        contactPoint.addLocal(this.sCenter);
        contactPoint.subtractLocal(this.triPlane.getNormal());
        if (this.isPointInTriangle(contactPoint, this.scaledTri) && !embedded) {
            foundCollision = true;
            minT = t0;
            contactPoint.multLocal(this.dimension);
            contactNormal.set(this.velocity).multLocal(t0);
            contactNormal.addLocal(this.center);
            contactNormal.subtractLocal(contactPoint).normalizeLocal();
            CollisionResult result = new CollisionResult();
            result.setContactPoint(contactPoint);
            result.setContactNormal(contactNormal);
            result.setDistance(minT * this.velocity.length());
            return result;
        }
        float velocitySquared = this.sVelocity.lengthSquared();
        Vector3f v1 = this.scaledTri.get1();
        Vector3f v2 = this.scaledTri.get2();
        Vector3f v3 = this.scaledTri.get3();
        float newT = this.collideWithVertex(this.sCenter, this.sVelocity, velocitySquared, v1, minT);
        if (!Float.isNaN(newT)) {
            minT = newT;
            contactPoint.set(v1);
            foundCollision = true;
        }
        newT = this.collideWithVertex(this.sCenter, this.sVelocity, velocitySquared, v2, minT);
        if (!Float.isNaN(newT)) {
            minT = newT;
            contactPoint.set(v2);
            foundCollision = true;
        }
        newT = this.collideWithVertex(this.sCenter, this.sVelocity, velocitySquared, v3, minT);
        if (!Float.isNaN(newT)) {
            minT = newT;
            contactPoint.set(v3);
            foundCollision = true;
        }
        newT = this.collideWithSegment(this.sCenter, this.sVelocity, velocitySquared, v1, v2, minT, contactPoint);
        if (!Float.isNaN(newT)) {
            minT = newT;
            foundCollision = true;
        }
        newT = this.collideWithSegment(this.sCenter, this.sVelocity, velocitySquared, v2, v3, minT, contactPoint);
        if (!Float.isNaN(newT)) {
            minT = newT;
            foundCollision = true;
        }
        newT = this.collideWithSegment(this.sCenter, this.sVelocity, velocitySquared, v3, v1, minT, contactPoint);
        if (!Float.isNaN(newT)) {
            minT = newT;
            foundCollision = true;
        }
        if (foundCollision) {
            contactPoint.multLocal(this.dimension);
            contactNormal.set(this.velocity).multLocal(t0);
            contactNormal.addLocal(this.center);
            contactNormal.subtractLocal(contactPoint).normalizeLocal();
            CollisionResult result = new CollisionResult();
            result.setContactPoint(contactPoint);
            result.setContactNormal(contactNormal);
            result.setDistance(minT * this.velocity.length());
            return result;
        }
        return null;
    }

    public CollisionResult collideWithSweepSphere(SweepSphere other) {
        this.temp1.set(this.velocity).subtractLocal(other.velocity);
        this.temp2.set(this.center).subtractLocal(other.center);
        this.temp3.set(this.dimension).addLocal(other.dimension);
        float a = this.temp1.lengthSquared();
        float b = 2.0f * this.temp1.dot(this.temp2);
        float c = this.temp2.lengthSquared() - this.temp3.getX() * this.temp3.getX();
        float t = SweepSphere.getLowestRoot(a, b, c, 1.0f);
        if (Float.isNaN(t)) {
            return null;
        }
        CollisionResult result = new CollisionResult();
        result.setDistance(this.velocity.length() * t);
        this.temp1.set(this.velocity).multLocal(t).addLocal(this.center);
        this.temp2.set(other.velocity).multLocal(t).addLocal(other.center);
        this.temp3.set(this.temp2).subtractLocal(this.temp1);
        this.temp2.set(this.temp3).normalizeLocal();
        result.setContactNormal(new Vector3f(this.temp2));
        this.temp3.set(this.temp2).multLocal(this.dimension).addLocal(this.temp1);
        result.setContactPoint(new Vector3f(this.temp3));
        return result;
    }

    public static void main(String[] args) {
        SweepSphere ss = new SweepSphere();
        ss.setCenter(Vector3f.ZERO);
        ss.setDimension(1.0f);
        ss.setVelocity(new Vector3f(10.0f, 10.0f, 10.0f));
        SweepSphere ss2 = new SweepSphere();
        ss2.setCenter(new Vector3f(5.0f, 5.0f, 5.0f));
        ss2.setDimension(1.0f);
        ss2.setVelocity(new Vector3f(-10.0f, -10.0f, -10.0f));
        CollisionResults cr = new CollisionResults();
        ss.collideWith(ss2, cr);
        if (cr.size() > 0) {
            CollisionResult c = cr.getClosestCollision();
            System.out.println("D = " + c.getDistance());
            System.out.println("P = " + c.getContactPoint());
            System.out.println("N = " + c.getContactNormal());
        }
    }

    @Override
    public int collideWith(Collidable other, CollisionResults results) throws UnsupportedCollisionException {
        if (other instanceof AbstractTriangle) {
            AbstractTriangle tri = (AbstractTriangle)other;
            CollisionResult result = this.collideWithTriangle(tri);
            if (result != null) {
                results.addCollision(result);
                return 1;
            }
            return 0;
        }
        if (other instanceof SweepSphere) {
            SweepSphere sph = (SweepSphere)other;
            CollisionResult result = this.collideWithSweepSphere(sph);
            if (result != null) {
                results.addCollision(result);
                return 1;
            }
            return 0;
        }
        throw new UnsupportedCollisionException();
    }
}

