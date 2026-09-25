/*
 * geoEngine 0.1 (package aionjHungary.geoEngine) - a cut-down copy of the jMonkeyEngine3 math,
 * scene-graph and collision code (BSD licence, jmonkeyengine.org) repackaged by "aionjHungary".
 * Source recovered from gameserver/lib/geoEngine-0.1.jar with CFR 0.152 and repaired so that it
 * recompiles to the jar's API and (modulo javac-version differences) bytecode; see ../../README.md.
 */
package aionjHungary.geoEngine.collision;

import aionjHungary.geoEngine.collision.CollisionResult;
import java.util.ArrayList;
import java.util.Collections;
import java.util.Iterator;

public class CollisionResults
implements Iterable<CollisionResult> {
    private final ArrayList<CollisionResult> results = new ArrayList();
    private boolean sorted = true;
    private boolean onlyFirst = false;

    public void clear() {
        this.results.clear();
    }

    @Override
    public Iterator<CollisionResult> iterator() {
        if (!this.sorted) {
            Collections.sort(this.results);
            this.sorted = true;
        }
        return this.results.iterator();
    }

    public void addCollision(CollisionResult result) {
        this.results.add(result);
        if (!this.onlyFirst) {
            this.sorted = false;
        }
    }

    public int size() {
        return this.results.size();
    }

    public CollisionResult getClosestCollision() {
        if (this.size() == 0) {
            return null;
        }
        if (!this.sorted) {
            Collections.sort(this.results);
            this.sorted = true;
        }
        return this.results.get(0);
    }

    public CollisionResult getFarthestCollision() {
        if (this.size() == 0) {
            return null;
        }
        if (!this.sorted) {
            Collections.sort(this.results);
            this.sorted = true;
        }
        return this.results.get(this.size() - 1);
    }

    public CollisionResult getCollision(int index) {
        if (!this.sorted) {
            Collections.sort(this.results);
            this.sorted = true;
        }
        return this.results.get(index);
    }

    public CollisionResult getCollisionDirect(int index) {
        return this.results.get(index);
    }

    public String toString() {
        StringBuilder sb = new StringBuilder();
        sb.append("CollisionResults[");
        for (CollisionResult result : this.results) {
            sb.append(result).append(", ");
        }
        if (this.results.size() > 0) {
            sb.setLength(sb.length() - 2);
        }
        sb.append("]");
        return sb.toString();
    }

    public void setOnlyFirst(boolean onlyFirst) {
        this.onlyFirst = onlyFirst;
    }

    public boolean isOnlyFirst() {
        return this.onlyFirst;
    }
}

