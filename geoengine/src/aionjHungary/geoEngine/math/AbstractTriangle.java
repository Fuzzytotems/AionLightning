/*
 * geoEngine 0.1 (package aionjHungary.geoEngine) - a cut-down copy of the jMonkeyEngine3 math,
 * scene-graph and collision code (BSD licence, jmonkeyengine.org) repackaged by "aionjHungary".
 * Source recovered from gameserver/lib/geoEngine-0.1.jar with CFR 0.152 and repaired so that it
 * recompiles to the jar's API and (modulo javac-version differences) bytecode; see ../../README.md.
 */
package aionjHungary.geoEngine.math;

import aionjHungary.geoEngine.collision.Collidable;
import aionjHungary.geoEngine.collision.CollisionResults;
import aionjHungary.geoEngine.math.Vector3f;

public abstract class AbstractTriangle
implements Collidable {
    public abstract Vector3f get1();

    public abstract Vector3f get2();

    public abstract Vector3f get3();

    public abstract void set(Vector3f var1, Vector3f var2, Vector3f var3);

    @Override
    public int collideWith(Collidable other, CollisionResults results) {
        return other.collideWith(this, results);
    }
}

