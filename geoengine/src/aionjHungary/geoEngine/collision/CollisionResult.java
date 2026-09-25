/*
 * geoEngine 0.1 (package aionjHungary.geoEngine) - a cut-down copy of the jMonkeyEngine3 math,
 * scene-graph and collision code (BSD licence, jmonkeyengine.org) repackaged by "aionjHungary".
 * Source recovered from gameserver/lib/geoEngine-0.1.jar with CFR 0.152 and repaired so that it
 * recompiles to the jar's API and (modulo javac-version differences) bytecode; see ../../README.md.
 */
package aionjHungary.geoEngine.collision;

import aionjHungary.geoEngine.math.Vector3f;

public class CollisionResult
implements Comparable<CollisionResult> {
    private Vector3f contactPoint;
    private Vector3f contactNormal;
    private float distance;
    private int triangleIndex;

    public CollisionResult(Vector3f contactPoint, float distance) {
        this.contactPoint = contactPoint;
        this.distance = distance;
    }

    public CollisionResult() {
    }

    public void setContactNormal(Vector3f norm) {
        this.contactNormal = norm;
    }

    public void setContactPoint(Vector3f point) {
        this.contactPoint = point;
    }

    public void setDistance(float dist) {
        this.distance = dist;
    }

    public void setTriangleIndex(int index) {
        this.triangleIndex = index;
    }

    @Override
    public int compareTo(CollisionResult other) {
        if (this.distance < other.distance) {
            return -1;
        }
        if (this.distance > other.distance) {
            return 1;
        }
        return 0;
    }

    public Vector3f getContactPoint() {
        return this.contactPoint;
    }

    @Deprecated
    public Vector3f getWorldContactPoint() {
        return this.contactPoint;
    }

    public Vector3f getContactNormal() {
        return this.contactNormal;
    }

    public float getDistance() {
        return this.distance;
    }

    public int getTriangleIndex() {
        return this.triangleIndex;
    }
}

