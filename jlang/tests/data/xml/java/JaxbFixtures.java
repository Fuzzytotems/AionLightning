// Generates the expected outputs used by jlang/tests/test_xml_jaxb.cpp with the real JAXB RI.
//
//   javac -cp jaxb-api-2.3.1.jar:jaxb-impl-2.3.9.jar:activation-1.1.1.jar -d out JaxbFixtures.java
//   java -cp out:<same jars> JaxbFixtures <repo root>
//
// Classes mirror the gameserver JAXB classes they are named after (same annotations and field
// order), so the output is what the gameserver would load and marshal.
import javax.xml.bind.*;
import javax.xml.bind.annotation.*;
import javax.xml.bind.annotation.adapters.*;
import java.io.*;
import java.nio.charset.StandardCharsets;
import java.nio.file.*;
import java.util.*;

public class JaxbFixtures {
    static StringBuilder LOG = new StringBuilder();
    static void log(String s) { LOG.append(s).append('\n'); }
    static String cls(Object o) { return o == null ? "null" : o.getClass().getSimpleName(); }

    // ------------------------------------------------------------------ spawns (SpawnsData)
    public enum SpawnTime { DAY, NIGHT }
    public enum SpawnHandlerType { RIFT, STATIC }

    @XmlRootElement(name = "spawns") @XmlAccessorType(XmlAccessType.FIELD)
    public static class SpawnsData {
        @XmlElement(name = "spawn") protected List<SpawnGroup> spawnGroups;
        @XmlTransient private int counter = 0;
        void afterUnmarshal(Unmarshaller u, Object parent) { counter = spawnGroups == null ? 0 : spawnGroups.size(); }
    }

    @XmlAccessorType(XmlAccessType.FIELD) @XmlType(name = "spawn")
    public static class SpawnGroup {
        @XmlAttribute(name = "time") private SpawnTime spawnTime;
        @XmlAttribute(name = "anchor") private String anchor;
        @XmlAttribute(name = "handler") private SpawnHandlerType handler;
        @XmlAttribute(name = "interval") private int interval;
        @XmlAttribute(name = "pool") private int pool = 0;
        @XmlAttribute(name = "npcid") private int npcid;
        @XmlAttribute(name = "map") private int mapid;
        @XmlAttribute(name = "rw") private int randomWalk;
        @XmlAttribute(name = "npcid_dr") private int siegeNpcId_balaur;
        @XmlAttribute(name = "npcid_da") private int siegeNpcId_asmodians;
        @XmlAttribute(name = "npcid_li") private int siegeNpcId_elyos;
        @XmlAttribute(name = "boss") private boolean boss;
        @XmlElement(name = "object") private List<SpawnTemplate> objects;
        @XmlTransient private Map<Integer, Integer> lastSpawnedTemplate = new HashMap<Integer, Integer>();
    }

    @XmlAccessorType(XmlAccessType.FIELD) @XmlType(name = "object")
    public static class SpawnTemplate {
        @XmlTransient private SpawnGroup spawnGroup;
        @XmlAttribute(name = "rw") private int randomWalk;
        @XmlAttribute(name = "w") private int walkerId;
        @XmlAttribute(name = "h") private byte heading;
        @XmlAttribute(name = "z") private float z;
        @XmlAttribute(name = "y") private float y;
        @XmlAttribute(name = "x") private float x;
        @XmlAttribute(name = "staticid") private int staticid;
        @XmlAttribute(name = "fly") private int npcfly;
        @XmlTransient private BitSet spawnState = new BitSet();
        private int spawnId = 0;
    }

    static String dump(SpawnsData d) {
        StringBuilder sb = new StringBuilder();
        sb.append("counter=").append(d.counter).append('\n');
        for (SpawnGroup g : d.spawnGroups) {
            sb.append("spawn time=").append(g.spawnTime).append(" anchor=").append(g.anchor).append(" handler=").append(g.handler)
              .append(" interval=").append(g.interval).append(" pool=").append(g.pool).append(" npcid=").append(g.npcid)
              .append(" map=").append(g.mapid).append(" rw=").append(g.randomWalk).append(" dr=").append(g.siegeNpcId_balaur)
              .append(" da=").append(g.siegeNpcId_asmodians).append(" li=").append(g.siegeNpcId_elyos).append(" boss=").append(g.boss)
              .append(" objects=").append(g.objects == null ? "null" : String.valueOf(g.objects.size())).append('\n');
            if (g.objects != null) for (SpawnTemplate t : g.objects) {
                sb.append("  object rw=").append(t.randomWalk).append(" w=").append(t.walkerId).append(" h=").append(t.heading)
                  .append(" z=").append(t.z).append(" y=").append(t.y).append(" x=").append(t.x).append(" staticid=").append(t.staticid)
                  .append(" fly=").append(t.npcfly).append(" spawnId=").append(t.spawnId).append('\n');
            }
        }
        return sb.toString();
    }

    // ------------------------------------------------------------------ PlayerExperienceTable
    @XmlRootElement(name = "player_experience_table") @XmlAccessorType(XmlAccessType.NONE)
    public static class PlayerExperienceTable {
        @XmlElement(name = "exp") private long[] experience;
    }

    // ------------------------------------------------------------------ behavior probe
    public enum Kind { A, B, @XmlEnumValue("C1") C }

    @XmlRootElement(name = "root") @XmlAccessorType(XmlAccessType.NONE)
    public static class Root {
        @XmlElement(name = "npc") List<Npc> npcs;
        @XmlElement(name = "item") List<Item> items;
        @XmlElement(name = "title") List<Title> titles;
        @XmlElement(name = "ref") Ref ref;
        @XmlElement(name = "mods") Mods mods;
        @XmlElement(name = "group") List<Group> groups;
        @XmlElement(name = "pre") List<String> pre = new ArrayList<String>(Arrays.asList("x", "y"));
        @XmlElement(name = "arr") int[] arr = {7, 7};
        @XmlElement(name = "text") List<String> texts;
        void afterUnmarshal(Unmarshaller u, Object parent) {
            log("after Root parent=" + cls(parent) + " ref.first=" + (ref.first == null ? null : ref.first.name)
                + " ref.any=" + (ref.any == null ? "null" : String.valueOf(ref.any.size()))
                + " gear.items=" + (npcs.get(0).gear.list.items == null ? "null" : "set"));
        }
    }

    @XmlAccessorType(XmlAccessType.NONE)
    public static class Npc {
        static int N = 0;
        int serial = ++N;
        @XmlAttribute(name = "srange") int aggro;
        @XmlAttribute(name = "arange") int arange;
        @XmlAttribute(name = "srange") int rate;
        @XmlAttribute(name = "height") float height = 1;
        @XmlAttribute(name = "flag") boolean flag = true;
        @XmlAttribute(name = "kind") Kind kind;
        @XmlAttribute(name = "n") Integer n;
        @XmlAttribute(name = "ids") List<Integer> ids;
        @XmlElement(name = "equipment") Gear gear;
        @XmlElement(name = "skill") List<Integer> skills;
        @XmlElement(name = "classes") @XmlList List<Kind> classes;
        String id;
        @XmlID @XmlAttribute(name = "npc_id") private void setUid(String s) { id = s; log("setUid [" + s + "] npc#" + serial); }
        void afterUnmarshal(Unmarshaller u, Object parent) {
            log("after Npc " + id + " #" + serial + " parent=" + cls(parent) + " gear.items="
                + (gear == null ? "nogear" : gear.list.items == null ? "null" : "set"));
        }
    }

    @XmlJavaTypeAdapter(GearAdapter.class)
    public static class Gear { GearList list; }

    public static class GearList {
        @XmlElement(name = "item") @XmlIDREF public Item[] items;
        void afterUnmarshal(Unmarshaller u, Object parent) { log("after GearList parent=" + cls(parent) + " items=" + (items == null ? "null" : "set")); }
    }

    public static class GearAdapter extends XmlAdapter<GearList, Gear> {
        public GearList marshal(Gear g) { return null; }
        public Gear unmarshal(GearList l) { log("adapter.unmarshal items=" + (l.items == null ? "null" : "set")); Gear g = new Gear(); g.list = l; return g; }
    }

    @XmlAccessorType(XmlAccessType.FIELD)
    public static class Item {
        static int N = 0;
        @XmlTransient int serial = ++N;
        @XmlAttribute @XmlID String id;
        @XmlAttribute String name;
        void afterUnmarshal(Unmarshaller u, Object parent) { log("after Item [" + id + "] " + name + " #" + serial); }
    }

    @XmlAccessorType(XmlAccessType.FIELD)
    public static class Title { @XmlAttribute @XmlID String id; }

    @XmlAccessorType(XmlAccessType.FIELD)
    public static class Ref {
        @XmlAttribute(name = "first") @XmlIDREF Item first;
        @XmlAttribute(name = "second") @XmlIDREF Item second;
        @XmlAttribute(name = "third") @XmlIDREF Object third;
        @XmlElement(name = "r") @XmlIDREF List<Object> any;
        void afterUnmarshal(Unmarshaller u, Object parent) {
            log("after Ref first=" + (first == null ? null : first.name) + " second=" + (second == null ? null : second.name)
                + " third=" + cls(third) + " any=" + any);
        }
    }

    @XmlAccessorType(XmlAccessType.NONE)
    public static class Mods {
        @XmlElements({@XmlElement(name = "add", type = Add.class), @XmlElement(name = "set", type = Set_.class)})
        TreeSet<M> m;
        void afterUnmarshal(Unmarshaller u, Object parent) { log("after Mods size=" + (m == null ? -1 : m.size())); }
    }

    @XmlAccessorType(XmlAccessType.FIELD)
    public static abstract class M implements Comparable<M> {
        static int ID = 0;
        @XmlTransient int myid;
        @XmlAttribute String name;
        @XmlAttribute int priority;
        M() { myid = ++ID; }
        public int compareTo(M o) { int r = priority - o.priority; return r != 0 ? r : myid - o.myid; }
        void afterUnmarshal(Unmarshaller u, Object parent) { log("after " + getClass().getSimpleName() + " " + name + " #" + myid + " parent=" + cls(parent)); }
    }
    public static class Add extends M {}
    public static class Set_ extends M {}

    @XmlAccessorType(XmlAccessType.FIELD)
    public static class Group {
        @XmlValue String value;
        @XmlAttribute(name = "type", required = true) Kind type;
    }

    static String dump(Root r) {
        StringBuilder sb = new StringBuilder();
        for (Npc n : r.npcs) {
            sb.append("npc #").append(n.serial).append(" id=[").append(n.id).append("] aggro=").append(n.aggro).append(" arange=").append(n.arange)
              .append(" rate=").append(n.rate).append(" height=").append(n.height).append(" flag=").append(n.flag)
              .append(" kind=").append(n.kind).append(" n=").append(n.n).append(" ids=").append(n.ids)
              .append(" skills=").append(n.skills).append(" classes=").append(n.classes);
            if (n.gear != null) {
                sb.append(" gear=[");
                for (Item i : n.gear.list.items) sb.append(i.name).append('#').append(i.serial).append(' ');
                sb.append(']');
            }
            sb.append('\n');
        }
        for (Item i : r.items) sb.append("item [").append(i.id).append("] ").append(i.name).append(" #").append(i.serial).append('\n');
        sb.append("titles=").append(r.titles.size()).append('\n');
        sb.append("ref first=").append(r.ref.first.name).append('#').append(r.ref.first.serial).append(" second=").append(r.ref.second.name)
          .append(" third=").append(cls(r.ref.third)).append(" any=");
        for (Object o : r.ref.any) sb.append(o instanceof Item ? ((Item) o).name : cls(o)).append(' ');
        sb.append('\n');
        sb.append("mods=");
        for (M m : r.mods.m) sb.append(m.getClass().getSimpleName()).append(':').append(m.name).append('#').append(m.myid).append(' ');
        sb.append(r.mods.m.getClass().getSimpleName()).append('\n');
        for (Group g : r.groups) sb.append("group type=").append(g.type).append(" value=[").append(g.value).append("]\n");
        sb.append("pre=").append(r.pre).append('\n');
        sb.append("arr=").append(Arrays.toString(r.arr)).append('\n');
        for (String t : r.texts) sb.append("text=[").append(t).append("]\n");
        return sb.toString();
    }

    // ------------------------------------------------------------------ main
    static void write(Path p, String s) throws IOException { Files.write(p, s.getBytes(StandardCharsets.UTF_8)); }

    public static void main(String[] a) throws Exception {
        Path repo = Paths.get(a[0]);
        Path out = repo.resolve("jlang/tests/data/xml");
        Path data = repo.resolve("gameserver/data/static_data");

        JAXBContext sc = JAXBContext.newInstance(SpawnsData.class);
        for (String f : new String[] {"spawns/Npcs/110010000.xml", "spawns/Rifts/rifts.xml"}) {
            SpawnsData d = (SpawnsData) sc.createUnmarshaller().unmarshal(data.resolve(f).toFile());
            String base = f.replace('/', '_').replace(".xml", "");
            write(out.resolve(base + ".dump.txt"), dump(d));
            Marshaller m = sc.createMarshaller();
            m.setProperty(Marshaller.JAXB_FORMATTED_OUTPUT, true);
            m.marshal(d, out.resolve(base + ".formatted.xml").toFile());
            m = sc.createMarshaller();
            m.marshal(d, out.resolve(base + ".plain.xml").toFile());
        }

        PlayerExperienceTable t = (PlayerExperienceTable) JAXBContext.newInstance(PlayerExperienceTable.class).createUnmarshaller()
            .unmarshal(data.resolve("player_experience_table.xml").toFile());
        write(out.resolve("player_experience_table.expected.txt"), Arrays.toString(t.experience) + "\n");

        JAXBContext pc = JAXBContext.newInstance(Root.class);
        Root r = (Root) pc.createUnmarshaller().unmarshal(out.resolve("probe.xml").toFile());
        write(out.resolve("probe.expected.txt"), LOG + "----\n" + dump(r));
    }
}
