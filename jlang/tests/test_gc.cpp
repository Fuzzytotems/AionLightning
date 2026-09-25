// Garbage collector integration: cyclic garbage from several threads, long-lived structures,
// std containers on the GC heap, monitors and strings under collection pressure.
#include "jtest.h"

#include <atomic>
#include <functional>
#include <map>
#include <unordered_map>
#include <vector>

using jlang::String;

namespace {

struct Player;
struct Controller : public virtual jlang::Object {
    Player* owner = nullptr;
    std::vector<Player*> known;
    String name = String(std::string(64, 'n'));
    std::function<void()> callback;
};
struct Player : public virtual jlang::Object {
    Controller* ctl = nullptr;
    std::unordered_map<int32_t, Player*> map;
    int32_t hp = 5;
    Player() {
        ctl = new Controller();
        ctl->owner = this;
        ctl->callback = [this] { hp++; };
    }
};

std::atomic<int64_t> g_made{0};
std::atomic<int32_t> g_bad{0};

void worker(void*) {
    Player* keep = new Player();  // long-lived for this thread, checked at the end
    for (int i = 0; i < 60000; i++) {
        Player* p = new Player();
        Player* q = new Player();
        p->ctl->known.push_back(q);
        q->ctl->known.push_back(p);  // cycle
        p->map[i] = q;
        p->ctl->callback();
        if (p->hp != 6) g_bad++;
        JSYNC(p) { q->hp++; }
        if ((i & 1023) == 0) keep->ctl->known.push_back(p);
        String s = jlang::str("player-", i, "-", q->hp);
        if (!s.startsWith("player-")) g_bad++;
        g_made++;
    }
    for (Player* p : keep->ctl->known) {
        if (p->ctl->owner != p || p->hp != 6 || p->ctl->name.length() != 64) g_bad++;
        if (p->ctl->known.size() != 1 || p->ctl->known[0]->ctl->known[0] != p) g_bad++;
    }
}

}  // namespace

JTEST(GC_StressWithCyclesAcrossThreads) {
    std::vector<Player*>* longLived = new std::vector<Player*>();
    for (int i = 0; i < 2000; i++) longLived->push_back(new Player());
    std::map<int, String>* strings = new std::map<int, String>();
    for (int i = 0; i < 2000; i++) (*strings)[i] = jlang::str("value-", i, std::string(50, 'v'));
    int64_t collectionsBefore = jlang::gc::collections();
    std::vector<uint64_t> threads;
    for (int t = 0; t < 4; t++) threads.push_back(jlang::gc::startNativeThread(&worker, nullptr));
    for (uint64_t t : threads) jlang::gc::joinNativeThread(t);
    jlang::gc::collect();
    JCHECK_EQ(g_made.load(), INT64_C(240000));
    JCHECK_EQ(g_bad.load(), 0);
    JCHECK(jlang::gc::collections() > collectionsBefore);
    int ok = 0;
    for (Player* p : *longLived) {
        if (p->ctl->owner == p && p->ctl->name.length() == 64) ok++;
    }
    JCHECK_EQ(ok, 2000);
    for (int i = 0; i < 2000; i++) JCHECK_EQ(strings->at(i), jlang::str("value-", i, std::string(50, 'v')));
    // the heap stays bounded: ~480k players with cycles were garbage
    JCHECK(jlang::gc::heapSize() < INT64_C(2) * 1024 * 1024 * 1024);
}

JTEST(GC_ThreadRegistration) {
    JCHECK(jlang::gc::isCurrentThreadRegistered());
    JCHECK(!jlang::gc::registerCurrentThread());  // main thread is already registered
    std::atomic<bool> registered{false};
    struct Arg {
        std::atomic<bool>* out;
    };
    uint64_t t = jlang::gc::startNativeThread(
        [](void* p) { static_cast<Arg*>(p)->out->store(jlang::gc::isCurrentThreadRegistered()); },
        new Arg{&registered});
    jlang::gc::joinNativeThread(t);
    JCHECK(registered.load());
    void* cell = jlang::gc::allocUncollectable(64);
    JCHECK(cell != nullptr);
    jlang::gc::freeUncollectable(cell);
    char* atomicMem = static_cast<char*>(jlang::gc::allocAtomic(100));
    JCHECK_EQ(atomicMem[99], 0);
    JCHECK(jlang::gc::totalAllocated() > 0);
}

JTEST(GC_AlignedAndNothrowNew) {
    struct alignas(64) Wide {
        char data[100];
    };
    for (int i = 0; i < 100; i++) {
        Wide* w = new Wide();
        JCHECK_EQ(reinterpret_cast<uintptr_t>(w) % 64, 0u);
        w->data[99] = 1;
    }
    int* p = new (std::nothrow) int[10];
    JCHECK(p != nullptr);
    JCHECK_EQ(p[9], 0);  // GC memory is zeroed
    delete[] p;          // no-op, must not crash
    int* q = new int(5);
    delete q;
}
