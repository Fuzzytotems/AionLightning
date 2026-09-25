/*
 * geoEngine 0.1 (package aionjHungary.geoEngine) - a cut-down copy of the jMonkeyEngine3 math,
 * scene-graph and collision code (BSD licence, jmonkeyengine.org) repackaged by "aionjHungary".
 * Source recovered from gameserver/lib/geoEngine-0.1.jar with CFR 0.152 and repaired so that it
 * recompiles to the jar's API and (modulo javac-version differences) bytecode; see ../../README.md.
 */
package aionjHungary.geoEngine;

import com.jme3.bounding.BoundingBox;
import com.jme3.bounding.BoundingVolume;
import com.jme3.math.Vector3f;
import com.jme3.scene.Mesh;
import com.jme3.scene.VertexBuffer;
import com.jme3.util.BufferUtils;
import java.io.File;
import java.io.FileNotFoundException;
import java.io.IOException;
import java.io.RandomAccessFile;
import java.nio.ByteOrder;
import java.nio.MappedByteBuffer;
import java.nio.channels.FileChannel;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import org.apache.log4j.Logger;

public class Meshs {
    private static Logger log = Logger.getLogger(Meshs.class);

    public static Map<String, List<Mesh>> load() {
        Map<String, List<Mesh>> nodeList = new HashMap<String, List<Mesh>>();
        File geoFile = new File("data/geo/meshs.geo");
        FileChannel roChannel = null;
        MappedByteBuffer geo = null;
        try {
            roChannel = new RandomAccessFile(geoFile, "r").getChannel();
            int size = (int)roChannel.size();
            geo = roChannel.map(FileChannel.MapMode.READ_ONLY, 0L, size).load();
        }
        catch (FileNotFoundException e) {
            log.warn("geo/meshs.geo file missing!!");
            return null;
        }
        catch (IOException e) {
            log.warn("geo/meshs.geo file IO error!!");
            return null;
        }
        geo.order(ByteOrder.LITTLE_ENDIAN);
        while (geo.hasRemaining()) {
            short namelenght = geo.getShort();
            byte[] nameByte = new byte[namelenght];
            geo.get(nameByte);
            String name = new String(nameByte);
            int modelCount = geo.getShort();
            List<Mesh> meshs = new ArrayList<Mesh>();
            for (int c = 0; c < modelCount; ++c) {
                Mesh m = new Mesh();
                short vectorCount = geo.getShort();
                Vector3f[] vertices = new Vector3f[vectorCount];
                for (short x = 0; x < vectorCount; x = (short)(x + 1)) {
                    vertices[x] = new Vector3f(geo.getFloat(), geo.getFloat(), geo.getFloat());
                }
                short tringle = geo.getShort();
                short[] indexes = new short[tringle];
                for (short x = 0; x < tringle; x = (short)(x + 1)) {
                    indexes[x] = geo.getShort();
                }
                m.setBuffer(VertexBuffer.Type.Position, 3, BufferUtils.createFloatBuffer((Vector3f[])vertices));
                m.setBuffer(VertexBuffer.Type.Index, 3, BufferUtils.createShortBuffer((short[])indexes));
                m.setBound((BoundingVolume)new BoundingBox());
                m.updateBound();
                meshs.add(m);
            }
            nodeList.put(name.toLowerCase(), meshs);
        }
        return nodeList;
    }
}

