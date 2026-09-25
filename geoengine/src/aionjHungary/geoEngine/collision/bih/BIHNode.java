/*
 * geoEngine 0.1 (package aionjHungary.geoEngine) - a cut-down copy of the jMonkeyEngine3 math,
 * scene-graph and collision code (BSD licence, jmonkeyengine.org) repackaged by "aionjHungary".
 * Source recovered from gameserver/lib/geoEngine-0.1.jar with CFR 0.152 and repaired so that it
 * recompiles to the jar's API and (modulo javac-version differences) bytecode; see ../../README.md.
 */
package aionjHungary.geoEngine.collision.bih;

import aionjHungary.geoEngine.bounding.BoundingBox;
import aionjHungary.geoEngine.collision.Collidable;
import aionjHungary.geoEngine.collision.CollisionResult;
import aionjHungary.geoEngine.collision.CollisionResults;
import aionjHungary.geoEngine.collision.bih.BIHTree;
import aionjHungary.geoEngine.math.Matrix4f;
import aionjHungary.geoEngine.math.Ray;
import aionjHungary.geoEngine.math.Triangle;
import aionjHungary.geoEngine.math.Vector3f;
import aionjHungary.geoEngine.utils.TempVars;
import java.util.ArrayList;

public final class BIHNode {
    private int leftIndex;
    private int rightIndex;
    private BIHNode left;
    private BIHNode right;
    private float leftPlane;
    private float rightPlane;
    private int axis;

    public BIHNode(int l, int r) {
        this.leftIndex = l;
        this.rightIndex = r;
        this.axis = 3;
    }

    public BIHNode(int axis) {
        this.axis = axis;
    }

    public BIHNode() {
    }

    public BIHNode getLeftChild() {
        return this.left;
    }

    public void setLeftChild(BIHNode left) {
        this.left = left;
    }

    public float getLeftPlane() {
        return this.leftPlane;
    }

    public void setLeftPlane(float leftPlane) {
        this.leftPlane = leftPlane;
    }

    public BIHNode getRightChild() {
        return this.right;
    }

    public void setRightChild(BIHNode right) {
        this.right = right;
    }

    public float getRightPlane() {
        return this.rightPlane;
    }

    public void setRightPlane(float rightPlane) {
        this.rightPlane = rightPlane;
    }

    public final int intersectWhere(Collidable col, BoundingBox box, Matrix4f worldMatrix, BIHTree tree, CollisionResults results) {
        ArrayList<BIHStackData> stack = TempVars.get().bihStack;
        stack.clear();
        float[] minExts = new float[]{box.getCenter().x - box.getXExtent(), box.getCenter().y - box.getYExtent(), box.getCenter().z - box.getZExtent()};
        float[] maxExts = new float[]{box.getCenter().x + box.getXExtent(), box.getCenter().y + box.getYExtent(), box.getCenter().z + box.getZExtent()};
        stack.add(new BIHStackData(this, 0.0f, 0.0f));
        Triangle t = new Triangle();
        int cols = 0;
        stackloop: while (stack.size() > 0) {
            BIHNode node = stack.remove(stack.size() - 1).node;
            while (node.axis != 3) {
                int a = node.axis;
                float maxExt = maxExts[a];
                float minExt = minExts[a];
                if (node.leftPlane < node.rightPlane && minExt > node.leftPlane && maxExt < node.rightPlane) continue stackloop;
                if (maxExt < node.rightPlane) {
                    node = node.left;
                    continue;
                }
                if (minExt > node.leftPlane) {
                    node = node.right;
                    continue;
                }
                stack.add(new BIHStackData(node.right, 0.0f, 0.0f));
                node = node.left;
            }
            for (int i = node.leftIndex; i <= node.rightIndex; ++i) {
                tree.getTriangle(i, t.get1(), t.get2(), t.get3());
                if (worldMatrix != null) {
                    worldMatrix.mult(t.get1(), t.get1());
                    worldMatrix.mult(t.get2(), t.get2());
                    worldMatrix.mult(t.get3(), t.get3());
                }
                int added = col.collideWith(t, results);
                if (added > 0) {
                    int index = tree.getTriangleIndex(i);
                    int start = results.size() - added;
                    for (int j = start; j < results.size(); ++j) {
                        CollisionResult cr = results.getCollisionDirect(j);
                        cr.setTriangleIndex(index);
                    }
                    cols += added;
                }
            }
        }
        return cols;
    }

    public final int intersectBrute(Ray r, Matrix4f worldMatrix, BIHTree tree, float sceneMin, float sceneMax, CollisionResults results) {
        float tHit = Float.POSITIVE_INFINITY;
        Vector3f v1 = new Vector3f();
        Vector3f v2 = new Vector3f();
        Vector3f v3 = new Vector3f();
        int cols = 0;
        ArrayList<BIHStackData> stack = TempVars.get().bihStack;
        stack.clear();
        stack.add(new BIHStackData(this, 0.0f, 0.0f));
        while (stack.size() > 0) {
            BIHStackData data = stack.remove(stack.size() - 1);
            BIHNode node = data.node;
            while (node.axis != 3) {
                BIHNode nearNode = node.left;
                BIHNode farNode = node.right;
                stack.add(new BIHStackData(farNode, 0.0f, 0.0f));
                node = nearNode;
            }
            for (int i = node.leftIndex; i <= node.rightIndex; ++i) {
                tree.getTriangle(i, v1, v2, v3);
                if (worldMatrix != null) {
                    worldMatrix.mult(v1, v1);
                    worldMatrix.mult(v2, v2);
                    worldMatrix.mult(v3, v3);
                }
                float t = r.intersects(v1, v2, v3);
                if (t < tHit) {
                    tHit = t;
                    Vector3f contactPoint = new Vector3f(r.direction).multLocal(tHit).addLocal(r.origin);
                    CollisionResult cr = new CollisionResult(contactPoint, tHit);
                    cr.setTriangleIndex(tree.getTriangleIndex(i));
                    results.addCollision(cr);
                    ++cols;
                }
            }
        }
        return cols;
    }

    public final int intersectWhere(Ray r, Matrix4f worldMatrix, BIHTree tree, float sceneMin, float sceneMax, CollisionResults results) {
        ArrayList<BIHStackData> stack = TempVars.get().bihStack;
        stack.clear();
        Vector3f o = r.getOrigin().clone();
        Vector3f d = r.getDirection().clone();
        Matrix4f inv = worldMatrix.invert();
        inv.mult(r.getOrigin(), r.getOrigin());
        inv.multNormal(r.getDirection(), r.getDirection());
        float[] origins = new float[]{r.getOrigin().x, r.getOrigin().y, r.getOrigin().z};
        float[] invDirections = new float[]{1.0f / r.getDirection().x, 1.0f / r.getDirection().y, 1.0f / r.getDirection().z};
        r.getDirection().normalizeLocal();
        Vector3f v1 = new Vector3f();
        Vector3f v2 = new Vector3f();
        Vector3f v3 = new Vector3f();
        int cols = 0;
        stack.add(new BIHStackData(this, sceneMin, sceneMax));
        stackloop: while (stack.size() > 0) {
            BIHStackData data = stack.remove(stack.size() - 1);
            BIHNode node = data.node;
            float tMin = data.min;
            float tMax = data.max;
            if (tMax < tMin) continue;
            while (node.axis != 3) {
                int a = node.axis;
                float origin = origins[a];
                float invDirection = invDirections[a];
                float tNearSplit = (node.leftPlane - origin) * invDirection;
                float tFarSplit = (node.rightPlane - origin) * invDirection;
                BIHNode nearNode = node.left;
                BIHNode farNode = node.right;
                if (invDirection < 0.0f) {
                    float tmpSplit = tNearSplit;
                    tNearSplit = tFarSplit;
                    tFarSplit = tmpSplit;
                    BIHNode tmpNode = nearNode;
                    nearNode = farNode;
                    farNode = tmpNode;
                }
                if (tMin > tNearSplit && tMax < tFarSplit) continue stackloop;
                if (tMin > tNearSplit) {
                    tMin = Math.max(tMin, tFarSplit);
                    node = farNode;
                    continue;
                }
                if (tMax < tFarSplit) {
                    tMax = Math.min(tMax, tNearSplit);
                    node = nearNode;
                    continue;
                }
                stack.add(new BIHStackData(farNode, Math.max(tMin, tFarSplit), tMax));
                tMax = Math.min(tMax, tNearSplit);
                node = nearNode;
            }
            for (int i = node.leftIndex; i <= node.rightIndex; ++i) {
                tree.getTriangle(i, v1, v2, v3);
                float t = r.intersects(v1, v2, v3);
                if (Float.isInfinite(t)) continue;
                if (worldMatrix != null) {
                    worldMatrix.mult(v1, v1);
                    worldMatrix.mult(v2, v2);
                    worldMatrix.mult(v3, v3);
                    float t_world = new Ray(o, d).intersects(v1, v2, v3);
                    t = t_world;
                }
                Vector3f contactNormal = Triangle.computeTriangleNormal(v1, v2, v3, null);
                Vector3f contactPoint = new Vector3f(d).multLocal(t).addLocal(o);
                float worldSpaceDist = o.distance(contactPoint);
                if (worldSpaceDist > r.limit) continue;
                CollisionResult cr = new CollisionResult(contactPoint, worldSpaceDist);
                cr.setContactNormal(contactNormal);
                cr.setTriangleIndex(tree.getTriangleIndex(i));
                results.addCollision(cr);
                ++cols;
            }
        }
        r.setOrigin(o);
        r.setDirection(d);
        return cols;
    }

    public static final class BIHStackData {
        private final BIHNode node;
        private final float min;
        private final float max;

        BIHStackData(BIHNode node, float min, float max) {
            this.node = node;
            this.min = min;
            this.max = max;
        }
    }
}

