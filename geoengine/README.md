# geoEngine 0.1: recovered Java source

This directory holds Java source for `gameserver/lib/geoEngine-0.1.jar` (package
`aionjHungary.geoEngine`). The game server uses this library for geodata: terrain height
(`GeoMap.getZ`) and line of sight (`GeoMap.canSee`). The jar was only ever distributed as a binary.
This source was recovered from it so the geo code can go through the same Java to C++ generator as
the rest of the code base.

## Provenance

| | |
|---|---|
| Input | `gameserver/lib/geoEngine-0.1.jar`, SHA-256 `54eac9fea9c5951b51f2682e1dccd934bb5bdf147dc508a1548ae058086d0dc7` |
| Jar build | Ant 1.7.1, Java 6 class files (major 50), debug info on (LocalVariableTable present), class timestamps March to April 2011 |
| Content | A cut-down copy of the jMonkeyEngine 3 math, scene-graph and collision code (early 2011, BSD licence, jmonkeyengine.org), repackaged by "aionjHungary", plus that project's own loader (`GeoWorldLoader`) and world node (`models.GeoMap`) |
| Decompiler | CFR 0.152 |
| Repair | Compile errors fixed, CFR mis-decompilations corrected, and statement structure restored to match the jar's bytecode. Original local-variable names and types were checked against the jar's LocalVariableTable. |
| Not included | `test/GeodataLoad` (a stand-alone test main in the jar, outside the library package) |

All 45 top-level classes of the library package are here, one `.java` file each, with the same
package and class names, nested classes, member order and signatures as the jar. The jar holds 61
`aionjHungary` class files; the other 16 are nested, anonymous and switch-map classes that javac
generates from those 45 files.

### Verification

`verify/run-all.sh WORKDIR [queriesPerKind] [seeds...]` repeats every check below. It writes only
inside `WORKDIR`, and a full run takes about a minute.

1. **API.** `verify/bcdiff.py` compares every class of the jar with the recompiled classes. It
   checks class flags, supertypes, generic signatures, named member classes, and each field and
   method, including private ones: descriptor, flags, generic signature, `throws`, constant
   values, `@Deprecated`. The result is identical except for three extra methods in the stale
   class `GeoNode` (see [Differences from the jar](#differences-from-the-jar)). `verify/api-diff.sh`
   runs a plain `javap -p` diff. Its only other differences are compiler-generated synthetic
   members, listed at the end of that section.
2. **Bytecode.** `bcdiff.py --code` normalizes every method body so that javac 6 output (the jar)
   and javac 21 output (`--release 8`) of the same source compare equal. Details are in the
   script's header. Of 1,292 method bodies, 1,289 are identical. The remaining three are the two
   stale-class repairs and one extra `checkcast` that javac inserts. Local-variable names and types
   also match the jar's LocalVariableTable. The script reports 0 unexplained differences.
3. **Source compatibility.** `commons/src`, all of `gameserver/src` and `gameserver/data/scripts`
   (3,539 classes) compile with `javac --release 8` against the recompiled classes. The output is
   byte-identical to the same build against the original jar.
   - `NpcShoutsService.java` needs a one-line cast for an ambiguous varargs call. javac ≥ 8
     rejects the call whichever geoEngine is used. The script patches a copy of the file; the
     repository file is unchanged.
4. **Behaviour.** `verify/GeoDiffTest.java` generates synthetic geodata, loads it with the original
   jar and the recompiled classes side by side (two isolated class loaders), and compares:
   - the loaded scene graphs;
   - `getZ` results bit for bit;
   - `canSee` results;
   - every exception, by class and message.

   The data has 9 worlds and 11 models: heightmap and flat terrain, dense, sparse and axis-aligned
   placements, zero, negative and skewed transforms, and missing or truncated files. Three seeds
   of 20,000 `getZ` and 20,000 `canSee` queries per world gave 1,080,090 comparisons, all
   identical. The comparison covers the exceptions the original throws: out-of-map
   `ArrayIndexOutOfBoundsException`, `NullPointerException` on worlds without models, and
   `ArithmeticException` from singular placement matrices. It also reaches the BIH case where a
   split distance is `0 * inf = NaN`.

## Layout

```
geoengine/
  src/aionjHungary/geoEngine/**   the library: 45 files (this is what gets ported)
  jme3-stubs/src/com/jme3/**      compile-only stubs, see below (never shipped, never ported)
  verify/                         build.sh, bcdiff.py, api-diff.sh, GeoDiffTest.java, run-all.sh
```

Build: `verify/build.sh OUTDIR`. It runs `javac --release 8 -g` with `log4j-1.2.16.jar` and the
stubs on the class path. `--release 8` matters for two reasons:

- `Node.clone()` references `javax.activation.UnsupportedDataTypeException`, which was removed
  from the JDK in Java 11.
- With `--release 9+`, calls such as `floatBuffer.clear()` would bind to the covariant NIO
  overrides added in Java 9 instead of `Buffer.clear()`.

### Dead code and the jME3 stubs

- **`Meshs` and `ModelList`** are an older loader written against the real jMonkeyEngine 3
  (`com.jme3.*`) and log4j. jME3 is neither in the jar nor on the server's class path, so on the
  JVM these two classes fail with `NoClassDefFoundError` as soon as they are touched. Nothing
  touches them.
  - Their public signatures mention `com.jme3.scene.Mesh` and `com.jme3.scene.Node`. To keep the
    API identical, `jme3-stubs/` declares exactly the jME3 members their constant pools reference
    (10 classes). The stub bodies throw; the stubs are compile-time only.
  - **Exclude `Meshs`, `ModelList` and `jme3-stubs/` from the C++ port.**
- Also unreachable from the game server:
  - `GeoNode`, `BoundingSphere`, `SweepSphere`, `Eigen3f`, `Transform`, `Intersection`,
    `BIHTriangle`, `TriangleAxisComparator`, `IndexByteBuffer`, `IndexIntBuffer`;
  - most of `BufferUtils`, `Mesh` and `VertexBuffer`.
- The game server's only entry points are:
  - `GeoWorldLoader.loadMeshs/loadWorld`;
  - `new GeoMap(String, int)`, `GeoMap.getZ(float,float,float)` and `GeoMap.canSee(6 floats)`;
  - `org.openaion.gameserver.geo.GeoEngine`.

## What was repaired

**Compile errors.** `javac` on the raw CFR output (with log4j on the class path) gives 63 errors:

| Where | Errors | Cause and fix |
|---|---|---|
| `Meshs`, `ModelList` | 51 | Missing `com.jme3.*`: compile-only stubs, see above |
| `utils/IntMap` | 4 | Lost generics: `import ...IntMap.Entry` (the nested class is not in scope in the `implements Iterable<Entry>` clause) and `(T)` casts on raw `Entry.value` |
| `scene/Node.descendantMatches` | 2 | Lost generics: `List<T>` result, `(T)` cast |
| `math/FastMath.convertHalfToFloat` | 2 | Lost `(int)` cast on the `switch` selector (cases `0x8000`/`0xfc00` do not fit a `short`) |
| `scene/Mesh.setInterleaved` | 1 | Raw `ArrayList` → `ArrayList<VertexBuffer>` |
| `models/GeoNode` | 2 (+1) | Stale class, see [Differences from the jar](#differences-from-the-jar) |
| `bounding/BoundingSphere.intersects(BoundingVolume)` | 1 | Stale call, same section |

**Behavioural mis-decompilations**, found by the bytecode comparison:

- `Vector3f.toArray(float[])` and `Vector2f.toArray(float[])`: CFR folded
  `if (floats == null) floats = new float[3]; floats[0] = x; ...` into
  `if (floats == null) floats = new float[]{x, y, z};`. A non-null argument was then returned
  without being filled.
- `VertexBuffer.Type.MiscAttrib` had lost its `@Deprecated`.

**Structure restored for bytecode fidelity** (no change in behaviour). The first normalized
comparison flagged 94 method bodies whose code differed from the jar although it compiled. The
restored patterns:

- statements that CFR had moved into expressions, sometimes reordered: `if ((t = f()) < x)`,
  `boolean bl = eq = ...`, `float z2 = (z += 1) - (tz += 1)`, and a 12-term determinant inlined into
  one expression in `Matrix4f.invert`;
- statements that CFR reordered: `BoundingSphere.merge`, `FastMath.counterClockwise`,
  `Eigen3f.computeVectors`, `GeoMap.getZ(x, y)` and `GeoMap.canSee`;
- ternaries and `return a && b` back to the original `if`/`else` and `if (...) return false; return true;` forms;
- chained assignments (`m01 = m02 = m03 = 0f`);
- element-wise array stores that CFR had turned into initializers;
- `for (int i = 0, max = n; ...)` loops;
- local-variable scopes and declarations: hoisted declarations, per-loop `int i`, `switch` cases
  without braces, and `for` loops where CFR had `int n = j++; a[n] = a[n] * m;`;
- declared types: `Map`/`List` vs `HashMap`/`ArrayList` (interface vs. virtual calls), and `int`
  vs `short` locals such as `tringle`, `nameLenght` and `vert1..3`;
- catch-variable names (`ex`).

Label names, casts such as `(Object)((Object)x)` and CFR's file headers were cleaned up.

## Differences from the jar

These are all expected, and `bcdiff.py` lists them as explained.

1. **`models/GeoNode` is stale in the jar.** It was compiled against an older `Spatial`, so on the
   JVM some of its methods fail at run time:
   - It does not implement the abstract `Spatial.getVertexCount()`, `getTriangleCount()` and
     `setTransform(Matrix3f, Vector3f, float)`, so calling them throws `AbstractMethodError`.
   - `updateWorldBound()` starts with `super.updateWorldBound()`, which does not exist, so it
     throws `NoSuchMethodError`.

   Java source cannot express a concrete class with unimplemented abstract methods. The source
   therefore declares the three methods, which throw `AbstractMethodError`. These are the only
   extra members. `updateWorldBound()` throws the `NoSuchMethodError` explicitly; its unreachable
   remainder is kept in a comment.
2. **`BoundingSphere.intersects(BoundingVolume)`** is stale in the same way. It calls a missing
   `BoundingVolume.intersectsSphere` and now throws the `NoSuchMethodError` explicitly.
3. **`Mesh.setInterleaved()`**: javac ≥ 8 inserts a `checkcast VertexBuffer` on the generic
   `IntMap.Entry<VertexBuffer>.getValue()` result; javac 6 did not. The map only ever holds
   `VertexBuffer`s.
4. **Synthetic members only, all javac-version artefacts:**
   - enums gain a private `$values()`;
   - javac 21 switches on `ordinal()` directly for enums of the same file, so the switch-map
     holder `VertexBuffer$1` and one map field of `Mesh$1` disappear;
   - the anonymous `TempVars$1` is no longer flagged `final`;
   - javac 6 LocalVariableTable entries such as `i$` differ.

## Geodata files read by the loader

**Location.** `GeoWorldLoader` reads from a static directory prefix, `GEO_DIR`:

- The default is `"data/geo/"`, relative to the process working directory. `setGeoDir(String)`
  replaces it; the value is concatenated, so it must end with a separator.
- The game server never calls `setGeoDir`. It loads:
  - `data/geo/meshs.geo` once;
  - `data/geo/<mapId>.geo` for every map in `WORLD_MAPS_DATA`, where `<mapId>` is decimal, for
    example `data/geo/110010000.geo`.
- This is only done when `gameserver.enable.geo=true`.
- The repository ships no geodata: `gameserver/data/geo/README` says it was removed for
  copyright reasons.

**Reading.** Both files are memory-mapped read-only (`FileChannel.map`, size truncated to `int`),
read little-endian, and never closed.

- A missing or unreadable file prints a stack trace, then:
  - `loadMeshs()` returns `null`;
  - `loadWorld()` returns `false`.
- A truncated file throws `BufferUnderflowException` out of the loader.

### `meshs.geo`: model library

The file is a sequence of records, read until the end of the file.

```
record:
  int16  nameLen
  byte   name[nameLen]         decoded with the platform charset; NOT lowercased
  int16  meshCount             signed; <= 0 means no meshes and no entry
  mesh[meshCount]:
    int16   vertCount          number of vertices; must be <= 10922 (see below)
    float32 xyz[vertCount][3]
    int16   idxCount           number of indices (3 per triangle), <= 32767
    uint16  index[idxCount]    triangle list; read as unsigned (0..65535) by the BIH
```

- Each record becomes a `Node` (named only when `setDebugMod(true)`). Each mesh becomes one child
  `Geometry` whose `Mesh` has a Position buffer (3 floats per vertex), an Index buffer and a BIH
  collision tree built immediately (at most 21 triangles per leaf, depth limit 100).
- The result map is keyed by the record name exactly as stored. A later record with the same name
  replaces the earlier one. World files look models up by the lower-cased placement name, so
  library names must already be lower-case to be usable.
- **Count limits.** The vertex loop uses an `int16` counter over `vertCount * 3` floats.
  - With more than 10922 vertices the counter wraps negative, and the loader fails with
    `BufferOverflowException`.
  - A negative count gives `NegativeArraySizeException` or `IllegalArgumentException`.

### `<worldId>.geo`: terrain and model placements

```
uint8    terrainType          0 = flat, anything else = height map
if terrainType == 0:
  int16  height               whole map at height / 32
else:
  int32  n
  int16  heights[n]           square grid: size = (int)sqrt(n); sample (xi, yi) = heights[xi*size + yi]
                              world position of a sample = (2*xi, 2*yi); height unit = 1/32
then, until end of file, placements:
  int16   nameLen
  byte    name[nameLen]       looked up as name.toLowerCase(); unknown names are skipped
  float32 x, y, z             translation
  float32 rot[9]              3x3 rotation, row-major (m00 m01 m02 m10 ... m22)
  float32 scale               uniform scale
```

**Placing a model.** For each placement, the loader:

1. clones the named library node. The `Geometry` objects are new, but they share the `Mesh` and
   its BIH tree.
2. sets each `Geometry`'s world matrix to `T * R * S`: `loadIdentity`, `setRotationMatrix(rot)`,
   `scale(scale)`, `setTranslation(x, y, z)`.
3. computes its world bounding box and calls `GeoMap.attachChild`.

**Cells.**

- `new GeoMap(name, worldSize)` creates one empty cell `Node` per 256×256 square: x and y from
  0 to `worldSize` in steps of 256, each cell's box spanning z 0..4000.
- The constructor ignores `name`.
- `attachChild` attaches the model to every cell whose box intersects it. Because
  `Node.attachChild` re-parents, the model ends up only in the last intersecting cell.
- A model outside all cells is dropped.
- At the end of the file, `GeoMap.updateModelBound()` removes empty cells and merges the bounds.
- A world with no placed model therefore has no bound. Every `getZ` on it throws
  `NullPointerException`, and so does every `canSee` that gets as far as casting its ray.

### Query behaviour a port must keep

- **`getZ(x, y, z)`** returns the Z of the closest hit of a ray from `(x, y, z+2)` straight down,
  limited to length `z+2`. It falls back to the terrain height, which counts as a hit at distance
  `z - terrainZ` when `terrainZ < z + 2`.
  - Terrain height uses `p13 = p1 + (p1 - p3) * (x % 1)`. The sign is really wrong in the
    original; keep it.
  - Coordinates outside the height map throw `ArrayIndexOutOfBoundsException`.
- **`canSee(...)`** returns false beyond 80 units of horizontal distance. Otherwise both eye
  heights are raised by 1, the terrain is sampled every 2 units along the line, and one ray is
  cast between the eyes.
- **Numerics:**
  - Ray/box and BIH traversal rely on Java `float` semantics: `Math.min`/`max` propagate NaN and
    order -0.0 before +0.0; `1f/0f` gives infinity.
  - A singular placement matrix (for example scale 0) makes every ray that reaches its bound throw
    `ArithmeticException` from `Matrix4f.invert()`.
