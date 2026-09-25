/*
 * geoEngine 0.1 (package aionjHungary.geoEngine) - a cut-down copy of the jMonkeyEngine3 math,
 * scene-graph and collision code (BSD licence, jmonkeyengine.org) repackaged by "aionjHungary".
 * Source recovered from gameserver/lib/geoEngine-0.1.jar with CFR 0.152 and repaired so that it
 * recompiles to the jar's API and (modulo javac-version differences) bytecode; see ../../README.md.
 */
package aionjHungary.geoEngine.models;

import aionjHungary.geoEngine.bounding.BoundingBox;
import aionjHungary.geoEngine.collision.CollisionResult;
import aionjHungary.geoEngine.collision.CollisionResults;
import aionjHungary.geoEngine.math.Ray;
import aionjHungary.geoEngine.math.Vector3f;
import aionjHungary.geoEngine.scene.Node;
import aionjHungary.geoEngine.scene.Spatial;
import java.util.ArrayList;
import java.util.Iterator;
import java.util.List;

public class GeoMap
extends Node {
    private short[] terrainData;
    private List<BoundingBox> tmpBox = new ArrayList<BoundingBox>();

    public GeoMap(String name, int worldSize) {
        for (int x = 0; x < worldSize; x += 256) {
            for (int y = 0; y < worldSize; y += 256) {
                Node geoNode = new Node("");
                this.tmpBox.add(new BoundingBox(new Vector3f(x, y, 0.0f), new Vector3f(x + 256, y + 256, 4000.0f)));
                super.attachChild(geoNode);
            }
        }
    }

    @Override
    public int attachChild(Spatial child) {
        int i = 0;
        for (Spatial spatial : this.getChildren()) {
            if (this.tmpBox.get(i).intersects(child.getWorldBound())) {
                ((Node)spatial).attachChild(child);
            }
            ++i;
        }
        return 0;
    }

    public void setTerrainData(short[] terrainData) {
        this.terrainData = terrainData;
    }

    public float getZ(float x, float y, float z) {
        CollisionResults results = new CollisionResults();
        float newZ = 0.0f;
        if (this.terrainData.length == 1) {
            newZ = (float)this.terrainData[0] / 32.0f;
        } else {
            newZ = this.getZ(x, y);
        }
        if (newZ < z + 2.0f) {
            CollisionResult result = new CollisionResult();
            Vector3f contactPoint = new Vector3f(x, y, newZ);
            result.setContactPoint(contactPoint);
            result.setDistance(z - newZ);
            results.addCollision(result);
        }
        Vector3f pos = new Vector3f(x, y, z + 2.0f);
        Vector3f dir = new Vector3f(x, y, 0.0f);
        Float limit = Float.valueOf(pos.distance(dir));
        dir.subtractLocal(pos).normalizeLocal();
        Ray r = new Ray(pos, dir);
        r.setLimit(limit.floatValue());
        this.collideWith(r, results);
        if (results.size() == 0) {
            return newZ;
        }
        return results.getClosestCollision().getContactPoint().z;
    }

    private float getZ(float x, float y) {
        if (this.terrainData.length == 1) {
            return (float)this.terrainData[0] / 32.0f;
        }
        y /= 2.0f;
        x /= 2.0f;
        int size = (int)Math.sqrt(this.terrainData.length);
        int xInt = (int)x;
        int yInt = (int)y;
        float p1 = this.terrainData[yInt + xInt * size];
        float p2 = this.terrainData[yInt + 1 + xInt * size];
        float p3 = this.terrainData[yInt + (xInt + 1) * size];
        float p4 = this.terrainData[yInt + 1 + (xInt + 1) * size];
        float p13 = p1 + (p1 - p3) * (x % 1.0f);
        float p24 = p2 + (p4 - p2) * (x % 1.0f);
        float p1234 = p13 + (p24 - p13) * (y % 1.0f);
        return p1234 / 32.0f;
    }

    public boolean canSee(float x, float y, float z, float targetX, float targetY, float targetZ) {
        targetZ += 1.0f;
        z += 1.0f;
        float x2 = x - targetX;
        float y2 = y - targetY;
        float z2 = z - targetZ;
        float distance = (float)Math.sqrt(x2 * x2 + y2 * y2);
        if (distance > 80.0f) {
            return false;
        }
        int intD = (int)Math.abs(distance);
        boolean terrain = this.getZ(x, y) < z;
        for (float s = 2.0f; s < (float)intD; s += 2.0f) {
            float tempX = targetX + x2 * s / distance;
            float tempY = targetY + y2 * s / distance;
            float tempZ = targetZ + z2 * s / distance;
            if (terrain) {
                if (this.getZ(tempX, tempY) > tempZ) {
                    return false;
                }
            } else if (this.getZ(tempX, tempY) < tempZ) {
                return false;
            }
        }
        Vector3f pos = new Vector3f(x, y, z);
        Vector3f dir = new Vector3f(targetX, targetY, targetZ);
        Float limit = Float.valueOf(pos.distance(dir));
        dir.subtractLocal(pos).normalizeLocal();
        Ray r = new Ray(pos, dir);
        r.setLimit(limit.floatValue());
        CollisionResults results = new CollisionResults();
        this.collideWith(r, results);
        return results.size() == 0;
    }

    @Override
    public void updateModelBound() {
        if (this.getChildren() != null) {
            Iterator<Spatial> i = this.getChildren().iterator();
            while (i.hasNext()) {
                Spatial s = i.next();
                if (!(s instanceof Node) || !((Node)s).getChildren().isEmpty()) continue;
                i.remove();
            }
            this.tmpBox = null;
        }
        super.updateModelBound();
    }
}

