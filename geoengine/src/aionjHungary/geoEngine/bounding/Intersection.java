/*
 * geoEngine 0.1 (package aionjHungary.geoEngine) - a cut-down copy of the jMonkeyEngine3 math,
 * scene-graph and collision code (BSD licence, jmonkeyengine.org) repackaged by "aionjHungary".
 * Source recovered from gameserver/lib/geoEngine-0.1.jar with CFR 0.152 and repaired so that it
 * recompiles to the jar's API and (modulo javac-version differences) bytecode; see ../../README.md.
 */
package aionjHungary.geoEngine.bounding;

import aionjHungary.geoEngine.bounding.BoundingBox;
import aionjHungary.geoEngine.math.FastMath;
import aionjHungary.geoEngine.math.Plane;
import aionjHungary.geoEngine.math.Vector3f;

public class Intersection {
    private static final void findMinMax(float x0, float x1, float x2, Vector3f minMax) {
        minMax.set(x0, x0, 0.0f);
        if (x1 < minMax.x) {
            minMax.setX(x1);
        }
        if (x1 > minMax.y) {
            minMax.setY(x1);
        }
        if (x2 < minMax.x) {
            minMax.setX(x2);
        }
        if (x2 > minMax.y) {
            minMax.setY(x2);
        }
    }

    public static final boolean intersect(BoundingBox bbox, Vector3f v1, Vector3f v2, Vector3f v3) {
        Vector3f tmp0 = new Vector3f();
        Vector3f tmp1 = new Vector3f();
        Vector3f tmp2 = new Vector3f();
        Vector3f e0 = new Vector3f();
        Vector3f e1 = new Vector3f();
        Vector3f e2 = new Vector3f();
        Vector3f center = bbox.getCenter();
        Vector3f extent = bbox.getExtent(null);
        v1.subtract(center, tmp0);
        v2.subtract(center, tmp1);
        v3.subtract(center, tmp2);
        tmp1.subtract(tmp0, e0);
        tmp2.subtract(tmp1, e1);
        tmp0.subtract(tmp2, e2);
        float fex = FastMath.abs(e0.x);
        float fey = FastMath.abs(e0.y);
        float fez = FastMath.abs(e0.z);
        float p0 = e0.z * tmp0.y - e0.y * tmp0.z;
        float p2 = e0.z * tmp2.y - e0.y * tmp2.z;
        float min = Math.min(p0, p2);
        float max = Math.max(p0, p2);
        float rad = fez * extent.y + fey * extent.z;
        if (min > rad || max < -rad) {
            return false;
        }
        p0 = -e0.z * tmp0.x + e0.x * tmp0.z;
        p2 = -e0.z * tmp2.x + e0.x * tmp2.z;
        min = Math.min(p0, p2);
        max = Math.max(p0, p2);
        rad = fez * extent.x + fex * extent.z;
        if (min > rad || max < -rad) {
            return false;
        }
        float p1 = e0.y * tmp1.x - e0.x * tmp1.y;
        p2 = e0.y * tmp2.x - e0.x * tmp2.y;
        min = Math.min(p1, p2);
        max = Math.max(p1, p2);
        rad = fey * extent.x + fex * extent.y;
        if (min > rad || max < -rad) {
            return false;
        }
        fex = FastMath.abs(e1.x);
        fey = FastMath.abs(e1.y);
        fez = FastMath.abs(e1.z);
        p0 = e1.z * tmp0.y - e1.y * tmp0.z;
        p2 = e1.z * tmp2.y - e1.y * tmp2.z;
        min = Math.min(p0, p2);
        max = Math.max(p0, p2);
        rad = fez * extent.y + fey * extent.z;
        if (min > rad || max < -rad) {
            return false;
        }
        p0 = -e1.z * tmp0.x + e1.x * tmp0.z;
        p2 = -e1.z * tmp2.x + e1.x * tmp2.z;
        min = Math.min(p0, p2);
        max = Math.max(p0, p2);
        rad = fez * extent.x + fex * extent.z;
        if (min > rad || max < -rad) {
            return false;
        }
        p0 = e1.y * tmp0.x - e1.x * tmp0.y;
        p1 = e1.y * tmp1.x - e1.x * tmp1.y;
        min = Math.min(p0, p1);
        max = Math.max(p0, p1);
        rad = fey * extent.x + fex * extent.y;
        if (min > rad || max < -rad) {
            return false;
        }
        fex = FastMath.abs(e2.x);
        fey = FastMath.abs(e2.y);
        fez = FastMath.abs(e2.z);
        p0 = e2.z * tmp0.y - e2.y * tmp0.z;
        p1 = e2.z * tmp1.y - e2.y * tmp1.z;
        min = Math.min(p0, p1);
        max = Math.max(p0, p1);
        rad = fez * extent.y + fey * extent.z;
        if (min > rad || max < -rad) {
            return false;
        }
        p0 = -e2.z * tmp0.x + e2.x * tmp0.z;
        p1 = -e2.z * tmp1.x + e2.x * tmp1.z;
        min = Math.min(p0, p1);
        max = Math.max(p0, p1);
        rad = fez * extent.x + fex * extent.y;
        if (min > rad || max < -rad) {
            return false;
        }
        p1 = e2.y * tmp1.x - e2.x * tmp1.y;
        p2 = e2.y * tmp2.x - e2.x * tmp2.y;
        min = Math.min(p1, p2);
        max = Math.max(p1, p2);
        rad = fey * extent.x + fex * extent.y;
        if (min > rad || max < -rad) {
            return false;
        }
        Vector3f minMax = new Vector3f();
        Intersection.findMinMax(tmp0.x, tmp1.x, tmp2.x, minMax);
        if (minMax.x > extent.x || minMax.y < -extent.x) {
            return false;
        }
        Intersection.findMinMax(tmp0.y, tmp1.y, tmp2.y, minMax);
        if (minMax.x > extent.y || minMax.y < -extent.y) {
            return false;
        }
        Intersection.findMinMax(tmp0.z, tmp1.z, tmp2.z, minMax);
        if (minMax.x > extent.z || minMax.y < -extent.z) {
            return false;
        }
        Plane p = new Plane();
        p.setPlanePoints(v1, v2, v3);
        return bbox.whichSide(p) != Plane.Side.Negative;
    }
}

