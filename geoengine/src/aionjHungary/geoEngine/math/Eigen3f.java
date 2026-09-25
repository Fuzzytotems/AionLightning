/*
 * geoEngine 0.1 (package aionjHungary.geoEngine) - a cut-down copy of the jMonkeyEngine3 math,
 * scene-graph and collision code (BSD licence, jmonkeyengine.org) repackaged by "aionjHungary".
 * Source recovered from gameserver/lib/geoEngine-0.1.jar with CFR 0.152 and repaired so that it
 * recompiles to the jar's API and (modulo javac-version differences) bytecode; see ../../README.md.
 */
package aionjHungary.geoEngine.math;

import aionjHungary.geoEngine.math.FastMath;
import aionjHungary.geoEngine.math.Matrix3f;
import aionjHungary.geoEngine.math.Vector3f;
import java.util.logging.Logger;

public class Eigen3f {
    private static final Logger logger = Logger.getLogger(Eigen3f.class.getName());
    float[] eigenValues = new float[3];
    Vector3f[] eigenVectors = new Vector3f[3];
    static final double ONE_THIRD_DOUBLE = 0.3333333333333333;
    static final double ROOT_THREE_DOUBLE = Math.sqrt(3.0);

    public Eigen3f() {
    }

    public Eigen3f(Matrix3f data) {
        this.calculateEigen(data);
    }

    public void calculateEigen(Matrix3f data) {
        this.eigenVectors[0] = new Vector3f();
        this.eigenVectors[1] = new Vector3f();
        this.eigenVectors[2] = new Vector3f();
        Matrix3f scaledData = new Matrix3f(data);
        float maxMagnitude = this.scaleMatrix(scaledData);
        double[] roots = new double[3];
        this.computeRoots(scaledData, roots);
        this.eigenValues[0] = (float)roots[0];
        this.eigenValues[1] = (float)roots[1];
        this.eigenValues[2] = (float)roots[2];
        float[] maxValues = new float[3];
        Vector3f[] maxRows = new Vector3f[3];
        maxRows[0] = new Vector3f();
        maxRows[1] = new Vector3f();
        maxRows[2] = new Vector3f();
        for (int i = 0; i < 3; ++i) {
            Matrix3f tempMatrix = new Matrix3f(scaledData);
            tempMatrix.m00 -= this.eigenValues[i];
            tempMatrix.m11 -= this.eigenValues[i];
            tempMatrix.m22 -= this.eigenValues[i];
            float[] val = new float[1];
            val[0] = maxValues[i];
            if (!this.positiveRank(tempMatrix, val, maxRows[i])) {
                if (maxMagnitude > 1.0f) {
                    for (int j = 0; j < 3; ++j) {
                        this.eigenValues[j] *= maxMagnitude;
                    }
                }
                this.eigenVectors[0].set(Vector3f.UNIT_X);
                this.eigenVectors[1].set(Vector3f.UNIT_Y);
                this.eigenVectors[2].set(Vector3f.UNIT_Z);
                return;
            }
            maxValues[i] = val[0];
        }
        float maxCompare = maxValues[0];
        int i = 0;
        if (maxValues[1] > maxCompare) {
            maxCompare = maxValues[1];
            i = 1;
        }
        if (maxValues[2] > maxCompare) {
            i = 2;
        }
        switch (i) {
            case 0:
                maxRows[0].normalizeLocal();
                this.computeVectors(scaledData, maxRows[0], 1, 2, 0);
                break;
            case 1:
                maxRows[1].normalizeLocal();
                this.computeVectors(scaledData, maxRows[1], 2, 0, 1);
                break;
            case 2:
                maxRows[2].normalizeLocal();
                this.computeVectors(scaledData, maxRows[2], 0, 1, 2);
                break;
        }
        if (maxMagnitude > 1.0f) {
            for (i = 0; i < 3; ++i) {
                this.eigenValues[i] *= maxMagnitude;
            }
        }
    }

    private float scaleMatrix(Matrix3f mat) {
        float max = FastMath.abs(mat.m00);
        float abs = FastMath.abs(mat.m01);
        if (abs > max) {
            max = abs;
        }
        abs = FastMath.abs(mat.m02);
        if (abs > max) {
            max = abs;
        }
        abs = FastMath.abs(mat.m11);
        if (abs > max) {
            max = abs;
        }
        abs = FastMath.abs(mat.m12);
        if (abs > max) {
            max = abs;
        }
        abs = FastMath.abs(mat.m22);
        if (abs > max) {
            max = abs;
        }
        if (max > 1.0f) {
            float fInvMax = 1.0f / max;
            mat.multLocal(fInvMax);
        }
        return max;
    }

    private void computeVectors(Matrix3f mat, Vector3f vect, int index1, int index2, int index3) {
        Vector3f vectorU = new Vector3f();
        Vector3f vectorV = new Vector3f();
        Vector3f.generateComplementBasis(vectorU, vectorV, vect);
        Vector3f tempVect = mat.mult(vectorU);
        float p00 = this.eigenValues[index3] - vectorU.dot(tempVect);
        float p01 = vectorV.dot(tempVect);
        float p11 = this.eigenValues[index3] - vectorV.dot(mat.mult(vectorV));
        float invLength;
        float max = FastMath.abs(p00);
        int row = 0;
        float fAbs = FastMath.abs(p01);
        if (fAbs > max) {
            max = fAbs;
        }
        fAbs = FastMath.abs(p11);
        if (fAbs > max) {
            max = fAbs;
            row = 1;
        }
        if (max >= 1.0E-4f) {
            if (row == 0) {
                invLength = FastMath.invSqrt(p00 * p00 + p01 * p01);
                p00 *= invLength;
                p01 *= invLength;
                vectorU.mult(p01, this.eigenVectors[index3]).addLocal(vectorV.mult(p00));
            } else {
                invLength = FastMath.invSqrt(p11 * p11 + p01 * p01);
                p11 *= invLength;
                p01 *= invLength;
                vectorU.mult(p11, this.eigenVectors[index3]).addLocal(vectorV.mult(p01));
            }
        } else if (row == 0) {
            this.eigenVectors[index3] = vectorV;
        } else {
            this.eigenVectors[index3] = vectorU;
        }
        Vector3f vectorS = vect.cross(this.eigenVectors[index3]);
        mat.mult(vect, tempVect);
        p00 = this.eigenValues[index1] - vect.dot(tempVect);
        p01 = vectorS.dot(tempVect);
        p11 = this.eigenValues[index1] - vectorS.dot(mat.mult(vectorS));
        max = FastMath.abs(p00);
        row = 0;
        fAbs = FastMath.abs(p01);
        if (fAbs > max) {
            max = fAbs;
        }
        fAbs = FastMath.abs(p11);
        if (fAbs > max) {
            max = fAbs;
            row = 1;
        }
        if (max >= 1.0E-4f) {
            if (row == 0) {
                invLength = FastMath.invSqrt(p00 * p00 + p01 * p01);
                p00 *= invLength;
                p01 *= invLength;
                this.eigenVectors[index1] = vect.mult(p01).add(vectorS.mult(p00));
            } else {
                invLength = FastMath.invSqrt(p11 * p11 + p01 * p01);
                p11 *= invLength;
                p01 *= invLength;
                this.eigenVectors[index1] = vect.mult(p11).add(vectorS.mult(p01));
            }
        } else if (row == 0) {
            this.eigenVectors[index1].set(vectorS);
        } else {
            this.eigenVectors[index1].set(vect);
        }
        this.eigenVectors[index3].cross(this.eigenVectors[index1], this.eigenVectors[index2]);
    }

    private boolean positiveRank(Matrix3f matrix, float[] maxMagnitudeStore, Vector3f maxRowStore) {
        maxMagnitudeStore[0] = -1.0f;
        int iMaxRow = -1;
        for (int iRow = 0; iRow < 3; ++iRow) {
            for (int iCol = iRow; iCol < 3; ++iCol) {
                float fAbs = FastMath.abs(matrix.get(iRow, iCol));
                if (!(fAbs > maxMagnitudeStore[0])) continue;
                maxMagnitudeStore[0] = fAbs;
                iMaxRow = iRow;
            }
        }
        maxRowStore.set(matrix.getRow(iMaxRow));
        return maxMagnitudeStore[0] >= 1.0E-4f;
    }

    private void computeRoots(Matrix3f mat, double[] rootsStore) {
        double a = mat.m00;
        double b = mat.m01;
        double c = mat.m02;
        double d = mat.m11;
        double e = mat.m12;
        double f = mat.m22;
        double char0 = a * d * f + 2.0 * b * c * e - a * e * e - d * c * c - f * b * b;
        double char1 = a * d - b * b + a * f - c * c + d * f - e * e;
        double char2 = a + d + f;
        double char2Div3 = char2 * 0.3333333333333333;
        double abcDiv3 = (char1 - char2 * char2Div3) * 0.3333333333333333;
        if (abcDiv3 > 0.0) {
            abcDiv3 = 0.0;
        }
        double mbDiv2 = 0.5 * (char0 + char2Div3 * (2.0 * char2Div3 * char2Div3 - char1));
        double q = mbDiv2 * mbDiv2 + abcDiv3 * abcDiv3 * abcDiv3;
        if (q > 0.0) {
            q = 0.0;
        }
        double magnitude = Math.sqrt(-abcDiv3);
        double angle = Math.atan2(Math.sqrt(-q), mbDiv2) * 0.3333333333333333;
        double cos = Math.cos(angle);
        double sin = Math.sin(angle);
        double root0 = char2Div3 + 2.0 * magnitude * cos;
        double root1 = char2Div3 - magnitude * (cos + ROOT_THREE_DOUBLE * sin);
        double root2 = char2Div3 - magnitude * (cos - ROOT_THREE_DOUBLE * sin);
        if (root1 >= root0) {
            rootsStore[0] = root0;
            rootsStore[1] = root1;
        } else {
            rootsStore[0] = root1;
            rootsStore[1] = root0;
        }
        if (root2 >= rootsStore[1]) {
            rootsStore[2] = root2;
        } else {
            rootsStore[2] = rootsStore[1];
            if (root2 >= rootsStore[0]) {
                rootsStore[1] = root2;
            } else {
                rootsStore[1] = rootsStore[0];
                rootsStore[0] = root2;
            }
        }
    }

    public static void main(String[] args) {
        Matrix3f mat = new Matrix3f(2.0f, 1.0f, 1.0f, 1.0f, 2.0f, 1.0f, 1.0f, 1.0f, 2.0f);
        Eigen3f eigenSystem = new Eigen3f(mat);
        logger.info("eigenvalues = ");
        for (int i = 0; i < 3; ++i) {
            logger.info(eigenSystem.getEigenValue(i) + " ");
        }
        logger.info("eigenvectors = ");
        for (int i = 0; i < 3; ++i) {
            Vector3f vector = eigenSystem.getEigenVector(i);
            logger.info(vector.toString());
            mat.setColumn(i, vector);
        }
        logger.info(mat.toString());
    }

    public float getEigenValue(int i) {
        return this.eigenValues[i];
    }

    public Vector3f getEigenVector(int i) {
        return this.eigenVectors[i];
    }

    public float[] getEigenValues() {
        return this.eigenValues;
    }

    public Vector3f[] getEigenVectors() {
        return this.eigenVectors;
    }
}

