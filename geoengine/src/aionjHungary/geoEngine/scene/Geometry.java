/*
 * geoEngine 0.1 (package aionjHungary.geoEngine) - a cut-down copy of the jMonkeyEngine3 math,
 * scene-graph and collision code (BSD licence, jmonkeyengine.org) repackaged by "aionjHungary".
 * Source recovered from gameserver/lib/geoEngine-0.1.jar with CFR 0.152 and repaired so that it
 * recompiles to the jar's API and (modulo javac-version differences) bytecode; see ../../README.md.
 */
package aionjHungary.geoEngine.scene;

import aionjHungary.geoEngine.bounding.BoundingVolume;
import aionjHungary.geoEngine.collision.Collidable;
import aionjHungary.geoEngine.collision.CollisionResults;
import aionjHungary.geoEngine.math.Matrix3f;
import aionjHungary.geoEngine.math.Matrix4f;
import aionjHungary.geoEngine.math.Ray;
import aionjHungary.geoEngine.math.Vector3f;
import aionjHungary.geoEngine.scene.Mesh;
import aionjHungary.geoEngine.scene.Spatial;

public class Geometry
extends Spatial {
    protected Mesh mesh;
    protected Matrix4f cachedWorldMat = new Matrix4f();

    public Geometry() {
    }

    public Geometry(String name) {
        super(name);
    }

    public Geometry(String name, Mesh mesh) {
        this(name);
        if (mesh == null) {
            throw new NullPointerException();
        }
        this.mesh = mesh;
    }

    @Override
    public int getVertexCount() {
        return this.mesh.getVertexCount();
    }

    @Override
    public int getTriangleCount() {
        return this.mesh.getTriangleCount();
    }

    public void setMesh(Mesh mesh) {
        this.mesh = mesh;
    }

    public Mesh getMesh() {
        return this.mesh;
    }

    public BoundingVolume getModelBound() {
        return this.mesh.getBound();
    }

    @Override
    public void updateModelBound() {
        this.mesh.updateBound();
        this.worldBound = this.getModelBound().transform(this.cachedWorldMat, this.worldBound);
    }

    public Matrix4f getWorldMatrix() {
        return this.cachedWorldMat;
    }

    @Override
    public void setModelBound(BoundingVolume modelBound) {
        this.mesh.setBound(modelBound);
    }

    @Override
    public int collideWith(Collidable other, CollisionResults results) {
        if (other instanceof Ray && !this.worldBound.intersects((Ray)other)) {
            return 0;
        }
        int added = this.mesh.collideWith(other, this.cachedWorldMat, this.worldBound, results);
        return added;
    }

    @Override
    public void setTransform(Matrix3f rotation, Vector3f loc, float scale) {
        this.cachedWorldMat.loadIdentity();
        this.cachedWorldMat.setRotationMatrix(rotation);
        this.cachedWorldMat.scale(scale);
        this.cachedWorldMat.setTranslation(loc);
    }
}

