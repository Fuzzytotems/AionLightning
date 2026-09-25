/*
 * geoEngine 0.1 (package aionjHungary.geoEngine) - a cut-down copy of the jMonkeyEngine3 math,
 * scene-graph and collision code (BSD licence, jmonkeyengine.org) repackaged by "aionjHungary".
 * Source recovered from gameserver/lib/geoEngine-0.1.jar with CFR 0.152 and repaired so that it
 * recompiles to the jar's API and (modulo javac-version differences) bytecode; see ../../README.md.
 */
package aionjHungary.geoEngine.scene;

import aionjHungary.geoEngine.bounding.BoundingVolume;
import aionjHungary.geoEngine.collision.Collidable;
import aionjHungary.geoEngine.collision.CollisionResults;
import aionjHungary.geoEngine.math.Matrix3f;
import aionjHungary.geoEngine.math.Ray;
import aionjHungary.geoEngine.math.Vector3f;
import aionjHungary.geoEngine.scene.Geometry;
import aionjHungary.geoEngine.scene.Spatial;
import java.util.ArrayList;
import java.util.List;
import java.util.logging.Logger;
import javax.activation.UnsupportedDataTypeException;

public class Node
extends Spatial
implements Cloneable {
    private static final Logger logger = Logger.getLogger(Node.class.getName());
    protected ArrayList<Spatial> children = new ArrayList(1);

    public Node() {
    }

    public Node(String name) {
        super(name);
    }

    public int getQuantity() {
        return this.children.size();
    }

    @Override
    public int getTriangleCount() {
        int count = 0;
        if (this.children != null) {
            for (int i = 0; i < this.children.size(); ++i) {
                count += this.children.get(i).getTriangleCount();
            }
        }
        return count;
    }

    @Override
    public int getVertexCount() {
        int count = 0;
        if (this.children != null) {
            for (int i = 0; i < this.children.size(); ++i) {
                count += this.children.get(i).getVertexCount();
            }
        }
        return count;
    }

    public int attachChild(Spatial child) {
        if (child == null) {
            throw new NullPointerException();
        }
        if (child.getParent() != this && child != this) {
            if (child.getParent() != null) {
                child.getParent().detachChild(child);
            }
            child.setParent(this);
            this.children.add(child);
        }
        return this.children.size();
    }

    public int attachChildAt(Spatial child, int index) {
        if (child == null) {
            throw new NullPointerException();
        }
        if (child.getParent() != this && child != this) {
            if (child.getParent() != null) {
                child.getParent().detachChild(child);
            }
            child.setParent(this);
            this.children.add(index, child);
        }
        return this.children.size();
    }

    public int detachChild(Spatial child) {
        if (child == null) {
            throw new NullPointerException();
        }
        if (child.getParent() == this) {
            int index = this.children.indexOf(child);
            if (index != -1) {
                this.detachChildAt(index);
            }
            return index;
        }
        return -1;
    }

    public int detachChildNamed(String childName) {
        if (childName == null) {
            throw new NullPointerException();
        }
        for (int x = 0, max = this.children.size(); x < max; ++x) {
            Spatial child = this.children.get(x);
            if (childName.equals(child.getName())) {
                this.detachChildAt(x);
                return x;
            }
        }
        return -1;
    }

    public Spatial detachChildAt(int index) {
        Spatial child = this.children.remove(index);
        if (child != null) {
            child.setParent(null);
        }
        return child;
    }

    public void detachAllChildren() {
        for (int i = this.children.size() - 1; i >= 0; --i) {
            this.detachChildAt(i);
        }
        logger.info("All children removed.");
    }

    public int getChildIndex(Spatial sp) {
        return this.children.indexOf(sp);
    }

    public void swapChildren(int index1, int index2) {
        Spatial c2 = this.children.get(index2);
        Spatial c1 = this.children.remove(index1);
        this.children.add(index1, c2);
        this.children.remove(index2);
        this.children.add(index2, c1);
    }

    public Spatial getChild(int i) {
        return this.children.get(i);
    }

    public Spatial getChild(String name) {
        if (name == null) {
            return null;
        }
        for (int x = 0, cSize = this.getQuantity(); x < cSize; ++x) {
            Spatial child = this.children.get(x);
            if (name.equals(child.getName())) {
                return child;
            }
            if (child instanceof Node) {
                Spatial out = ((Node)child).getChild(name);
                if (out != null) {
                    return out;
                }
            }
        }
        return null;
    }

    public boolean hasChild(Spatial spat) {
        if (this.children.contains(spat)) {
            return true;
        }
        for (int i = 0, max = this.getQuantity(); i < max; ++i) {
            Spatial child = this.children.get(i);
            if (child instanceof Node && ((Node)child).hasChild(spat)) {
                return true;
            }
        }
        return false;
    }

    public List<Spatial> getChildren() {
        return this.children;
    }

    public void childChange(Geometry geometry, int index1, int index2) {
        if (this.parent != null) {
            this.parent.childChange(geometry, index1, index2);
        }
    }

    @Override
    public int collideWith(Collidable other, CollisionResults results) {
        if (other instanceof Ray && !this.worldBound.intersects((Ray)other)) {
            return 0;
        }
        int total = 0;
        for (Spatial child : this.children) {
            total += child.collideWith(other, results);
        }
        return total;
    }

    public <T extends Spatial> List<T> descendantMatches(Class<T> spatialSubclass, String nameRegex) {
        List<T> newList = new ArrayList<T>();
        if (this.getQuantity() < 1) {
            return newList;
        }
        for (Spatial child : this.getChildren()) {
            if (child.matches(spatialSubclass, nameRegex)) {
                newList.add((T)child);
            }
            if (!(child instanceof Node)) continue;
            newList.addAll(((Node)child).descendantMatches(spatialSubclass, nameRegex));
        }
        return newList;
    }

    public <T extends Spatial> List<T> descendantMatches(Class<T> spatialSubclass) {
        return this.descendantMatches(spatialSubclass, null);
    }

    public <T extends Spatial> List<T> descendantMatches(String nameRegex) {
        return this.descendantMatches(null, nameRegex);
    }

    @Override
    public void setModelBound(BoundingVolume modelBound) {
        if (this.children != null) {
            for (int i = 0, max = this.children.size(); i < max; ++i) {
                this.children.get(i).setModelBound(modelBound != null ? modelBound.clone(null) : null);
            }
        }
    }

    @Override
    public void updateModelBound() {
        BoundingVolume resultBound = null;
        if (this.children != null) {
            for (int i = 0, max = this.children.size(); i < max; ++i) {
                Spatial child = this.children.get(i);
                child.updateModelBound();
                if (resultBound != null) {
                    resultBound.mergeLocal(child.getWorldBound());
                } else if (child.getWorldBound() != null) {
                    resultBound = child.getWorldBound().clone(this.worldBound);
                }
            }
        }
        this.worldBound = resultBound;
    }

    @Override
    public void setTransform(Matrix3f rotation, Vector3f loc, float scale) {
        if (this.children != null) {
            for (int i = 0; i < this.children.size(); ++i) {
                this.children.get(i).setTransform(rotation, loc, scale);
            }
        }
    }

    @Override
    public Node clone() throws CloneNotSupportedException {
        Node node = new Node(this.name);
        for (Spatial spatial : this.children) {
            if (spatial instanceof Geometry) {
                Geometry geom = new Geometry(spatial.getName(), ((Geometry)spatial).getMesh());
                node.attachChild(geom);
                continue;
            }
            if (spatial instanceof Node) {
                node.attachChild(((Node)spatial).clone());
                continue;
            }
            new UnsupportedDataTypeException();
        }
        return node;
    }
}

