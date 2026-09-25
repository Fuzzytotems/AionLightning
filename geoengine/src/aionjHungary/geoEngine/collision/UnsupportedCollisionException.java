/*
 * geoEngine 0.1 (package aionjHungary.geoEngine) - a cut-down copy of the jMonkeyEngine3 math,
 * scene-graph and collision code (BSD licence, jmonkeyengine.org) repackaged by "aionjHungary".
 * Source recovered from gameserver/lib/geoEngine-0.1.jar with CFR 0.152 and repaired so that it
 * recompiles to the jar's API and (modulo javac-version differences) bytecode; see ../../README.md.
 */
package aionjHungary.geoEngine.collision;

public class UnsupportedCollisionException
extends UnsupportedOperationException {
    public UnsupportedCollisionException(Throwable arg0) {
        super(arg0);
    }

    public UnsupportedCollisionException(String arg0, Throwable arg1) {
        super(arg0, arg1);
    }

    public UnsupportedCollisionException(String arg0) {
        super(arg0);
    }

    public UnsupportedCollisionException() {
    }
}

