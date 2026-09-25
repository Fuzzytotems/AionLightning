/*
 * geoEngine 0.1 (package aionjHungary.geoEngine) - a cut-down copy of the jMonkeyEngine3 math,
 * scene-graph and collision code (BSD licence, jmonkeyengine.org) repackaged by "aionjHungary".
 * Source recovered from gameserver/lib/geoEngine-0.1.jar with CFR 0.152 and repaired so that it
 * recompiles to the jar's API and (modulo javac-version differences) bytecode; see ../../README.md.
 */
package aionjHungary.geoEngine;

import com.jme3.bounding.BoundingBox;
import com.jme3.bounding.BoundingVolume;
import com.jme3.math.Matrix3f;
import com.jme3.math.Vector3f;
import com.jme3.scene.Geometry;
import com.jme3.scene.Mesh;
import com.jme3.scene.Node;
import com.jme3.scene.Spatial;
import java.io.File;
import java.io.RandomAccessFile;
import java.nio.ByteOrder;
import java.nio.MappedByteBuffer;
import java.nio.channels.FileChannel;
import java.util.List;
import java.util.Map;

public class ModelList {
    public static Node load(int worldId, Map<String, List<Mesh>> meshs) {
        File Geo = new File("data/geo/" + worldId + ".geo");
        int size;
        FileChannel roChannel = null;
        Node worldNode = new Node(String.valueOf(worldId));
        try {
            roChannel = new RandomAccessFile(Geo, "r").getChannel();
            size = (int)roChannel.size();
            MappedByteBuffer geo = roChannel.map(FileChannel.MapMode.READ_ONLY, 0L, size).load();
            geo.order(ByteOrder.LITTLE_ENDIAN);
            int modelCount = geo.getInt();
            for (int cc = 0; cc < modelCount; ++cc) {
                int nameLenght = geo.getShort();
                byte[] nameByte = new byte[nameLenght];
                geo.get(nameByte);
                String modelName = new String(nameByte);
                Vector3f loc = new Vector3f(geo.getFloat(), geo.getFloat(), geo.getFloat());
                float[][] tmp = new float[3][3];
                for (int r = 0; r < 3; ++r) {
                    for (int c = 0; c < 3; ++c) {
                        tmp[c][r] = geo.getFloat();
                    }
                }
                Matrix3f matrix = new Matrix3f();
                matrix.set(tmp);
                List<Mesh> meshss = meshs.get(modelName.toLowerCase());
                Node node = new Node(modelName);
                for (Mesh m : meshss) {
                    Geometry geom = new Geometry(modelName, m.clone());
                    geom.setModelBound((BoundingVolume)new BoundingBox());
                    geom.setLocalTranslation(loc);
                    geom.setLocalRotation(matrix);
                    node.attachChild((Spatial)geom);
                }
                node.setModelBound((BoundingVolume)new BoundingBox());
                worldNode.attachChild((Spatial)node);
            }
        }
        catch (Exception e) {
            e.printStackTrace();
            System.out.println("Failed to Load GeoFile data/geo/" + worldId + ".geo");
        }
        finally {
            try {
                if (roChannel != null) {
                    roChannel.close();
                }
            }
            catch (Exception e) {}
        }
        worldNode.updateGeometricState();
        return worldNode;
    }
}

