/*
 * geoEngine 0.1 (package aionjHungary.geoEngine) - a cut-down copy of the jMonkeyEngine3 math,
 * scene-graph and collision code (BSD licence, jmonkeyengine.org) repackaged by "aionjHungary".
 * Source recovered from gameserver/lib/geoEngine-0.1.jar with CFR 0.152 and repaired so that it
 * recompiles to the jar's API and (modulo javac-version differences) bytecode; see ../../README.md.
 */
package aionjHungary.geoEngine.collision.bih;

import aionjHungary.geoEngine.bounding.BoundingBox;
import aionjHungary.geoEngine.bounding.BoundingVolume;
import aionjHungary.geoEngine.collision.Collidable;
import aionjHungary.geoEngine.collision.CollisionResults;
import aionjHungary.geoEngine.collision.UnsupportedCollisionException;
import aionjHungary.geoEngine.collision.bih.BIHNode;
import aionjHungary.geoEngine.collision.bih.TriangleAxisComparator;
import aionjHungary.geoEngine.math.Matrix4f;
import aionjHungary.geoEngine.math.Ray;
import aionjHungary.geoEngine.math.Vector3f;
import aionjHungary.geoEngine.scene.CollisionData;
import aionjHungary.geoEngine.scene.Mesh;
import aionjHungary.geoEngine.scene.VertexBuffer;
import aionjHungary.geoEngine.scene.mesh.IndexBuffer;
import aionjHungary.geoEngine.utils.TempVars;
import java.nio.FloatBuffer;

public class BIHTree
implements CollisionData {
    public static final int MAX_TREE_DEPTH = 100;
    public static final int MAX_TRIS_PER_NODE = 21;
    private BIHNode root;
    private int maxTrisPerNode;
    private int numTris;
    private float[] pointData;
    private int[] triIndices;
    private transient float[] bihSwapTmp;
    private static final TriangleAxisComparator[] comparators = new TriangleAxisComparator[3];

    private void initTriList(FloatBuffer vb, IndexBuffer ib) {
        this.pointData = new float[this.numTris * 3 * 3];
        int p = 0;
        for (int i = 0; i < this.numTris * 3; i += 3) {
            int vert = ib.get(i) * 3;
            this.pointData[p++] = vb.get(vert++);
            this.pointData[p++] = vb.get(vert++);
            this.pointData[p++] = vb.get(vert);
            vert = ib.get(i + 1) * 3;
            this.pointData[p++] = vb.get(vert++);
            this.pointData[p++] = vb.get(vert++);
            this.pointData[p++] = vb.get(vert);
            vert = ib.get(i + 2) * 3;
            this.pointData[p++] = vb.get(vert++);
            this.pointData[p++] = vb.get(vert++);
            this.pointData[p++] = vb.get(vert);
        }
        this.triIndices = new int[this.numTris];
        for (int i = 0; i < this.numTris; ++i) {
            this.triIndices[i] = i;
        }
    }

    public BIHTree(Mesh mesh, int maxTrisPerNode) {
        this.maxTrisPerNode = maxTrisPerNode;
        if (maxTrisPerNode < 1 || mesh == null) {
            throw new IllegalArgumentException();
        }
        this.bihSwapTmp = new float[9];
        FloatBuffer vb = (FloatBuffer)mesh.getBuffer(VertexBuffer.Type.Position).getData();
        IndexBuffer ib = mesh.getIndexBuffer();
        this.numTris = ib.size() / 3;
        this.initTriList(vb, ib);
    }

    public BIHTree(Mesh mesh) {
        this(mesh, 21);
    }

    public BIHTree() {
    }

    public void construct() {
        BoundingBox sceneBbox = this.createBox(0, this.numTris - 1);
        this.root = this.createNode(0, this.numTris - 1, sceneBbox, 0);
    }

    private BoundingBox createBox(int l, int r) {
        TempVars vars = TempVars.get();
        assert (vars.lock());
        Vector3f min = vars.vect1.set(new Vector3f(Float.POSITIVE_INFINITY, Float.POSITIVE_INFINITY, Float.POSITIVE_INFINITY));
        Vector3f max = vars.vect2.set(new Vector3f(Float.NEGATIVE_INFINITY, Float.NEGATIVE_INFINITY, Float.NEGATIVE_INFINITY));
        Vector3f v1 = vars.vect3;
        Vector3f v2 = vars.vect4;
        Vector3f v3 = vars.vect5;
        for (int i = l; i <= r; ++i) {
            this.getTriangle(i, v1, v2, v3);
            BoundingBox.checkMinMax(min, max, v1);
            BoundingBox.checkMinMax(min, max, v2);
            BoundingBox.checkMinMax(min, max, v3);
        }
        BoundingBox bbox = new BoundingBox(min, max);
        assert (vars.unlock());
        return bbox;
    }

    int getTriangleIndex(int triIndex) {
        return this.triIndices[triIndex];
    }

    private int sortTriangles(int l, int r, float split, int axis) {
        int pivot = l;
        int j = r;
        TempVars vars = TempVars.get();
        assert (vars.lock());
        Vector3f v1 = vars.vect1;
        Vector3f v2 = vars.vect2;
        Vector3f v3 = vars.vect3;
        while (pivot <= j) {
            this.getTriangle(pivot, v1, v2, v3);
            v1.addLocal(v2).addLocal(v3).multLocal(0.33333334f);
            if (v1.get(axis) > split) {
                this.swapTriangles(pivot, j);
                --j;
                continue;
            }
            ++pivot;
        }
        assert (vars.unlock());
        pivot = pivot == l && j < pivot ? j : pivot;
        return pivot;
    }

    private void setMinMax(BoundingBox bbox, boolean doMin, int axis, float value) {
        Vector3f min = bbox.getMin(null);
        Vector3f max = bbox.getMax(null);
        if (doMin) {
            min.set(axis, value);
        } else {
            max.set(axis, value);
        }
        bbox.setMinMax(min, max);
    }

    private float getMinMax(BoundingBox bbox, boolean doMin, int axis) {
        if (doMin) {
            return bbox.getMin(null).get(axis);
        }
        return bbox.getMax(null).get(axis);
    }

    private BIHNode createNode(int l, int r, BoundingBox nodeBbox, int depth) {
        if (r - l < this.maxTrisPerNode || depth > 100) {
            return new BIHNode(l, r);
        }
        BoundingBox currentBox = this.createBox(l, r);
        Vector3f exteriorExt = nodeBbox.getExtent(null);
        Vector3f interiorExt = currentBox.getExtent(null);
        exteriorExt.subtractLocal(interiorExt);
        int axis = 0;
        if (exteriorExt.x > exteriorExt.y) {
            if (exteriorExt.x > exteriorExt.z) {
                axis = 0;
            } else {
                axis = 2;
            }
        } else if (exteriorExt.y > exteriorExt.z) {
            axis = 1;
        } else {
            axis = 2;
        }
        if (exteriorExt.equals(Vector3f.ZERO)) {
            axis = 0;
        }
        float split = currentBox.getCenter().get(axis);
        int pivot = this.sortTriangles(l, r, split, axis);
        if (pivot == l || pivot == r) {
            pivot = (r + l) / 2;
        }
        if (pivot < l) {
            BoundingBox rbbox = new BoundingBox(currentBox);
            this.setMinMax(rbbox, true, axis, split);
            return this.createNode(l, r, rbbox, depth + 1);
        }
        if (pivot > r) {
            BoundingBox lbbox = new BoundingBox(currentBox);
            this.setMinMax(lbbox, false, axis, split);
            return this.createNode(l, r, lbbox, depth + 1);
        }
        BIHNode node = new BIHNode(axis);
        BoundingBox lbbox = new BoundingBox(currentBox);
        this.setMinMax(lbbox, false, axis, split);
        node.setLeftPlane(this.getMinMax(this.createBox(l, Math.max(l, pivot - 1)), false, axis));
        node.setLeftChild(this.createNode(l, Math.max(l, pivot - 1), lbbox, depth + 1));
        BoundingBox rbbox = new BoundingBox(currentBox);
        this.setMinMax(rbbox, true, axis, split);
        node.setRightPlane(this.getMinMax(this.createBox(pivot, r), true, axis));
        node.setRightChild(this.createNode(pivot, r, rbbox, depth + 1));
        return node;
    }

    public void getTriangle(int index, Vector3f v1, Vector3f v2, Vector3f v3) {
        int pointIndex = index * 9;
        v1.x = this.pointData[pointIndex++];
        v1.y = this.pointData[pointIndex++];
        v1.z = this.pointData[pointIndex++];
        v2.x = this.pointData[pointIndex++];
        v2.y = this.pointData[pointIndex++];
        v2.z = this.pointData[pointIndex++];
        v3.x = this.pointData[pointIndex++];
        v3.y = this.pointData[pointIndex++];
        v3.z = this.pointData[pointIndex++];
    }

    public void swapTriangles(int index1, int index2) {
        int p1 = index1 * 9;
        int p2 = index2 * 9;
        System.arraycopy(this.pointData, p1, this.bihSwapTmp, 0, 9);
        System.arraycopy(this.pointData, p2, this.pointData, p1, 9);
        System.arraycopy(this.bihSwapTmp, 0, this.pointData, p2, 9);
        int tmp2 = this.triIndices[index1];
        this.triIndices[index1] = this.triIndices[index2];
        this.triIndices[index2] = tmp2;
    }

    private int collideWithRay(Ray r, Matrix4f worldMatrix, BoundingVolume worldBound, CollisionResults results) {
        CollisionResults boundResults = new CollisionResults();
        worldBound.collideWith(r, boundResults);
        if (boundResults.size() > 0) {
            float tMin = boundResults.getClosestCollision().getDistance();
            float tMax = boundResults.getFarthestCollision().getDistance();
            if (tMax <= 0.0f) {
                tMax = Float.POSITIVE_INFINITY;
            } else if (tMin == tMax) {
                tMin = 0.0f;
            }
            if (tMin <= 0.0f) {
                tMin = 0.0f;
            }
            if (r.getLimit() < Float.POSITIVE_INFINITY) {
                tMax = Math.min(tMax, r.getLimit());
            }
            return this.root.intersectWhere(r, worldMatrix, this, tMin, tMax, results);
        }
        return 0;
    }

    private int collideWithBoundingVolume(BoundingVolume bv, Matrix4f worldMatrix, CollisionResults results) {
        BoundingBox bbox;
        if (bv instanceof BoundingBox) {
            bbox = new BoundingBox((BoundingBox)bv);
        } else {
            throw new UnsupportedCollisionException();
        }
        bbox.transform(worldMatrix.invert(), (BoundingVolume)bbox);
        return this.root.intersectWhere(bv, bbox, worldMatrix, this, results);
    }

    @Override
    public int collideWith(Collidable other, Matrix4f worldMatrix, BoundingVolume worldBound, CollisionResults results) {
        if (other instanceof Ray) {
            Ray ray = (Ray)other;
            return this.collideWithRay(ray, worldMatrix, worldBound, results);
        }
        if (other instanceof BoundingVolume) {
            BoundingVolume bv = (BoundingVolume)other;
            return this.collideWithBoundingVolume(bv, worldMatrix, results);
        }
        throw new UnsupportedCollisionException();
    }

    static {
        BIHTree.comparators[0] = new TriangleAxisComparator(0);
        BIHTree.comparators[1] = new TriangleAxisComparator(1);
        BIHTree.comparators[2] = new TriangleAxisComparator(2);
    }
}

