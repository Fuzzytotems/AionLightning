/*
 * geoEngine 0.1 (package aionjHungary.geoEngine) - a cut-down copy of the jMonkeyEngine3 math,
 * scene-graph and collision code (BSD licence, jmonkeyengine.org) repackaged by "aionjHungary".
 * Source recovered from gameserver/lib/geoEngine-0.1.jar with CFR 0.152 and repaired so that it
 * recompiles to the jar's API and (modulo javac-version differences) bytecode; see ../../README.md.
 */
package aionjHungary.geoEngine.utils;

import aionjHungary.geoEngine.collision.bih.BIHNode;
import aionjHungary.geoEngine.math.Matrix3f;
import aionjHungary.geoEngine.math.Matrix4f;
import aionjHungary.geoEngine.math.Quaternion;
import aionjHungary.geoEngine.math.Triangle;
import aionjHungary.geoEngine.math.Vector2f;
import aionjHungary.geoEngine.math.Vector3f;
import java.util.ArrayList;

public class TempVars {
    private static final ThreadLocal<TempVars> varsLocal = new ThreadLocal<TempVars>(){

        @Override
        public TempVars initialValue() {
            return new TempVars();
        }
    };
    private boolean locked = false;
    private StackTraceElement[] lockerStack;
    public final Triangle triangle = new Triangle();
    public final Vector3f vect1 = new Vector3f();
    public final Vector3f vect2 = new Vector3f();
    public final Vector3f vect3 = new Vector3f();
    public final Vector3f vect4 = new Vector3f();
    public final Vector3f vect5 = new Vector3f();
    public final Vector2f vect2d = new Vector2f();
    public final Matrix3f tempMat3 = new Matrix3f();
    public final Matrix4f tempMat4 = new Matrix4f();
    public final Quaternion quat1 = new Quaternion();
    public final float[] fWdU = new float[3];
    public final float[] fAWdU = new float[3];
    public final float[] fDdU = new float[3];
    public final float[] fADdU = new float[3];
    public final float[] fAWxDdU = new float[3];
    public final ArrayList<BIHNode.BIHStackData> bihStack = new ArrayList();

    public static TempVars get() {
        return varsLocal.get();
    }

    private TempVars() {
    }

    public final boolean lock() {
        if (this.locked) {
            System.err.println("INTERNAL ERROR");
            System.err.println("Offending trace: ");
            StackTraceElement[] stack = new Throwable().getStackTrace();
            for (int i = 1; i < stack.length; ++i) {
                System.err.println("\tat " + stack[i].toString());
            }
            System.err.println("Attempted to aquire TempVars lock owned by");
            for (int i = 1; i < this.lockerStack.length; ++i) {
                System.err.println("\tat " + this.lockerStack[i].toString());
            }
            System.exit(1);
            return false;
        }
        this.lockerStack = new Throwable().getStackTrace();
        this.locked = true;
        return true;
    }

    public final boolean unlock() {
        if (!this.locked) {
            System.err.println("INTERNAL ERROR");
            System.err.println("Attempted to release non-existent lock: ");
            StackTraceElement[] stack = new Throwable().getStackTrace();
            for (int i = 1; i < stack.length; ++i) {
                System.err.println("\tat " + stack[i].toString());
            }
            System.exit(1);
            return false;
        }
        this.lockerStack = null;
        this.locked = false;
        return true;
    }
}

