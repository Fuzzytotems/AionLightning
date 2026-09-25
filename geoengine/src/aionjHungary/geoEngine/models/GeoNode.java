/*
 * geoEngine 0.1 (package aionjHungary.geoEngine) - a cut-down copy of the jMonkeyEngine3 math,
 * scene-graph and collision code (BSD licence, jmonkeyengine.org) repackaged by "aionjHungary".
 * Source recovered from gameserver/lib/geoEngine-0.1.jar with CFR 0.152 and repaired so that it
 * recompiles to the jar's API and (modulo javac-version differences) bytecode; see ../../README.md.
 */
package aionjHungary.geoEngine.models;

import aionjHungary.geoEngine.bounding.BoundingVolume;
import aionjHungary.geoEngine.collision.Collidable;
import aionjHungary.geoEngine.collision.CollisionResults;
import aionjHungary.geoEngine.collision.UnsupportedCollisionException;
import aionjHungary.geoEngine.math.Matrix3f;
import aionjHungary.geoEngine.math.Ray;
import aionjHungary.geoEngine.math.Vector3f;
import aionjHungary.geoEngine.scene.Spatial;
import java.util.ArrayList;
import java.util.List;

public class GeoNode
extends Spatial {
    private List<Spatial> childrens = new ArrayList<Spatial>();

    public GeoNode(String name) {
        super(name);
    }

    public void addGeoNode(Spatial geoNode) {
        this.childrens.add(geoNode);
    }

    public List<Spatial> getChildrens() {
        return this.childrens;
    }

    @Override
    public int collideWith(Collidable other, CollisionResults results) throws UnsupportedCollisionException {
        if (!this.worldBound.intersects((Ray)other)) {
            return 0;
        }
        int count = 0;
        for (Spatial children : this.childrens) {
            count += children.collideWith(other, results);
        }
        return 0;
    }

    @Override
    public void updateModelBound() {
        if (this.childrens != null) {
            for (int i = 0, max = this.childrens.size(); i < max; ++i) {
                this.childrens.get(i).updateModelBound();
            }
        }
        this.updateWorldBound();
    }

    protected void updateWorldBound() {
        /*
         * REPAIR NOTE: GeoNode.class in geoEngine-0.1.jar is stale (compiled against an older
         * Spatial). Its first instruction is invokespecial Spatial.updateWorldBound()V, a method that
         * does not exist in this library, so on the JVM this method always fails with
         * NoSuchMethodError at that call and the rest of the original body never runs. The
         * explicit throw reproduces that behaviour; the unreachable remainder is kept for reference:
         *
         *     super.updateWorldBound();
         *     BoundingVolume resultBound = null;
         *     for (int i = 0, cSize = this.childrens.size(); i < cSize; ++i) {
         *         Spatial child = this.childrens.get(i);
         *         if (resultBound != null) {
         *             resultBound.mergeLocal(child.getWorldBound());
         *         } else if (child.getWorldBound() != null) {
         *             resultBound = child.getWorldBound().clone(this.worldBound);
         *         }
         *     }
         *     this.worldBound = resultBound;
         */
        throw new NoSuchMethodError("aionjHungary.geoEngine.scene.Spatial.updateWorldBound()V");
    }

    @Override
    public void setModelBound(BoundingVolume modelBound) {
        this.worldBound = modelBound;
    }

    /*
     * REPAIR NOTE: the three methods below are NOT present in geoEngine-0.1.jar. The stale
     * GeoNode.class does not implement the abstract Spatial methods getVertexCount(),
     * getTriangleCount() and setTransform(Matrix3f, Vector3f, float), so invoking any of them on a
     * GeoNode throws AbstractMethodError on the JVM. Java source cannot express a concrete class
     * with unimplemented abstract methods, so these three members are the only API difference from
     * the jar; each reproduces the JVM behaviour explicitly.
     */
    @Override
    public int getVertexCount() {
        throw new AbstractMethodError("aionjHungary.geoEngine.models.GeoNode.getVertexCount()I");
    }

    @Override
    public int getTriangleCount() {
        throw new AbstractMethodError("aionjHungary.geoEngine.models.GeoNode.getTriangleCount()I");
    }

    @Override
    public void setTransform(Matrix3f rotation, Vector3f loc, float scale) {
        throw new AbstractMethodError("aionjHungary.geoEngine.models.GeoNode.setTransform(LaionjHungary/geoEngine/math/Matrix3f;LaionjHungary/geoEngine/math/Vector3f;F)V");
    }
}
