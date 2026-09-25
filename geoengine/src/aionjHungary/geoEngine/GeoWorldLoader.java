/*
 * geoEngine 0.1 (package aionjHungary.geoEngine) - a cut-down copy of the jMonkeyEngine3 math,
 * scene-graph and collision code (BSD licence, jmonkeyengine.org) repackaged by "aionjHungary".
 * Source recovered from gameserver/lib/geoEngine-0.1.jar with CFR 0.152 and repaired so that it
 * recompiles to the jar's API and (modulo javac-version differences) bytecode; see ../../README.md.
 */
package aionjHungary.geoEngine;

import aionjHungary.geoEngine.math.Matrix3f;
import aionjHungary.geoEngine.math.Vector3f;
import aionjHungary.geoEngine.models.GeoMap;
import aionjHungary.geoEngine.scene.Geometry;
import aionjHungary.geoEngine.scene.Mesh;
import aionjHungary.geoEngine.scene.Node;
import aionjHungary.geoEngine.scene.Spatial;
import aionjHungary.geoEngine.scene.VertexBuffer;
import java.io.File;
import java.io.FileNotFoundException;
import java.io.IOException;
import java.io.RandomAccessFile;
import java.nio.ByteOrder;
import java.nio.FloatBuffer;
import java.nio.MappedByteBuffer;
import java.nio.ShortBuffer;
import java.nio.channels.FileChannel;
import java.util.HashMap;
import java.util.Map;

public class GeoWorldLoader {
    private static String GEO_DIR = "data/geo/";
    private static boolean DEBUG = false;

    public static void setGeoDir(String dir) {
        GEO_DIR = dir;
    }

    public static void setDebugMod(boolean debug) {
        DEBUG = debug;
    }

    public static Map<String, Spatial> loadMeshs() {
        Map<String, Spatial> geoms = new HashMap<String, Spatial>();
        File geoFile = new File(GEO_DIR + "meshs.geo");
        FileChannel roChannel = null;
        MappedByteBuffer geo = null;
        try {
            roChannel = new RandomAccessFile(geoFile, "r").getChannel();
            int size = (int)roChannel.size();
            geo = roChannel.map(FileChannel.MapMode.READ_ONLY, 0L, size).load();
        }
        catch (FileNotFoundException e) {
            e.printStackTrace();
            return null;
        }
        catch (IOException e) {
            e.printStackTrace();
            return null;
        }
        geo.order(ByteOrder.LITTLE_ENDIAN);
        while (geo.hasRemaining()) {
            short namelenght = geo.getShort();
            byte[] nameByte = new byte[namelenght];
            geo.get(nameByte);
            String name = new String(nameByte);
            int modelCount = geo.getShort();
            Node node = new Node(DEBUG ? name : null);
            for (int c = 0; c < modelCount; ++c) {
                Mesh m = new Mesh();
                int vectorCount = geo.getShort() * 3;
                FloatBuffer vertices = FloatBuffer.allocate(vectorCount);
                for (short x = 0; x < vectorCount; x = (short)(x + 1)) {
                    vertices.put(geo.getFloat());
                }
                int tringle = geo.getShort();
                ShortBuffer indexes = ShortBuffer.allocate(tringle);
                for (short x = 0; x < tringle; x = (short)(x + 1)) {
                    indexes.put(geo.getShort());
                }
                m.setBuffer(VertexBuffer.Type.Position, 3, vertices);
                m.setBuffer(VertexBuffer.Type.Index, 3, indexes);
                m.createCollisionData();
                Geometry geom = new Geometry(null, m);
                if (modelCount == 1) {
                    geoms.put(name, geom);
                }
                node.attachChild(geom);
            }
            if (node.getChildren().isEmpty()) continue;
            geoms.put(name, node);
        }
        return geoms;
    }

    public static boolean loadWorld(int worldId, Map<String, Spatial> models, GeoMap map) {
        File geoFile = new File(GEO_DIR + worldId + ".geo");
        FileChannel roChannel = null;
        MappedByteBuffer geo = null;
        try {
            roChannel = new RandomAccessFile(geoFile, "r").getChannel();
            int size = (int)roChannel.size();
            geo = roChannel.map(FileChannel.MapMode.READ_ONLY, 0L, size).load();
        }
        catch (FileNotFoundException e) {
            e.printStackTrace();
            return false;
        }
        catch (IOException e) {
            e.printStackTrace();
            return false;
        }
        geo.order(ByteOrder.LITTLE_ENDIAN);
        if (geo.get() == 0) {
            map.setTerrainData(new short[]{geo.getShort()});
        } else {
            int size = geo.getInt();
            short[] terrainData = new short[size];
            for (int i = 0; i < size; ++i) {
                terrainData[i] = geo.getShort();
            }
            map.setTerrainData(terrainData);
        }
        while (geo.hasRemaining()) {
            int nameLenght = geo.getShort();
            byte[] nameByte = new byte[nameLenght];
            geo.get(nameByte);
            String name = new String(nameByte);
            Vector3f loc = new Vector3f(geo.getFloat(), geo.getFloat(), geo.getFloat());
            float[] matrix = new float[9];
            for (int i = 0; i < 9; ++i) {
                matrix[i] = geo.getFloat();
            }
            float scale = geo.getFloat();
            Matrix3f matrix3f = new Matrix3f();
            matrix3f.set(matrix);
            Spatial node = models.get(name.toLowerCase());
            if (node == null) continue;
            Spatial nodeClone = null;
            try {
                nodeClone = node.clone();
            }
            catch (CloneNotSupportedException e) {
                e.printStackTrace();
            }
            nodeClone.setTransform(matrix3f, loc, scale);
            nodeClone.updateModelBound();
            map.attachChild(nodeClone);
        }
        map.updateModelBound();
        return true;
    }
}

