// jlang/Geom.h - java.awt.Point, java.awt.Rectangle (+ java.awt.geom.RectangularShape) and
// java.awt.Polygon with the exact JDK algorithms (integer overflow behaviour included).
//
//   java.awt.Point                  -> jlang::Point*      (public int32_t x, y)
//   java.awt.Rectangle              -> jlang::Rectangle*  (public int32_t x, y, width, height)
//   java.awt.geom.RectangularShape  -> jlang::Rectangle*
//   java.awt.Polygon                -> jlang::Polygon*    (npoints, xpoints, ypoints)
//
// Polygon::contains() reproduces java.awt.Polygon.contains(double, double) bit for bit: the
// bounding-box pre-check (Rectangle2D.contains with the int-wrapped width/height), the
// crossing-number walk with its half-open edge rules and the same double arithmetic.
// Rectangle::add/union_/intersection/intersects/contains use the JDK's overflow handling.
// `union` is a C++ keyword: Java r.union(s) -> r->union_(s) (CONVENTIONS §3.4).
#pragma once

#include <jlang/Array.h>
#include <jlang/Object.h>
#include <jlang/String.h>

#include <cstdint>

namespace jlang {

class Rectangle;

// ---------------------------------------------------------------------------------------
// java.awt.Point
class Point : public virtual Object {
public:
    int32_t x = 0;
    int32_t y = 0;

    Point() {}
    Point(int32_t x, int32_t y) : x(x), y(y) {}
    explicit Point(Point* p);  // new Point(Point p)

    double getX() { return x; }
    double getY() { return y; }
    Point* getLocation() { return new Point(x, y); }
    void setLocation(Point* p);
    void setLocation(int32_t x, int32_t y) { this->x = x; this->y = y; }
    // Rounds like Java: (int) Math.floor(v + 0.5) with Java's saturating cast.
    void setLocation(double x, double y);
    void move(int32_t x, int32_t y) { setLocation(x, y); }
    void translate(int32_t dx, int32_t dy) { x += dx; y += dy; }

    double distanceSq(double px, double py);
    double distanceSq(Point* p);
    double distance(double px, double py);
    double distance(Point* p);
    static double distanceSq(double x1, double y1, double x2, double y2);
    static double distance(double x1, double y1, double x2, double y2);

    // Point.equals: same x/y for another Point; otherwise identity (Object.equals).
    bool equals(Object* o) override;
    // Point2D.hashCode (doubleToLongBits of x and y).
    int32_t hashCode() override;
    // "java.awt.Point[x=1,y=2]"
    String toString() override;
    Point* clone() override { return new Point(x, y); }
};

// ---------------------------------------------------------------------------------------
// java.awt.Rectangle (also used for java.awt.geom.RectangularShape / Rectangle2D calls).
class Rectangle : public virtual Object {
public:
    int32_t x = 0;
    int32_t y = 0;
    int32_t width = 0;
    int32_t height = 0;

    Rectangle() {}
    Rectangle(int32_t x, int32_t y, int32_t width, int32_t height)
        : x(x), y(y), width(width), height(height) {}
    Rectangle(int32_t width, int32_t height) : width(width), height(height) {}
    explicit Rectangle(Rectangle* r);
    explicit Rectangle(Point* p);  // new Rectangle(Point): location p, size 0x0

    // RectangularShape / Rectangle2D accessors (double results, as in Java).
    double getX() { return x; }
    double getY() { return y; }
    double getWidth() { return width; }
    double getHeight() { return height; }
    double getMinX() { return getX(); }
    double getMinY() { return getY(); }
    double getMaxX() { return getX() + getWidth(); }
    double getMaxY() { return getY() + getHeight(); }
    double getCenterX() { return getX() + getWidth() / 2.0; }
    double getCenterY() { return getY() + getHeight() / 2.0; }

    Rectangle* getBounds() { return new Rectangle(x, y, width, height); }
    Point* getLocation() { return new Point(x, y); }
    void setBounds(Rectangle* r);
    void setBounds(int32_t x, int32_t y, int32_t width, int32_t height);
    void setRect(double x, double y, double width, double height);  // Rectangle.setRect rounding
    void setLocation(Point* p);
    void setLocation(int32_t x, int32_t y);
    void setSize(int32_t width, int32_t height);
    void translate(int32_t dx, int32_t dy);  // with Java's overflow clamping
    void grow(int32_t h, int32_t v);         // with Java's overflow clamping

    // Rectangle.contains / inside (int) and Rectangle2D.contains (double): half-open.
    bool contains(Point* p);
    bool contains(int32_t x, int32_t y);
    bool inside(int32_t x, int32_t y) { return contains(x, y); }
    bool contains(double x, double y);
    bool contains(Rectangle* r);
    bool contains(int32_t X, int32_t Y, int32_t W, int32_t H);
    bool intersects(Rectangle* r);
    bool intersects(double x, double y, double w, double h);  // Rectangle2D.intersects
    Rectangle* intersection(Rectangle* r);
    Rectangle* union_(Rectangle* r);
    void add(int32_t newx, int32_t newy);
    void add(Point* pt);
    void add(Rectangle* r);
    bool isEmpty() { return width <= 0 || height <= 0; }

    bool equals(Object* o) override;
    int32_t hashCode() override;  // Rectangle2D.hashCode
    // "java.awt.Rectangle[x=0,y=0,width=0,height=0]"
    String toString() override;
    Rectangle* clone() override { return new Rectangle(x, y, width, height); }
};

// ---------------------------------------------------------------------------------------
// java.awt.Polygon
class Polygon : public virtual Object {
public:
    int32_t npoints = 0;
    Array<int32_t>* xpoints = nullptr;
    Array<int32_t>* ypoints = nullptr;

    Polygon();
    // Copies the first npoints coordinates (IndexOutOfBoundsException / NegativeArraySizeException
    // like Java).
    Polygon(Array<int32_t>* xpoints, Array<int32_t>* ypoints, int32_t npoints);

    void reset();
    void invalidate();
    void translate(int32_t deltaX, int32_t deltaY);
    void addPoint(int32_t x, int32_t y);

    Rectangle* getBounds();       // a copy of the bounding box
    Rectangle* getBoundingBox();  // deprecated Java name, same result

    bool contains(Point* p);
    bool contains(int32_t x, int32_t y);
    bool inside(int32_t x, int32_t y) { return contains(x, y); }
    bool contains(double x, double y);

    String toString() override;

protected:
    Rectangle* bounds = nullptr;  // cached bounding box (Java field name)

private:
    void calculateBounds();
    void updateBounds(int32_t x, int32_t y);
};

}  // namespace jlang
