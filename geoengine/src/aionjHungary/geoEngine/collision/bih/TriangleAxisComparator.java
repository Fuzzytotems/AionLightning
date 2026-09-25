/*
 * geoEngine 0.1 (package aionjHungary.geoEngine) - a cut-down copy of the jMonkeyEngine3 math,
 * scene-graph and collision code (BSD licence, jmonkeyengine.org) repackaged by "aionjHungary".
 * Source recovered from gameserver/lib/geoEngine-0.1.jar with CFR 0.152 and repaired so that it
 * recompiles to the jar's API and (modulo javac-version differences) bytecode; see ../../README.md.
 */
package aionjHungary.geoEngine.collision.bih;

import aionjHungary.geoEngine.collision.bih.BIHTriangle;
import aionjHungary.geoEngine.math.Vector3f;
import java.util.Comparator;

public class TriangleAxisComparator
implements Comparator<BIHTriangle> {
    private final int axis;

    public TriangleAxisComparator(int axis) {
        this.axis = axis;
    }

    @Override
    public int compare(BIHTriangle o1, BIHTriangle o2) {
        float v2;
        float v1;
        Vector3f c1 = o1.getCenter();
        Vector3f c2 = o2.getCenter();
        switch (this.axis) {
            case 0: {
                v1 = c1.x;
                v2 = c2.x;
                break;
            }
            case 1: {
                v1 = c1.y;
                v2 = c2.y;
                break;
            }
            case 2: {
                v1 = c1.z;
                v2 = c2.z;
                break;
            }
            default: {
                assert (false);
                return 0;
            }
        }
        if (v1 > v2) {
            return 1;
        }
        if (v1 < v2) {
            return -1;
        }
        return 0;
    }
}

