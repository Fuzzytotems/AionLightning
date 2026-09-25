// jlang/Collections.h - the jlang collections (included by <jlang/jlang.h>):
//
//   Java                                         C++
//   Iterator/ListIterator/Enumeration<E>         jlang::Iterator<E>*
//   Iterable<E>, Comparable<T>, Comparator<T>    jlang::Iterable<E>, Comparable<T>, Comparator<T>
//   Collection/List/ArrayList/LinkedList/...     jlang::List<E>*            <jlang/List.h>
//   Set/HashSet/LinkedHashSet/FastSet            jlang::Set<E>*             <jlang/Set.h>
//   Map/HashMap/LinkedHashMap/FastMap/...        jlang::Map<K,V>*           <jlang/Map.h>
//   Map.Entry<K,V>                               jlang::Entry<K,V> (value)
//   ConcurrentHashMap                            jlang::ConcurrentHashMap<K,V>*
//   TreeMap/SortedMap/EnumMap, TreeSet/EnumSet   jlang::TreeMap<K,V>*, jlang::TreeSet<E>*  <jlang/Tree.h>
//   Queue/Deque/ArrayDeque, ConcurrentLinkedQueue jlang::Deque<E>*, jlang::ConcurrentLinkedQueue<E>*
//   PriorityQueue                                jlang::PriorityQueue<E>*   <jlang/Deque.h>
//   TIntObjectHashMap/TIntIntHashMap/TIntArrayList  jlang::TIntObjectHashMap<V>* ... <jlang/Trove.h>
//   BitSet                                       jlang::BitSet*             <jlang/BitSet.h>
//   Arrays, ArrayUtils (commons-lang)            jlang::Arrays, jlang::ArrayUtils  <jlang/Arrays.h>
//   Collections                                  jlang::Collections (below)
//   T[]                                          jlang::Array<T>*           <jlang/Array.h>
//
// Part of the jlang core: translated code includes <jlang/jlang.h>, never this header directly.
#pragma once

#include <jlang/Array.h>
#include <jlang/Arrays.h>
#include <jlang/BitSet.h>
#include <jlang/CollectionsCore.h>
#include <jlang/Deque.h>
#include <jlang/List.h>
#include <jlang/Map.h>
#include <jlang/Set.h>
#include <jlang/System.h>
#include <jlang/Tree.h>
#include <jlang/Trove.h>

namespace jlang {

namespace detail {
Random* collectionsRandom();  // the shared Random of Collections.shuffle(list)
}

// ---------------------------------------------------------------------------------------
// java.util.Collections
//   * unmodifiableX(c) returns c itself (no read-only wrapper).
//   * synchronizedX(c) enables c's lock (c->shared()) and returns c.
//   * emptyList/emptySet/emptyMap return new empty collections: emptyList<Player*>().
class Collections final {
public:
    // ---- sorting (stable, Java 6 merge sort)
    template<class T>
    static void sort(List<T>* list) {
        if (list == nullptr) detail::throwNullPointer();
        list->sort(static_cast<Comparator<T>*>(nullptr));
    }
    template<class T, class U>
        requires std::is_convertible_v<const T&, U>
    static void sort(List<T>* list, Comparator<U>* c) {
        if (list == nullptr) detail::throwNullPointer();
        list->sort(c);
    }
    template<class T, class F>
        requires detail::IsJavaCmpFn<F, T>
    static void sort(List<T>* list, F f) {
        if (list == nullptr) detail::throwNullPointer();
        list->sort(f);
    }

    // ---- order manipulation
    // shuffle(list[, rnd]): Java's algorithm (for i = size..2: swap(i-1, rnd.nextInt(i))), so a
    // seeded jlang::Random gives Java's permutation. rnd: any object with nextInt(int).
    template<class T>
    static void shuffle(List<T>* list) {
        shuffle(list, detail::collectionsRandom());
    }
    template<class T, class R>
        requires requires(R* r) { { r->nextInt(1) } -> std::convertible_to<int32_t>; }
    static void shuffle(List<T>* list, R* rnd) {
        if (list == nullptr || rnd == nullptr) detail::throwNullPointer();
        for (int32_t i = list->size(); i > 1; i--) swap(list, i - 1, static_cast<int32_t>(rnd->nextInt(i)));
    }
    template<class T>
    static void reverse(List<T>* list) {
        if (list == nullptr) detail::throwNullPointer();
        const int32_t size = list->size();
        for (int32_t i = 0, mid = size >> 1, j = size - 1; i < mid; i++, j--) swap(list, i, j);
    }
    template<class T>
    static void swap(List<T>* list, int32_t i, int32_t j) {
        if (list == nullptr) detail::throwNullPointer();
        list->set(i, list->set(j, list->get(i)));
    }
    template<class T>
    static void fill(List<T>* list, const std::type_identity_t<T>& obj) {
        if (list == nullptr) detail::throwNullPointer();
        const int32_t size = list->size();
        for (int32_t i = 0; i < size; i++) list->set(i, obj);
    }
    // copy(dest, src): IndexOutOfBoundsException("Source does not fit in dest").
    template<class T>
    static void copy(List<T>* dest, List<T>* src) {
        if (dest == nullptr || src == nullptr) detail::throwNullPointer();
        auto s = src->_snapshot();
        if (static_cast<int32_t>(s.size()) > dest->size()) detail::throwIndexOutOfBoundsMsg("Source does not fit in dest");
        for (std::size_t i = 0; i < s.size(); i++) dest->set(static_cast<int32_t>(i), s[i]);
    }

    // ---- min / max (NoSuchElementException for an empty collection)
    template<class T>
    static T min(Collection<T>* c) {
        return extreme(c, detail::NaturalCmp<T>{}, -1);
    }
    template<class T, class U>
        requires std::is_convertible_v<const T&, U>
    static T min(Collection<T>* c, Comparator<U>* comp) {
        return extreme(c, detail::PtrCmp<T, U>{comp}, -1);
    }
    template<class T>
    static T max(Collection<T>* c) {
        return extreme(c, detail::NaturalCmp<T>{}, 1);
    }
    template<class T, class U>
        requires std::is_convertible_v<const T&, U>
    static T max(Collection<T>* c, Comparator<U>* comp) {
        return extreme(c, detail::PtrCmp<T, U>{comp}, 1);
    }

    template<class T>
    static int32_t frequency(Collection<T>* c, const std::type_identity_t<T>& o) {
        if (c == nullptr) detail::throwNullPointer();
        int32_t n = 0;
        for (const auto& e : c->_snapshot()) {
            if (detail::javaEquals(static_cast<const T&>(o), static_cast<const T&>(e))) n++;
        }
        return n;
    }
    template<class T>
    static bool disjoint(Collection<T>* c1, Collection<T>* c2) {
        if (c1 == nullptr || c2 == nullptr) detail::throwNullPointer();
        for (const auto& e : c1->_snapshot()) {
            if (c2->contains(e)) return false;
        }
        return true;
    }

    // binarySearch(sorted list, key[, comparator]): index or -(insertion point) - 1.
    template<class T>
    static int32_t binarySearch(List<T>* list, const std::type_identity_t<T>& key) {
        return binarySearch0(list, key, detail::NaturalCmp<T>{});
    }
    template<class T, class U>
        requires std::is_convertible_v<const T&, U>
    static int32_t binarySearch(List<T>* list, const std::type_identity_t<T>& key, Comparator<U>* c) {
        return binarySearch0(list, key, detail::PtrCmp<T, U>{c});
    }

    // ---- factories
    template<class T>
    static List<T>* emptyList() {
        return new List<T>();
    }
    template<class T>
    static Set<T>* emptySet() {
        return new Set<T>();
    }
    template<class K, class V>
    static Map<K, V>* emptyMap() {
        return new Map<K, V>();
    }
    template<class T>
    static List<T>* singletonList(const T& o) {
        auto* l = new List<T>();
        l->add(o);
        return l;
    }
    static List<String> * singletonList(const char* o) { return singletonList(String(o)); }
    template<class T>
    static Set<T>* singleton(const T& o) {
        auto* s = new Set<T>();
        s->add(o);
        return s;
    }
    template<class K, class V>
    static Map<K, V>* singletonMap(const K& key, const V& value) {
        auto* m = new Map<K, V>();
        m->put(key, value);
        return m;
    }
    // nCopies(n, o): IllegalArgumentException("List length = -1") for n < 0.
    template<class T>
    static List<T>* nCopies(int32_t n, const T& o) {
        if (n < 0) detail::throwIllegalArgument("List length = " + std::to_string(n));
        auto* l = new List<T>(n);
        l->vec().assign(static_cast<std::size_t>(n), o);
        return l;
    }
    // addAll(c, a, b, ...) / addAll(c, array): true if c changed.
    template<class T, class... Ts>
        requires(sizeof...(Ts) > 0 && (std::is_convertible_v<const Ts&, T> && ...))
    static bool addAll(Collection<T>* c, const Ts&... elements) {
        if (c == nullptr) detail::throwNullPointer();
        bool changed = false;
        ((changed = c->add(static_cast<T>(elements)) || changed), ...);
        return changed;
    }
    template<class T>
    static bool addAll(Collection<T>* c, Array<T>* elements) {
        if (c == nullptr || elements == nullptr) detail::throwNullPointer();
        bool changed = false;
        for (const auto& e : *elements) changed = c->add(e) || changed;
        return changed;
    }
    // newSetFromMap(map): a Set (locked when the map is shared).
    template<class K>
    static Set<K>* newSetFromMap(Map<K, bool>* map) {
        if (map == nullptr) detail::throwNullPointer();
        if (!map->isEmpty()) detail::throwIllegalArgument("Map is non-empty");
        auto* s = new Set<K>();
        if (map->isShared()) s->shared();
        return s;
    }
    template<class T>
    static Comparator<T>* reverseOrder() {
        return new detail::ReverseComparator<T>(nullptr);
    }
    template<class T>
    static Comparator<T>* reverseOrder(Comparator<T>* cmp) {
        return new detail::ReverseComparator<T>(cmp);
    }
    // enumeration(c): an Enumeration (jlang::Iterator) over c.
    template<class T>
    static Iterator<T>* enumeration(Collection<T>* c) {
        if (c == nullptr) detail::throwNullPointer();
        return c->iterator();
    }
    // list(enumeration): the remaining elements as a new list.
    template<class T>
    static List<T>* list(Iterator<T>* e) {
        auto* l = new List<T>();
        while (e->hasNext()) l->add(e->next());
        return l;
    }

    // ---- wrappers: unmodifiable = identity, synchronized = enable the lock
    template<class C>
    static C* unmodifiableList(C* c) {
        return c;
    }
    template<class C>
    static C* unmodifiableSet(C* c) {
        return c;
    }
    template<class C>
    static C* unmodifiableMap(C* c) {
        return c;
    }
    template<class C>
    static C* unmodifiableCollection(C* c) {
        return c;
    }
    template<class C>
    static C* unmodifiableSortedMap(C* c) {
        return c;
    }
    template<class C>
    static C* unmodifiableSortedSet(C* c) {
        return c;
    }
    template<class C>
    static C* synchronizedList(C* c) {
        return makeShared(c);
    }
    template<class C>
    static C* synchronizedSet(C* c) {
        return makeShared(c);
    }
    template<class C>
    static C* synchronizedMap(C* c) {
        return makeShared(c);
    }
    template<class C>
    static C* synchronizedCollection(C* c) {
        return makeShared(c);
    }
    template<class C>
    static C* synchronizedSortedMap(C* c) {
        return makeShared(c);
    }
    template<class C>
    static C* synchronizedSortedSet(C* c) {
        return makeShared(c);
    }

private:
    template<class C>
    static C* makeShared(C* c) {
        if (c == nullptr) detail::throwNullPointer();
        c->shared();
        return c;
    }
    template<class T, class Cmp>
    static T extreme(Collection<T>* c, Cmp cmp, int32_t sign) {
        if (c == nullptr) detail::throwNullPointer();
        auto s = c->_snapshot();
        if (s.empty()) detail::throwNoSuchElement();
        T candidate = s[0];
        for (std::size_t i = 1; i < s.size(); i++) {
            const int32_t r = cmp(static_cast<const T&>(s[i]), candidate);
            if ((sign < 0 && r < 0) || (sign > 0 && r > 0)) candidate = s[i];
        }
        return candidate;
    }
    template<class T, class Cmp>
    static int32_t binarySearch0(List<T>* list, const T& key, Cmp c) {
        if (list == nullptr) detail::throwNullPointer();
        int32_t low = 0;
        int32_t high = list->size() - 1;
        while (low <= high) {
            const int32_t mid = static_cast<int32_t>(static_cast<uint32_t>(low + high) >> 1);
            const int32_t cmp = c(list->get(mid), key);
            if (cmp < 0) low = mid + 1;
            else if (cmp > 0) high = mid - 1;
            else return mid;
        }
        return -(low + 1);
    }
};

}  // namespace jlang
