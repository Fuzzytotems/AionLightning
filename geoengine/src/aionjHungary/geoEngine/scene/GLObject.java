/*
 * geoEngine 0.1 (package aionjHungary.geoEngine) - a cut-down copy of the jMonkeyEngine3 math,
 * scene-graph and collision code (BSD licence, jmonkeyengine.org) repackaged by "aionjHungary".
 * Source recovered from gameserver/lib/geoEngine-0.1.jar with CFR 0.152 and repaired so that it
 * recompiles to the jar's API and (modulo javac-version differences) bytecode; see ../../README.md.
 */
package aionjHungary.geoEngine.scene;

public abstract class GLObject
implements Cloneable {
    protected int id = -1;
    protected Object handleRef = null;
    protected boolean updateNeeded = true;
    protected final Type type;

    public GLObject(Type type) {
        this.type = type;
        this.handleRef = new Object();
    }

    protected GLObject(Type type, int id) {
        this.type = type;
        this.id = id;
    }

    public void setId(int id) {
        if (this.id != -1) {
            throw new IllegalStateException("ID has already been set for this GL object.");
        }
        this.id = id;
    }

    public int getId() {
        return this.id;
    }

    public void setUpdateNeeded() {
        this.updateNeeded = true;
    }

    public void clearUpdateNeeded() {
        this.updateNeeded = false;
    }

    public boolean isUpdateNeeded() {
        return this.updateNeeded;
    }

    public String toString() {
        return this.type.name() + " " + Integer.toHexString(this.hashCode());
    }

    protected GLObject clone() {
        try {
            GLObject obj = (GLObject)super.clone();
            obj.handleRef = new Object();
            obj.id = -1;
            obj.updateNeeded = true;
            return obj;
        }
        catch (CloneNotSupportedException ex) {
            throw new AssertionError();
        }
    }

    public abstract void resetObject();

    public abstract GLObject createDestructableClone();

    public static enum Type {
        VertexBuffer,
        ShaderSource,
        Shader;

    }
}

