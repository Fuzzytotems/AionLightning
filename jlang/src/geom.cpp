// jlang/Geom.cpp - java.awt.Point / Rectangle / Polygon (OpenJDK algorithms).
#include <jlang/Geom.h>

#include <jlang/Exceptions.h>

#include <cmath>
#include <cstring>
#include <limits>

namespace jlang {

namespace {

constexpr int32_t kIntMin = std::numeric_limits<int32_t>::min();
constexpr int32_t kIntMax = std::numeric_limits<int32_t>::max();

// Java int arithmetic (wraps on overflow regardless of -fwrapv).
inline int32_t wadd(int32_t a, int32_t b) {
    return static_cast<int32_t>(static_cast<uint32_t>(a) + static_cast<uint32_t>(b));
}
inline int32_t wsub(int32_t a, int32_t b) {
    return static_cast<int32_t>(static_cast<uint32_t>(a) - static_cast<uint32_t>(b));
}

inline int64_t doubleToLongBits(double d) {
    if (std::isnan(d)) return INT64_C(0x7ff8000000000000);
    int64_t bits;
    std::memcpy(&bits, &d, sizeof bits);
    return bits;
}

// Java (int) cast of a double: NaN -> 0, saturating.
inline int32_t d2i(double v) {
    if (std::isnan(v)) return 0;
    if (v >= 2147483647.0) return kIntMax;
    if (v <= -2147483648.0) return kIntMin;
    return static_cast<int32_t>(v);
}

inline int32_t hashBits(int64_t bits) {
    return static_cast<int32_t>(bits) ^ static_cast<int32_t>(bits >> 32);
}

String hexHash(int32_t h) {
    static const char* digits = "0123456789abcdef";
    uint32_t u = static_cast<uint32_t>(h);
    char buf[9];
    int n = 0;
    do {
        buf[n++] = digits[u & 15];
        u >>= 4;
    } while (u != 0);
    std::string s;
    while (n > 0) s.push_back(buf[--n]);
    return String(s);
}

// Rectangle.setRect helper.
int32_t clip(double v, bool doceil) {
    if (v <= kIntMin) return kIntMin;
    if (v >= kIntMax) return kIntMax;
    return d2i(doceil ? std::ceil(v) : std::floor(v));
}

}  // namespace

// =======================================================================================
// Point

Point::Point(Point* p) : x(p->x), y(p->y) {}

void Point::setLocation(Point* p) { setLocation(p->x, p->y); }

void Point::setLocation(double px, double py) {
    x = d2i(std::floor(px + 0.5));
    y = d2i(std::floor(py + 0.5));
}

double Point::distanceSq(double x1, double y1, double x2, double y2) {
    x1 -= x2;
    y1 -= y2;
    return x1 * x1 + y1 * y1;
}

double Point::distance(double x1, double y1, double x2, double y2) {
    x1 -= x2;
    y1 -= y2;
    return std::sqrt(x1 * x1 + y1 * y1);
}

double Point::distanceSq(double px, double py) {
    px -= getX();
    py -= getY();
    return px * px + py * py;
}

double Point::distanceSq(Point* p) {
    double px = p->getX() - getX();
    double py = p->getY() - getY();
    return px * px + py * py;
}

double Point::distance(double px, double py) {
    px -= getX();
    py -= getY();
    return std::sqrt(px * px + py * py);
}

double Point::distance(Point* p) {
    double px = p->getX() - getX();
    double py = p->getY() - getY();
    return std::sqrt(px * px + py * py);
}

bool Point::equals(Object* o) {
    if (Point* pt = dynamic_cast<Point*>(o)) return x == pt->x && y == pt->y;
    return Object::equals(o);
}

int32_t Point::hashCode() {
    int64_t bits = doubleToLongBits(getX());
    bits ^= static_cast<int64_t>(static_cast<uint64_t>(doubleToLongBits(getY())) * 31u);
    return hashBits(bits);
}

String Point::toString() { return str("java.awt.Point[x=", x, ",y=", y, "]"); }

// =======================================================================================
// Rectangle

Rectangle::Rectangle(Rectangle* r) : x(r->x), y(r->y), width(r->width), height(r->height) {}
Rectangle::Rectangle(Point* p) : x(p->x), y(p->y) {}

void Rectangle::setBounds(Rectangle* r) { setBounds(r->x, r->y, r->width, r->height); }

void Rectangle::setBounds(int32_t nx, int32_t ny, int32_t nw, int32_t nh) {
    x = nx;
    y = ny;
    width = nw;
    height = nh;
}

void Rectangle::setRect(double rx, double ry, double rw, double rh) {
    int32_t newx, newy, neww, newh;
    if (rx > 2.0 * kIntMax) {
        newx = kIntMax;
        neww = -1;
    } else {
        newx = clip(rx, false);
        if (rw >= 0) rw += rx - newx;
        neww = clip(rw, rw >= 0);
    }
    if (ry > 2.0 * kIntMax) {
        newy = kIntMax;
        newh = -1;
    } else {
        newy = clip(ry, false);
        if (rh >= 0) rh += ry - newy;
        newh = clip(rh, rh >= 0);
    }
    setBounds(newx, newy, neww, newh);
}

void Rectangle::setLocation(Point* p) { setLocation(p->x, p->y); }

void Rectangle::setLocation(int32_t nx, int32_t ny) {
    x = nx;
    y = ny;
}

void Rectangle::setSize(int32_t w, int32_t h) {
    width = w;
    height = h;
}

void Rectangle::translate(int32_t dx, int32_t dy) {
    int32_t oldv = x;
    int32_t newv = wadd(oldv, dx);
    if (dx < 0) {
        if (newv > oldv) {
            if (width >= 0) width = wadd(width, wsub(newv, kIntMin));
            newv = kIntMin;
        }
    } else {
        if (newv < oldv) {
            if (width >= 0) {
                width = wadd(width, wsub(newv, kIntMax));
                if (width < 0) width = kIntMax;
            }
            newv = kIntMax;
        }
    }
    x = newv;

    oldv = y;
    newv = wadd(oldv, dy);
    if (dy < 0) {
        if (newv > oldv) {
            if (height >= 0) height = wadd(height, wsub(newv, kIntMin));
            newv = kIntMin;
        }
    } else {
        if (newv < oldv) {
            if (height >= 0) {
                height = wadd(height, wsub(newv, kIntMax));
                if (height < 0) height = kIntMax;
            }
            newv = kIntMax;
        }
    }
    y = newv;
}

void Rectangle::grow(int32_t h, int32_t v) {
    int64_t x0 = x, y0 = y, x1 = width, y1 = height;
    x1 += x0;
    y1 += y0;
    x0 -= h;
    y0 -= v;
    x1 += h;
    y1 += v;
    if (x1 < x0) {
        x1 -= x0;
        if (x1 < kIntMin) x1 = kIntMin;
        if (x0 < kIntMin) x0 = kIntMin;
        else if (x0 > kIntMax) x0 = kIntMax;
    } else {
        if (x0 < kIntMin) x0 = kIntMin;
        else if (x0 > kIntMax) x0 = kIntMax;
        x1 -= x0;
        if (x1 < kIntMin) x1 = kIntMin;
        else if (x1 > kIntMax) x1 = kIntMax;
    }
    if (y1 < y0) {
        y1 -= y0;
        if (y1 < kIntMin) y1 = kIntMin;
        if (y0 < kIntMin) y0 = kIntMin;
        else if (y0 > kIntMax) y0 = kIntMax;
    } else {
        if (y0 < kIntMin) y0 = kIntMin;
        else if (y0 > kIntMax) y0 = kIntMax;
        y1 -= y0;
        if (y1 < kIntMin) y1 = kIntMin;
        else if (y1 > kIntMax) y1 = kIntMax;
    }
    setBounds(static_cast<int32_t>(x0), static_cast<int32_t>(y0), static_cast<int32_t>(x1),
              static_cast<int32_t>(y1));
}

bool Rectangle::contains(Point* p) { return contains(p->x, p->y); }

bool Rectangle::contains(int32_t X, int32_t Y) {
    int32_t w = width;
    int32_t h = height;
    if ((w | h) < 0) return false;
    int32_t rx = x;
    int32_t ry = y;
    if (X < rx || Y < ry) return false;
    w = wadd(w, rx);
    h = wadd(h, ry);
    //    overflow || intersect
    return (w < rx || w > X) && (h < ry || h > Y);
}

bool Rectangle::contains(double px, double py) {
    double x0 = getX();
    double y0 = getY();
    return px >= x0 && py >= y0 && px < x0 + getWidth() && py < y0 + getHeight();
}

bool Rectangle::contains(Rectangle* r) { return contains(r->x, r->y, r->width, r->height); }

bool Rectangle::contains(int32_t X, int32_t Y, int32_t W, int32_t H) {
    int32_t w = width;
    int32_t h = height;
    if ((w | h | W | H) < 0) return false;
    int32_t rx = x;
    int32_t ry = y;
    if (X < rx || Y < ry) return false;
    w = wadd(w, rx);
    W = wadd(W, X);
    if (W <= X) {
        if (w >= rx || W > w) return false;
    } else {
        if (w >= rx && W > w) return false;
    }
    h = wadd(h, ry);
    H = wadd(H, Y);
    if (H <= Y) {
        if (h >= ry || H > h) return false;
    } else {
        if (h >= ry && H > h) return false;
    }
    return true;
}

bool Rectangle::intersects(Rectangle* r) {
    int32_t tw = width;
    int32_t th = height;
    int32_t rw = r->width;
    int32_t rh = r->height;
    if (rw <= 0 || rh <= 0 || tw <= 0 || th <= 0) return false;
    int32_t tx = x;
    int32_t ty = y;
    int32_t rx = r->x;
    int32_t ry = r->y;
    rw = wadd(rw, rx);
    rh = wadd(rh, ry);
    tw = wadd(tw, tx);
    th = wadd(th, ty);
    //      overflow || intersect
    return (rw < rx || rw > tx) && (rh < ry || rh > ty) && (tw < tx || tw > rx) &&
           (th < ty || th > ry);
}

bool Rectangle::intersects(double px, double py, double w, double h) {
    if (isEmpty() || w <= 0 || h <= 0) return false;
    double x0 = getX();
    double y0 = getY();
    return px + w > x0 && py + h > y0 && px < x0 + getWidth() && py < y0 + getHeight();
}

Rectangle* Rectangle::intersection(Rectangle* r) {
    int32_t tx1 = x;
    int32_t ty1 = y;
    int32_t rx1 = r->x;
    int32_t ry1 = r->y;
    int64_t tx2 = tx1;
    tx2 += width;
    int64_t ty2 = ty1;
    ty2 += height;
    int64_t rx2 = rx1;
    rx2 += r->width;
    int64_t ry2 = ry1;
    ry2 += r->height;
    if (tx1 < rx1) tx1 = rx1;
    if (ty1 < ry1) ty1 = ry1;
    if (tx2 > rx2) tx2 = rx2;
    if (ty2 > ry2) ty2 = ry2;
    tx2 -= tx1;
    ty2 -= ty1;
    if (tx2 < kIntMin) tx2 = kIntMin;
    if (ty2 < kIntMin) ty2 = kIntMin;
    return new Rectangle(tx1, ty1, static_cast<int32_t>(tx2), static_cast<int32_t>(ty2));
}

Rectangle* Rectangle::union_(Rectangle* r) {
    int64_t tx2 = width;
    int64_t ty2 = height;
    if ((tx2 | ty2) < 0) return new Rectangle(r);
    int64_t rx2 = r->width;
    int64_t ry2 = r->height;
    if ((rx2 | ry2) < 0) return new Rectangle(this);
    int32_t tx1 = x;
    int32_t ty1 = y;
    tx2 += tx1;
    ty2 += ty1;
    int32_t rx1 = r->x;
    int32_t ry1 = r->y;
    rx2 += rx1;
    ry2 += ry1;
    if (tx1 > rx1) tx1 = rx1;
    if (ty1 > ry1) ty1 = ry1;
    if (tx2 < rx2) tx2 = rx2;
    if (ty2 < ry2) ty2 = ry2;
    tx2 -= tx1;
    ty2 -= ty1;
    if (tx2 > kIntMax) tx2 = kIntMax;
    if (ty2 > kIntMax) ty2 = kIntMax;
    return new Rectangle(tx1, ty1, static_cast<int32_t>(tx2), static_cast<int32_t>(ty2));
}

void Rectangle::add(int32_t newx, int32_t newy) {
    if ((width | height) < 0) {
        x = newx;
        y = newy;
        width = height = 0;
        return;
    }
    int32_t x1 = x;
    int32_t y1 = y;
    int64_t x2 = width;
    int64_t y2 = height;
    x2 += x1;
    y2 += y1;
    if (x1 > newx) x1 = newx;
    if (y1 > newy) y1 = newy;
    if (x2 < newx) x2 = newx;
    if (y2 < newy) y2 = newy;
    x2 -= x1;
    y2 -= y1;
    if (x2 > kIntMax) x2 = kIntMax;
    if (y2 > kIntMax) y2 = kIntMax;
    setBounds(x1, y1, static_cast<int32_t>(x2), static_cast<int32_t>(y2));
}

void Rectangle::add(Point* pt) { add(pt->x, pt->y); }

void Rectangle::add(Rectangle* r) {
    int64_t tx2 = width;
    int64_t ty2 = height;
    if ((tx2 | ty2) < 0) setBounds(r->x, r->y, r->width, r->height);
    int64_t rx2 = r->width;
    int64_t ry2 = r->height;
    if ((rx2 | ry2) < 0) return;
    int32_t tx1 = x;
    int32_t ty1 = y;
    tx2 += tx1;
    ty2 += ty1;
    int32_t rx1 = r->x;
    int32_t ry1 = r->y;
    rx2 += rx1;
    ry2 += ry1;
    if (tx1 > rx1) tx1 = rx1;
    if (ty1 > ry1) ty1 = ry1;
    if (tx2 < rx2) tx2 = rx2;
    if (ty2 < ry2) ty2 = ry2;
    tx2 -= tx1;
    ty2 -= ty1;
    if (tx2 > kIntMax) tx2 = kIntMax;
    if (ty2 > kIntMax) ty2 = kIntMax;
    setBounds(tx1, ty1, static_cast<int32_t>(tx2), static_cast<int32_t>(ty2));
}

bool Rectangle::equals(Object* o) {
    if (Rectangle* r = dynamic_cast<Rectangle*>(o))
        return x == r->x && y == r->y && width == r->width && height == r->height;
    return Object::equals(o);
}

int32_t Rectangle::hashCode() {
    uint64_t bits = static_cast<uint64_t>(doubleToLongBits(getX()));
    bits += static_cast<uint64_t>(doubleToLongBits(getY())) * 37u;
    bits += static_cast<uint64_t>(doubleToLongBits(getWidth())) * 43u;
    bits += static_cast<uint64_t>(doubleToLongBits(getHeight())) * 47u;
    return hashBits(static_cast<int64_t>(bits));
}

String Rectangle::toString() {
    return str("java.awt.Rectangle[x=", x, ",y=", y, ",width=", width, ",height=", height, "]");
}

// =======================================================================================
// Polygon

namespace {
constexpr int32_t kMinLength = 4;  // java.awt.Polygon.MIN_LENGTH

Array<int32_t>* copyOf(Array<int32_t>* a, int32_t n) {
    auto* r = new Array<int32_t>(n);
    int32_t m = a->length < n ? a->length : n;
    if (m > 0) std::memcpy(r->data(), a->data(), sizeof(int32_t) * static_cast<size_t>(m));
    return r;
}
}  // namespace

Polygon::Polygon() {
    xpoints = new Array<int32_t>(kMinLength);
    ypoints = new Array<int32_t>(kMinLength);
}

Polygon::Polygon(Array<int32_t>* xs, Array<int32_t>* ys, int32_t n) {
    if (xs == nullptr || ys == nullptr) throw NullPointerException();
    if (n > xs->length || n > ys->length)
        throw IndexOutOfBoundsException(String("npoints > xpoints.length || npoints > ypoints.length"));
    if (n < 0) throw NegativeArraySizeException(String("npoints < 0"));
    npoints = n;
    xpoints = copyOf(xs, n);
    ypoints = copyOf(ys, n);
}

void Polygon::reset() {
    npoints = 0;
    bounds = nullptr;
}

void Polygon::invalidate() { bounds = nullptr; }

void Polygon::translate(int32_t deltaX, int32_t deltaY) {
    for (int32_t i = 0; i < npoints; i++) {
        (*xpoints)[i] = wadd((*xpoints)[i], deltaX);
        (*ypoints)[i] = wadd((*ypoints)[i], deltaY);
    }
    if (bounds != nullptr) bounds->translate(deltaX, deltaY);
}

void Polygon::calculateBounds() {
    int32_t minX = kIntMax, minY = kIntMax, maxX = kIntMin, maxY = kIntMin;
    const int32_t* xs = xpoints->data();
    const int32_t* ys = ypoints->data();
    for (int32_t i = 0; i < npoints; i++) {
        int32_t px = xs[i];
        if (px < minX) minX = px;
        if (px > maxX) maxX = px;
        int32_t py = ys[i];
        if (py < minY) minY = py;
        if (py > maxY) maxY = py;
    }
    bounds = new Rectangle(minX, minY, wsub(maxX, minX), wsub(maxY, minY));
}

void Polygon::updateBounds(int32_t px, int32_t py) {
    Rectangle* b = bounds;
    if (px < b->x) {
        b->width = wadd(b->width, wsub(b->x, px));
        b->x = px;
    } else {
        int32_t d = wsub(px, b->x);
        if (d > b->width) b->width = d;
    }
    if (py < b->y) {
        b->height = wadd(b->height, wsub(b->y, py));
        b->y = py;
    } else {
        int32_t d = wsub(py, b->y);
        if (d > b->height) b->height = d;
    }
}

void Polygon::addPoint(int32_t px, int32_t py) {
    if (npoints >= xpoints->length || npoints >= ypoints->length) {
        int32_t newLength = npoints * 2;
        if (newLength < kMinLength) {
            newLength = kMinLength;
        } else if ((newLength & (newLength - 1)) != 0) {
            // Integer.highestOneBit(newLength)
            uint32_t v = static_cast<uint32_t>(newLength);
            uint32_t hb = 1u;
            while ((v >>= 1) != 0) hb <<= 1;
            newLength = static_cast<int32_t>(hb);
        }
        xpoints = copyOf(xpoints, newLength);
        ypoints = copyOf(ypoints, newLength);
    }
    (*xpoints)[npoints] = px;
    (*ypoints)[npoints] = py;
    npoints++;
    if (bounds != nullptr) updateBounds(px, py);
}

Rectangle* Polygon::getBoundingBox() {
    if (npoints == 0) return new Rectangle();
    if (bounds == nullptr) calculateBounds();
    return bounds->getBounds();
}

Rectangle* Polygon::getBounds() { return getBoundingBox(); }

bool Polygon::contains(Point* p) { return contains(p->x, p->y); }

bool Polygon::contains(int32_t px, int32_t py) {
    return contains(static_cast<double>(px), static_cast<double>(py));
}

bool Polygon::contains(double px, double py) {
    if (npoints <= 2 || !getBoundingBox()->contains(px, py)) return false;
    int32_t hits = 0;
    const int32_t* xs = xpoints->data();
    const int32_t* ys = ypoints->data();
    int32_t lastx = xs[npoints - 1];
    int32_t lasty = ys[npoints - 1];
    int32_t curx, cury;
    // Walk the edges of the polygon
    for (int32_t i = 0; i < npoints; lastx = curx, lasty = cury, i++) {
        curx = xs[i];
        cury = ys[i];
        if (cury == lasty) continue;
        int32_t leftx;
        if (curx < lastx) {
            if (px >= lastx) continue;
            leftx = curx;
        } else {
            if (px >= curx) continue;
            leftx = lastx;
        }
        double test1, test2;
        if (cury < lasty) {
            if (py < cury || py >= lasty) continue;
            if (px < leftx) {
                hits++;
                continue;
            }
            test1 = px - curx;
            test2 = py - cury;
        } else {
            if (py < lasty || py >= cury) continue;
            if (px < leftx) {
                hits++;
                continue;
            }
            test1 = px - lastx;
            test2 = py - lasty;
        }
        if (test1 < (test2 / static_cast<double>(wsub(lasty, cury)) *
                     static_cast<double>(wsub(lastx, curx)))) {
            hits++;
        }
    }
    return (hits & 1) != 0;
}

String Polygon::toString() { return str("java.awt.Polygon@", hexHash(hashCode())); }

}  // namespace jlang
