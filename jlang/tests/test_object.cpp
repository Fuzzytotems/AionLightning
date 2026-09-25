// jlang::Object, Class, casts, monitors (JSYNC, wait/notify), boxing, Runnable.
#include "jtest.h"

#include <atomic>
#include <chrono>
#include <thread>

using jlang::Class;
using jlang::Object;
using jlang::String;

namespace org::openaion::test {

class Shape : public virtual Object {
public:
    virtual int32_t sides() = 0;
};
class Named : public virtual Object {
public:
    virtual String name() { return "named"; }
};
class Square : public Shape, public virtual Named {
public:
    int32_t sides() override { return 4; }
};
class Unrelated : public virtual Object {};
class ToRename : public virtual Object {};
class SM_SYSTEM_MESSAGE : public virtual Object {};

class Point : public virtual Object {
public:
    Point(int32_t x, int32_t y) : x(x), y(y) {}
    int32_t hashCode() override { return 31 * x + y; }
    bool equals(Object* o) override {
        auto* p = jlang::cast<Point>(o);  // throws ClassCastException for non-points
        return p != nullptr && p->x == x && p->y == y;
    }
    int32_t x, y;
};

class Plugin : public virtual Object {
public:
    String toString() override { return "plugin"; }
};

class Direction final {
public:
    enum class Value : int32_t { _NULL = -1, NORTH, SOUTH };
    static const Direction NORTH, SOUTH;
    constexpr Direction() = default;
    constexpr Direction(std::nullptr_t) {}
    constexpr explicit Direction(Value v) : v_(v) {}
    constexpr operator Value() const { return v_; }
    int32_t ordinal() const { return static_cast<int32_t>(v_); }
    String name() const { return v_ == Value::NORTH ? String("NORTH") : String("SOUTH"); }
    String toString() const { return name(); }
    friend bool operator==(Direction a, Direction b) { return a.v_ == b.v_; }
    friend bool operator==(Direction a, std::nullptr_t) { return a.v_ == Value::_NULL; }

private:
    Value v_ = Value::_NULL;
};
inline const Direction Direction::NORTH{Direction::Value::NORTH};
inline const Direction Direction::SOUTH{Direction::Value::SOUTH};

}  // namespace org::openaion::test

using namespace org::openaion::test;

JTEST(Object_IdentityDefaults) {
    Object* a = new Object();
    Object* b = new Object();
    JCHECK(a->equals(a));
    JCHECK(!a->equals(b));
    JCHECK_EQ(a->hashCode(), a->hashCode());
    JCHECK_EQ(a->hashCode(), jlang::identityHashCode(a));
    JCHECK_EQ(jlang::System::identityHashCode(a), a->identityHashCode());
    JCHECK_EQ(jlang::identityHashCode(nullptr), 0);
    Square* sq = new Square();
    Shape* asShape = sq;
    Named* asNamed = sq;
    // one Object subobject: the same identity through every interface
    JCHECK_EQ(static_cast<Object*>(asShape), static_cast<Object*>(asNamed));
    JCHECK_EQ(jlang::identityHashCode(asShape), jlang::identityHashCode(asNamed));
    String ts = a->toString();
    JCHECK(ts.startsWith("java.lang.Object@"));
    JCHECK_EQ(ts.substring(17), jlang::Integer::toHexString(a->hashCode()));
    JCHECK(sq->toString().startsWith("org.openaion.test.Square@"));
    JCHECK_THROWS(jlang::CloneNotSupportedException, a->clone());
    // copies get their own monitor and identity
    Point p(1, 2);
    Point q = p;
    JCHECK(p.equals(&q));
    JCHECK(&p != &q);
}

JTEST(Class_NamesAndIdentity) {
    JCHECK_EQ(Class::of<Square>()->getName(), String("org.openaion.test.Square"));
    JCHECK_EQ(Class::of<Square>()->getSimpleName(), String("Square"));
    JCHECK_EQ(Class::of<SM_SYSTEM_MESSAGE>()->getSimpleName(), String("SM_SYSTEM_MESSAGE"));
    JCHECK_EQ(Class::of<Object>()->getName(), String("java.lang.Object"));
    JCHECK_EQ(Class::of<jlang::Integer>()->getName(), String("java.lang.Integer"));
    JCHECK_EQ(Class::of<jlang::IllegalStateException>()->getName(), String("java.lang.IllegalStateException"));
    JCHECK_EQ(Class::of<jlang::SQLException>()->getName(), String("java.sql.SQLException"));
    JCHECK_EQ(Class::of<int32_t>()->getName(), String("int"));
    JCHECK_EQ(jlang::Integer::TYPE, Class::of<int32_t>());
    JCHECK_EQ(Class::of<bool>()->getName(), String("boolean"));
    JCHECK_EQ(Class::of<int8_t>()->getName(), String("byte"));
    JCHECK_EQ(Class::of<char16_t>()->getName(), String("char"));
    JCHECK(Class::of<int64_t>()->isPrimitive());
    JCHECK(!Class::of<Square>()->isPrimitive());
    Square* sq = new Square();
    Shape* sh = sq;
    JCHECK_EQ(sh->getClass(), Class::of<Square>());
    JCHECK_EQ(sq->getClass(), (new Square())->getClass());
    JCHECK_EQ(Class::of<Square>()->toString(), String("class org.openaion.test.Square"));
    JCHECK_EQ(Class::forName("org.openaion.test.Square"), Class::of<Square>());
    JCHECK(Class::findClass("no.such.Clazz") == nullptr);
    JCHECK_THROWS(jlang::ClassNotFoundException, Class::forName("no.such.Clazz"));
    // exact isInstance through dynamic_cast for Class::of<T>()
    JCHECK(Class::of<Shape>()->isInstance(sq));
    JCHECK(Class::of<Named>()->isInstance(sq));
    JCHECK(!Class::of<Unrelated>()->isInstance(sq));
    JCHECK(!Class::of<Shape>()->isInstance(nullptr));
    JCHECK(Class::of<Object>()->isAssignableFrom(Class::of<Square>()));
    JCHECK(!Class::of<Object>()->isAssignableFrom(Class::of<int32_t>()));
    JCHECK(String(Class::of<jlang::detail::RunnableLambda<void (*)()>>()->getName()).startsWith("jlang.detail.RunnableLambda"));
}

namespace {
Object* makePlugin() { return new Plugin(); }
}  // namespace

JTEST(Class_Registry) {
    Class* shape = Class::registerClass<Shape>("org.openaion.test.Shape", Class::of<Object>(), {}, Class::ABSTRACT, nullptr);
    Class* named = Class::registerClass<Named>("org.openaion.test.Named", nullptr, {}, Class::INTERFACE, nullptr);
    String root("data/scripts/system/handlers");
    Class* square = Class::registerClass<Square>("org.openaion.test.Square", shape, {named}, Class::NONE,
                                                 [] { return static_cast<Object*>(new Square()); },
                                                 {{"QuestHandler", {{"value", "1000"}, {"name", "q"}}}}, &root);
    Class* plugin = Class::registerClass<Plugin>("org.openaion.test.Outer$Plugin", nullptr, {shape}, Class::NONE, &makePlugin,
                                                 {{"org.openaion.anno.Marker", {}}});
    JCHECK(square->isRegistered());
    JCHECK(shape->isAbstract());
    JCHECK(named->isInterface());
    JCHECK(named->isAbstract());
    JCHECK(!square->isAbstract());
    JCHECK_EQ(square->getSuperclass(), shape);
    JCHECK_EQ(square->getInterfaces().size(), 1u);
    JCHECK(shape->isAssignableFrom(square));
    JCHECK(named->isAssignableFrom(square));
    JCHECK(!square->isAssignableFrom(shape));
    JCHECK(shape->isAssignableFrom(plugin));  // via interfaces metadata
    JCHECK(!named->isAssignableFrom(plugin));
    JCHECK_EQ(square->getScriptRoot(), root);
    JCHECK(plugin->getScriptRoot().isNull());
    auto* anno = square->getAnnotation("QuestHandler");
    JCHECK(anno != nullptr);
    JCHECK_EQ(anno->at("value"), String("1000"));
    JCHECK(square->isAnnotationPresent("org.whatever.QuestHandler"));
    JCHECK(plugin->isAnnotationPresent("Marker"));
    JCHECK(!plugin->isAnnotationPresent("QuestHandler"));
    // nested-class name from the registry
    JCHECK_EQ(plugin->getName(), String("org.openaion.test.Outer$Plugin"));
    JCHECK_EQ(plugin->getSimpleName(), String("Plugin"));
    JCHECK_EQ(plugin->getCanonicalName(), String("org.openaion.test.Outer.Plugin"));
    JCHECK_EQ(Class::forName("org.openaion.test.Outer$Plugin"), plugin);
    Object* inst = square->newInstance();
    JCHECK(jlang::instanceof<Square>(inst));
    JCHECK_EQ(Class::forName("org.openaion.test.Outer$Plugin")->newInstance()->toString(), String("plugin"));
    JCHECK_THROWS(jlang::InstantiationException, shape->newInstance());
    JCHECK_THROWS(jlang::InstantiationException, Class::of<Unrelated>()->newInstance());
    bool found = false;
    for (Class* c : Class::getRegisteredClasses()) found = found || c == square;
    JCHECK(found);
    Class::of<ToRename>()->setName("org.openaion.test.Renamed")->setAbstract(true)->addInterface(named);
    JCHECK_EQ(Class::forName("org.openaion.test.Renamed"), Class::of<ToRename>());
    JCHECK(named->isAssignableFrom(Class::of<ToRename>()));
    JCHECK(Class::findClass("org.openaion.test.ToRename") == nullptr);
}

JTEST(Cast_Instanceof) {
    Square* sq = new Square();
    Object* o = sq;
    JCHECK(jlang::instanceof<Shape>(o));
    JCHECK(jlang::instanceof<Named>(o));
    JCHECK(jlang::instanceof<Square>(static_cast<Named*>(sq)));
    JCHECK(!jlang::instanceof<Unrelated>(o));
    JCHECK(!jlang::instanceof<Shape>(static_cast<Object*>(nullptr)));
    JCHECK(!jlang::instanceof<Shape>(nullptr));
    JCHECK_EQ(jlang::cast<Shape>(o)->sides(), 4);
    JCHECK_EQ(jlang::cast<Square>(static_cast<Named*>(sq)), sq);
    JCHECK(jlang::cast<Shape>(static_cast<Object*>(nullptr)) == nullptr);
    JCHECK_EQ(jlang::cast<Object>(sq), o);  // upcast
    try {
        jlang::cast<Unrelated>(o);
        JCHECK(false);
    } catch (jlang::ClassCastException& e) {
        JCHECK_EQ(e.getMessage(),
                  String("class org.openaion.test.Square cannot be cast to class org.openaion.test.Unrelated"));
    }
    Point* p = new Point(1, 2);
    JCHECK(p->equals(new Point(1, 2)));
    JCHECK_THROWS(jlang::ClassCastException, p->equals(sq));
}

JTEST(Monitor_Reentrancy) {
    Object* lock = new Object();
    int counter = 0;
    JSYNC(lock) {
        counter++;
        JSYNC(lock) {
            counter++;
            JSYNC(lock) { counter++; }
        }
        JCHECK(lock->monitorHeldByCurrentThread());
    }
    JCHECK(!lock->monitorHeldByCurrentThread());
    JCHECK_EQ(counter, 3);
    // released on exceptions and on break/continue/return
    try {
        JSYNC(lock) { throw jlang::IllegalStateException("x"); }
    } catch (jlang::IllegalStateException&) {
    }
    JCHECK(!lock->monitorHeldByCurrentThread());
    for (int i = 0; i < 3; i++) {
        JSYNC(lock) {
            if (i == 0) continue;
            break;
        }
    }
    JCHECK(!lock->monitorHeldByCurrentThread());
    auto f = [&]() -> int {
        JSYNC(lock) { return 5; }
        return 0;
    };
    JCHECK_EQ(f(), 5);
    JCHECK(!lock->monitorHeldByCurrentThread());
    // if/else binding: JSYNC must not capture a following else
    bool elseRan = false;
    if (counter == 0)
        JSYNC(lock) { counter = 100; }
    else
        elseRan = true;
    JCHECK(elseRan);
    // interfaces, classes and null
    Square* sq = new Square();
    Named* asNamed = sq;
    JSYNC(asNamed) { JCHECK(sq->monitorHeldByCurrentThread()); }
    JSYNC(Class::of<Square>()) { counter++; }
    Object* nul = nullptr;
    JCHECK_THROWS(jlang::NullPointerException, [&] { JSYNC(nul) {} }());
    JCHECK_THROWS(jlang::IllegalMonitorStateException, lock->notify());
    JCHECK_THROWS(jlang::IllegalMonitorStateException, lock->wait());
    JCHECK_THROWS(jlang::IllegalMonitorStateException, lock->monitorExit());
    // stack objects have monitors too (freed by ~Object)
    Object local;
    JSYNC(&local) { counter++; }
}

namespace {
struct ThreadArgs {
    std::function<void()> fn;
};
void runThread(void* p) { static_cast<ThreadArgs*>(p)->fn(); }
uint64_t startThread(std::function<void()> fn) {
    return jlang::gc::startNativeThread(&runThread, new ThreadArgs{std::move(fn)});
}
}  // namespace

JTEST(Monitor_MutualExclusion) {
    Object* lock = new Object();
    int64_t counter = 0;  // protected by lock
    std::vector<uint64_t> threads;
    for (int t = 0; t < 4; t++) {
        threads.push_back(startThread([&] {
            for (int i = 0; i < 20000; i++) {
                JSYNC(lock) {
                    JSYNC(lock) { counter++; }
                }
            }
        }));
    }
    for (uint64_t t : threads) jlang::gc::joinNativeThread(t);
    JCHECK_EQ(counter, 80000);
}

JTEST(Monitor_WaitNotify) {
    // Producer/consumer through a one-slot buffer, with nested monitor recursion on wait.
    Object* lock = new Object();
    int slot = -1;
    int64_t sum = 0;
    const int N = 2000;
    uint64_t consumer = startThread([&] {
        for (int i = 0; i < N; i++) {
            JSYNC(lock) {
                JSYNC(lock) {  // wait must release all recursion levels
                    while (slot < 0) lock->wait();
                    sum += slot;
                    slot = -1;
                    lock->notifyAll();
                }
            }
        }
    });
    for (int i = 0; i < N; i++) {
        JSYNC(lock) {
            while (slot >= 0) lock->wait();
            slot = i;
            lock->notify();
            JCHECK(lock->monitorHeldByCurrentThread());
        }
    }
    jlang::gc::joinNativeThread(consumer);
    JCHECK_EQ(sum, static_cast<int64_t>(N) * (N - 1) / 2);

    // timed wait returns; the monitor is re-acquired
    auto t0 = std::chrono::steady_clock::now();
    JSYNC(lock) {
        lock->wait(30);
        JCHECK(lock->monitorHeldByCurrentThread());
    }
    JCHECK(std::chrono::steady_clock::now() - t0 >= std::chrono::milliseconds(25));
    JCHECK_THROWS(jlang::IllegalArgumentException, lock->wait(-1));
}

JTEST(Monitor_WaitInterrupt) {
    Object* lock = new Object();
    std::atomic<jlang::sync::InterruptState*> state{nullptr};
    std::atomic<int> outcome{0};
    uint64_t t = startThread([&] {
        state.store(jlang::sync::current());
        JSYNC(lock) {
            try {
                lock->wait(INT64_MAX);  // Long.MAX_VALUE: effectively forever, must not overflow
                outcome = 1;
            } catch (jlang::InterruptedException&) {
                outcome = lock->monitorHeldByCurrentThread() ? 2 : 3;
            }
        }
        JCHECK(!jlang::sync::interrupted());  // cleared by the exception
    });
    while (state.load() == nullptr) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    jlang::sync::interrupt(state.load());
    jlang::gc::joinNativeThread(t);
    JCHECK_EQ(outcome.load(), 2);

    // interrupt before sleeping
    jlang::sync::interrupt(jlang::sync::current());
    JCHECK(jlang::sync::isInterrupted(jlang::sync::current()));
    JCHECK_THROWS(jlang::InterruptedException, jlang::sync::sleep(1000));
    JCHECK(!jlang::sync::isInterrupted(jlang::sync::current()));
    jlang::sync::sleep(1);
    JCHECK_THROWS(jlang::IllegalArgumentException, jlang::sync::sleep(-1));
    // interrupted() test-and-clear
    jlang::sync::interrupt(jlang::sync::current());
    JCHECK(jlang::sync::interrupted());
    JCHECK(!jlang::sync::interrupted());
}

JTEST(Runnable_Lambda) {
    int hits = 0;
    jlang::Runnable* r = jlang::Runnable::of([&hits] { hits++; });
    r->run();
    r->run();
    JCHECK_EQ(hits, 2);
    JCHECK(jlang::instanceof<jlang::Runnable>(static_cast<Object*>(r)));
}

JTEST(Boxing) {
    using jlang::box;
    using jlang::unbox;
    Object* i = box(5);
    JCHECK(jlang::instanceof<jlang::Integer>(i));
    JCHECK_EQ(unbox<int32_t>(i), 5);
    JCHECK_EQ(box(5), box(5));  // cached like Integer.valueOf
    JCHECK(box(1000) != box(1000));
    JCHECK(box(1000)->equals(box(1000)));
    JCHECK_EQ(i->toString(), String("5"));
    JCHECK(jlang::instanceof<jlang::Long>(box(int64_t{5})));
    JCHECK(jlang::instanceof<jlang::Short>(box(int16_t{5})));
    JCHECK(jlang::instanceof<jlang::Byte>(box(int8_t{5})));
    JCHECK(jlang::instanceof<jlang::Float>(box(5.0f)));
    JCHECK(jlang::instanceof<jlang::Double>(box(5.0)));
    JCHECK(jlang::instanceof<jlang::Character>(box(u'c')));
    JCHECK_EQ(box(true), jlang::Boolean::TRUE);
    JCHECK_EQ(unbox<bool>(box(false)), false);
    JCHECK_EQ(box(5.5)->toString(), String("5.5"));
    JCHECK(box(String()) == nullptr);
    JCHECK(box(static_cast<const char*>(nullptr)) == nullptr);
    Object* s = box(String("text"));
    JCHECK_EQ(s->toString(), String("text"));
    JCHECK_EQ(s->getClass()->getName(), String("java.lang.String"));
    JCHECK_EQ(unbox<String>(s), String("text"));
    JCHECK_THROWS(jlang::ClassCastException, unbox<int32_t>(s));
    JCHECK_THROWS(jlang::NullPointerException, unbox<int32_t>(nullptr));
    JCHECK(!jlang::unboxOptional<int32_t>(nullptr).has_value());
    JCHECK_EQ(*jlang::unboxOptional<int32_t>(box(3)), 3);
    JCHECK(box(std::optional<int32_t>()) == nullptr);
    JCHECK_EQ(unbox<int32_t>(box(std::optional<int32_t>(9))), 9);
    // enums: one box per constant
    Object* n1 = box(Direction::NORTH);
    Object* n2 = box(Direction::NORTH);
    JCHECK_EQ(n1, n2);
    JCHECK(n1 != box(Direction::SOUTH));
    JCHECK_EQ(n1->toString(), String("NORTH"));
    JCHECK(unbox<Direction>(n1) == Direction::NORTH);
    JCHECK(box(Direction()) == nullptr);
    Square* sq = new Square();
    JCHECK_EQ(box(static_cast<Shape*>(sq)), static_cast<Object*>(sq));
    JCHECK_EQ(unbox<Square*>(box(sq)), sq);
    JCHECK(box(nullptr) == nullptr);
}

JTEST(Boxes_Statics) {
    using namespace jlang;
    JCHECK_EQ(Integer::MAX_VALUE, 2147483647);
    JCHECK_EQ(Integer::valueOf("12")->intValue(), 12);
    JCHECK_EQ(Integer::valueOf("ff", 16)->intValue(), 255);
    JCHECK_EQ((new Integer(String("77")))->intValue(), 77);
    JCHECK_EQ(Integer::compare(1, 2), -1);
    JCHECK_EQ(Integer::signum(-9), -1);
    JCHECK_EQ(Integer::rotateLeft(1, 33), 2);
    JCHECK_EQ(Long::decode("-0x10")->longValue(), -16);
    JCHECK_EQ(Long::decode("-9223372036854775808")->longValue(), Long::MIN_VALUE);
    JCHECK_EQ(Short::parseShort("-32768"), Short::MIN_VALUE);
    JCHECK_THROWS(NumberFormatException, Short::parseShort("32768"));
    JCHECK_EQ(Short::decode("0x7f")->shortValue(), 127);
    JCHECK_EQ(Byte::decode("-128")->byteValue(), -128);
    JCHECK_THROWS(NumberFormatException, Byte::decode("0x100"));
    JCHECK_EQ(Short::reverseBytes(0x1234), 0x3412);
    JCHECK_THROWS(NumberFormatException, Integer::parseInt(String()));
    try {
        Integer::parseInt(String());
    } catch (NumberFormatException& e) {
        JCHECK_EQ(e.getMessage(), String("Cannot parse null string: null"));
    }
    JCHECK(Float::isNaN(Float::NaN));
    JCHECK(Double::isInfinite(Double::POSITIVE_INFINITY));
    JCHECK_EQ(Float::floatToIntBits(Float::NaN), 0x7fc00000);
    JCHECK_EQ(Float::intBitsToFloat(0x3f800000), 1.0f);
    JCHECK_EQ(Double::doubleToLongBits(1.0), INT64_C(0x3ff0000000000000));
    JCHECK_EQ(Float::compare(0.0f, -0.0f), 1);
    JCHECK_EQ(Double::compare(Double::NaN, Double::POSITIVE_INFINITY), 1);
    JCHECK((new Float(1.5f))->equals(new Float(1.5f)));
    JCHECK(!(new Double(0.0))->equals(new Double(-0.0)));
    JCHECK((new Double(Double::NaN))->equals(new Double(Double::NaN)));
    JCHECK_EQ((new Double(1e20))->intValue(), Integer::MAX_VALUE);
    JCHECK_EQ((new Float(Float::NaN))->longValue(), 0);
    JCHECK_EQ(Double::MIN_VALUE, 4.9e-324);
    JCHECK(Boolean::parseBoolean("TRUE"));
    JCHECK(!Boolean::parseBoolean("yes"));
    JCHECK(!Boolean::parseBoolean(String()));
    JCHECK_EQ(Boolean::valueOf("true"), Boolean::TRUE);
    JCHECK_EQ(Boolean::toString(false), String("false"));
    JCHECK_EQ(Boolean::TRUE->hashCode(), 1231);
    JCHECK(Character::isDigit(u'7'));
    JCHECK(Character::isDigit(u'٣'));  // Arabic-indic three
    JCHECK(!Character::isDigit(u'x'));
    JCHECK(Character::isLetter(u'Ж'));
    JCHECK(Character::isLetter('a'));
    JCHECK(!Character::isLetter(u'1'));
    JCHECK(Character::isLetterOrDigit(u'1'));
    JCHECK(Character::isUpperCase(u'Ж'));
    JCHECK(Character::isLowerCase(u'ж'));
    JCHECK(Character::isWhitespace(u' '));
    JCHECK(Character::isWhitespace(u'\t'));
    JCHECK(!Character::isWhitespace(u' '));
    JCHECK(Character::isSpaceChar(u' '));
    JCHECK_EQ(Character::toUpperCase(u'ж'), u'Ж');
    JCHECK_EQ(Character::toLowerCase(u'A'), u'a');
    JCHECK_EQ(Character::toUpperCase('b'), u'B');
    JCHECK_EQ(Character::toUpperCase(static_cast<int32_t>('c')), static_cast<int32_t>('C'));
    JCHECK_EQ(Character::digit(u'f', 16), 15);
    JCHECK_EQ(Character::digit(u'9', 8), -1);
    JCHECK_EQ(Character::getNumericValue(u'z'), 35);
    JCHECK_EQ(Character::forDigit(11, 16), u'b');
    JCHECK_EQ(Character::valueOf(u'a'), Character::valueOf(u'a'));
    JCHECK_EQ(Character::toString(u'q'), String("q"));
    JCHECK_EQ(Character::toCodePoint(u'\xD83D', u'\xDE00'), 0x1F600);
}
