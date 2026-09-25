// jlang/Tree.h - java.util.TreeMap (also SortedMap/NavigableMap/EnumMap) and
// java.util.TreeSet (also SortedSet/NavigableSet/EnumSet).
//
// The red-black tree is a line-by-line port of java.util.TreeMap, so ordering, lookups and
// removals behave exactly like Java even with inconsistent comparators (for example the
// "never returns 0" comparators used to keep duplicates in a TreeSet).
//
// Part of the jlang core: translated code includes <jlang/jlang.h>, never this header directly.
#pragma once

#include <jlang/CollectionsCore.h>
#include <jlang/List.h>
#include <jlang/Map.h>
#include <jlang/Set.h>

namespace jlang {

namespace detail {

template<class K, class V>
struct RBNode {
    K key;
    [[no_unique_address]] V value;
    RBNode* left = nullptr;
    RBNode* right = nullptr;
    RBNode* parent = nullptr;
    bool color = true;  // BLACK
};

// Port of java.util.TreeMap's tree algorithms (JDK 6).
template<class K, class V>
class RBTree {
public:
    using Node = RBNode<K, V>;
    static constexpr bool RED = false;
    static constexpr bool BLACK = true;

    Node* root = nullptr;
    int32_t size = 0;
    Comparator<K>* comparator = nullptr;  // null: natural ordering

    int32_t compare(const K& a, const K& b) const {
        if (comparator != nullptr) return comparator->compare(a, b);
        return compareNatural(a, b);
    }

    Node* getEntry(const K& key) const {
        Node* p = root;
        while (p != nullptr) {
            const int32_t cmp = compare(key, p->key);
            if (cmp < 0) p = p->left;
            else if (cmp > 0) p = p->right;
            else return p;
        }
        return nullptr;
    }

    // Java TreeMap.put: returns the node holding key; *inserted tells whether it is new.
    Node* put(const K& key, bool* inserted) {
        Node* t = root;
        if (t == nullptr) {
            root = newNode(key, nullptr);
            size = 1;
            *inserted = true;
            return root;
        }
        int32_t cmp;
        Node* parent;
        do {
            parent = t;
            cmp = compare(key, t->key);
            if (cmp < 0) t = t->left;
            else if (cmp > 0) t = t->right;
            else {
                *inserted = false;
                return t;
            }
        } while (t != nullptr);
        Node* e = newNode(key, parent);
        if (cmp < 0) parent->left = e;
        else parent->right = e;
        fixAfterInsertion(e);
        size++;
        *inserted = true;
        return e;
    }

    Node* first() const {
        Node* p = root;
        if (p != nullptr) {
            while (p->left != nullptr) p = p->left;
        }
        return p;
    }
    Node* last() const {
        Node* p = root;
        if (p != nullptr) {
            while (p->right != nullptr) p = p->right;
        }
        return p;
    }

    static Node* successor(Node* t) {
        if (t == nullptr) return nullptr;
        if (t->right != nullptr) {
            Node* p = t->right;
            while (p->left != nullptr) p = p->left;
            return p;
        }
        Node* p = t->parent;
        Node* ch = t;
        while (p != nullptr && ch == p->right) {
            ch = p;
            p = p->parent;
        }
        return p;
    }
    static Node* predecessor(Node* t) {
        if (t == nullptr) return nullptr;
        if (t->left != nullptr) {
            Node* p = t->left;
            while (p->right != nullptr) p = p->right;
            return p;
        }
        Node* p = t->parent;
        Node* ch = t;
        while (p != nullptr && ch == p->left) {
            ch = p;
            p = p->parent;
        }
        return p;
    }

    Node* ceiling(const K& key) const {
        Node* p = root;
        while (p != nullptr) {
            const int32_t cmp = compare(key, p->key);
            if (cmp < 0) {
                if (p->left != nullptr) p = p->left;
                else return p;
            } else if (cmp > 0) {
                if (p->right != nullptr) {
                    p = p->right;
                } else {
                    Node* parent = p->parent;
                    Node* ch = p;
                    while (parent != nullptr && ch == parent->right) {
                        ch = parent;
                        parent = parent->parent;
                    }
                    return parent;
                }
            } else {
                return p;
            }
        }
        return nullptr;
    }
    Node* floor(const K& key) const {
        Node* p = root;
        while (p != nullptr) {
            const int32_t cmp = compare(key, p->key);
            if (cmp > 0) {
                if (p->right != nullptr) p = p->right;
                else return p;
            } else if (cmp < 0) {
                if (p->left != nullptr) {
                    p = p->left;
                } else {
                    Node* parent = p->parent;
                    Node* ch = p;
                    while (parent != nullptr && ch == parent->left) {
                        ch = parent;
                        parent = parent->parent;
                    }
                    return parent;
                }
            } else {
                return p;
            }
        }
        return nullptr;
    }
    Node* higher(const K& key) const {
        Node* p = root;
        while (p != nullptr) {
            const int32_t cmp = compare(key, p->key);
            if (cmp < 0) {
                if (p->left != nullptr) p = p->left;
                else return p;
            } else {
                if (p->right != nullptr) {
                    p = p->right;
                } else {
                    Node* parent = p->parent;
                    Node* ch = p;
                    while (parent != nullptr && ch == parent->right) {
                        ch = parent;
                        parent = parent->parent;
                    }
                    return parent;
                }
            }
        }
        return nullptr;
    }
    Node* lower(const K& key) const {
        Node* p = root;
        while (p != nullptr) {
            const int32_t cmp = compare(key, p->key);
            if (cmp > 0) {
                if (p->right != nullptr) p = p->right;
                else return p;
            } else {
                if (p->left != nullptr) {
                    p = p->left;
                } else {
                    Node* parent = p->parent;
                    Node* ch = p;
                    while (parent != nullptr && ch == parent->left) {
                        ch = parent;
                        parent = parent->parent;
                    }
                    return parent;
                }
            }
        }
        return nullptr;
    }

    // True if p is still linked into this tree.
    bool attached(Node* p) const {
        while (p != nullptr && p->parent != nullptr) p = p->parent;
        return p != nullptr && p == root;
    }

    void clear() {
        root = nullptr;
        size = 0;
    }

    void deleteEntry(Node* p) {
        size--;
        // If strictly internal, copy successor's element to p and then make p point to successor.
        if (p->left != nullptr && p->right != nullptr) {
            Node* s = successor(p);
            p->key = s->key;
            p->value = s->value;
            p = s;
        }
        Node* replacement = (p->left != nullptr ? p->left : p->right);
        if (replacement != nullptr) {
            replacement->parent = p->parent;
            if (p->parent == nullptr) root = replacement;
            else if (p == p->parent->left) p->parent->left = replacement;
            else p->parent->right = replacement;
            p->left = p->right = p->parent = nullptr;
            if (p->color == BLACK) fixAfterDeletion(replacement);
        } else if (p->parent == nullptr) {
            root = nullptr;
        } else {
            if (p->color == BLACK) fixAfterDeletion(p);
            if (p->parent != nullptr) {
                if (p == p->parent->left) p->parent->left = nullptr;
                else if (p == p->parent->right) p->parent->right = nullptr;
                p->parent = nullptr;
            }
        }
    }

    template<class F>
    void forEach(F&& f) const {
        for (Node* e = first(); e != nullptr; e = successor(e)) f(e);
    }

private:
    static Node* newNode(const K& key, Node* parent) {
        Node* n = new Node{key, V{}, nullptr, nullptr, parent, BLACK};
        return n;
    }
    static bool colorOf(Node* p) { return p == nullptr ? BLACK : p->color; }
    static Node* parentOf(Node* p) { return p == nullptr ? nullptr : p->parent; }
    static void setColor(Node* p, bool c) {
        if (p != nullptr) p->color = c;
    }
    static Node* leftOf(Node* p) { return p == nullptr ? nullptr : p->left; }
    static Node* rightOf(Node* p) { return p == nullptr ? nullptr : p->right; }

    void rotateLeft(Node* p) {
        if (p != nullptr) {
            Node* r = p->right;
            p->right = r->left;
            if (r->left != nullptr) r->left->parent = p;
            r->parent = p->parent;
            if (p->parent == nullptr) root = r;
            else if (p->parent->left == p) p->parent->left = r;
            else p->parent->right = r;
            r->left = p;
            p->parent = r;
        }
    }
    void rotateRight(Node* p) {
        if (p != nullptr) {
            Node* l = p->left;
            p->left = l->right;
            if (l->right != nullptr) l->right->parent = p;
            l->parent = p->parent;
            if (p->parent == nullptr) root = l;
            else if (p->parent->right == p) p->parent->right = l;
            else p->parent->left = l;
            l->right = p;
            p->parent = l;
        }
    }
    void fixAfterInsertion(Node* x) {
        x->color = RED;
        while (x != nullptr && x != root && x->parent->color == RED) {
            if (parentOf(x) == leftOf(parentOf(parentOf(x)))) {
                Node* y = rightOf(parentOf(parentOf(x)));
                if (colorOf(y) == RED) {
                    setColor(parentOf(x), BLACK);
                    setColor(y, BLACK);
                    setColor(parentOf(parentOf(x)), RED);
                    x = parentOf(parentOf(x));
                } else {
                    if (x == rightOf(parentOf(x))) {
                        x = parentOf(x);
                        rotateLeft(x);
                    }
                    setColor(parentOf(x), BLACK);
                    setColor(parentOf(parentOf(x)), RED);
                    rotateRight(parentOf(parentOf(x)));
                }
            } else {
                Node* y = leftOf(parentOf(parentOf(x)));
                if (colorOf(y) == RED) {
                    setColor(parentOf(x), BLACK);
                    setColor(y, BLACK);
                    setColor(parentOf(parentOf(x)), RED);
                    x = parentOf(parentOf(x));
                } else {
                    if (x == leftOf(parentOf(x))) {
                        x = parentOf(x);
                        rotateRight(x);
                    }
                    setColor(parentOf(x), BLACK);
                    setColor(parentOf(parentOf(x)), RED);
                    rotateLeft(parentOf(parentOf(x)));
                }
            }
        }
        root->color = BLACK;
    }
    void fixAfterDeletion(Node* x) {
        while (x != root && colorOf(x) == BLACK) {
            if (x == leftOf(parentOf(x))) {
                Node* sib = rightOf(parentOf(x));
                if (colorOf(sib) == RED) {
                    setColor(sib, BLACK);
                    setColor(parentOf(x), RED);
                    rotateLeft(parentOf(x));
                    sib = rightOf(parentOf(x));
                }
                if (colorOf(leftOf(sib)) == BLACK && colorOf(rightOf(sib)) == BLACK) {
                    setColor(sib, RED);
                    x = parentOf(x);
                } else {
                    if (colorOf(rightOf(sib)) == BLACK) {
                        setColor(leftOf(sib), BLACK);
                        setColor(sib, RED);
                        rotateRight(sib);
                        sib = rightOf(parentOf(x));
                    }
                    setColor(sib, colorOf(parentOf(x)));
                    setColor(parentOf(x), BLACK);
                    setColor(rightOf(sib), BLACK);
                    rotateLeft(parentOf(x));
                    x = root;
                }
            } else {
                Node* sib = leftOf(parentOf(x));
                if (colorOf(sib) == RED) {
                    setColor(sib, BLACK);
                    setColor(parentOf(x), RED);
                    rotateRight(parentOf(x));
                    sib = leftOf(parentOf(x));
                }
                if (colorOf(rightOf(sib)) == BLACK && colorOf(leftOf(sib)) == BLACK) {
                    setColor(sib, RED);
                    x = parentOf(x);
                } else {
                    if (colorOf(leftOf(sib)) == BLACK) {
                        setColor(rightOf(sib), BLACK);
                        setColor(sib, RED);
                        rotateLeft(sib);
                        sib = leftOf(parentOf(x));
                    }
                    setColor(sib, colorOf(parentOf(x)));
                    setColor(parentOf(x), BLACK);
                    setColor(leftOf(sib), BLACK);
                    rotateRight(parentOf(x));
                    x = root;
                }
            }
        }
        setColor(x, BLACK);
    }
};

// Comparator<T> adapter for a Comparator<U>* (Comparator<? super T>).
template<class T, class U>
class ComparatorAdapter final : public virtual Comparator<T> {
public:
    explicit ComparatorAdapter(Comparator<U>* c) : c_(c) {}
    int32_t compare(T a, T b) override { return c_->compare(a, b); }

private:
    Comparator<U>* c_;
};

// Collections.reverseOrder(cmp) / natural reverse order.
template<class T>
class ReverseComparator final : public virtual Comparator<T> {
public:
    explicit ReverseComparator(Comparator<T>* c) : c_(c) {}
    int32_t compare(T a, T b) override {
        if (c_ == nullptr) return compareNatural(b, a);
        return c_->compare(b, a);
    }

private:
    Comparator<T>* c_;
};

// Iterator over a snapshot of a tree whose remove() deletes exactly the node it returned
// (Java's TreeMap iterator semantics, also with inconsistent comparators). Owner provides
// `void _removeIterated(TreeIterator*, std::size_t i)`.
template<class Out, class Tree, class Owner>
class TreeIterator final : public virtual Iterator<Out> {
public:
    using Node = typename Tree::Node;
    using Key = std::remove_cv_t<decltype(std::declval<Node>().key)>;

    TreeIterator(Owner* owner, Tree* tree, bool descending) : owner_(owner), tree_(tree) {
        nodes_.reserve(static_cast<std::size_t>(tree->size));
        keys_.reserve(static_cast<std::size_t>(tree->size));
        if (descending) {
            for (Node* e = tree->last(); e != nullptr; e = Tree::predecessor(e)) push(e);
        } else {
            for (Node* e = tree->first(); e != nullptr; e = Tree::successor(e)) push(e);
        }
    }
    bool hasNext() override { return cursor_ < keys_.size(); }
    Out next() override {
        if (cursor_ >= keys_.size()) throwNoSuchElement();
        lastRet_ = static_cast<int64_t>(cursor_++);
        return keys_[static_cast<std::size_t>(lastRet_)];
    }
    void remove() override {
        if (lastRet_ < 0) throwIllegalState();
        owner_->_removeIterated(this, static_cast<std::size_t>(lastRet_));
        lastRet_ = -1;
    }

    const Key& keyAt(std::size_t i) const { return keys_[i]; }

    // Unlinks the node of snapshot position i: the node itself when it is still linked and
    // holds the element, else a node found by scanning for the same element. Returns false
    // when the element is no longer in the tree.
    bool unlink(std::size_t i) {
        Node* n = nodes_[i];
        const Key& key = keys_[i];
        if (!(tree_->attached(n) && sameElement(n->key, key))) {
            n = nullptr;
            for (Node* e = tree_->first(); e != nullptr; e = Tree::successor(e)) {
                if (sameElement(e->key, key)) {
                    n = e;
                    break;
                }
            }
        }
        if (n == nullptr) return false;
        // deleteEntry moves the successor's element into a node with two children: follow it.
        if (n->left != nullptr && n->right != nullptr) {
            Node* s = Tree::successor(n);
            for (std::size_t j = 0; j < nodes_.size(); j++) {
                if (nodes_[j] == s) nodes_[j] = n;
            }
        }
        tree_->deleteEntry(n);
        return true;
    }

private:
    void push(Node* e) {
        nodes_.push_back(e);
        keys_.push_back(e->key);
    }

    Owner* owner_;
    Tree* tree_;
    Vec<Node*> nodes_;
    Vec<Key> keys_;
    std::size_t cursor_ = 0;
    int64_t lastRet_ = -1;
};

}  // namespace detail

// ---------------------------------------------------------------------------------------
// java.util.TreeMap<K,V> (SortedMap, NavigableMap, EnumMap): a jlang::Map ordered by the
// keys' natural ordering (jlang::Less rules: numbers, Strings, enum ordinals, compareTo) or
// by a Comparator. firstKey/lastKey throw NoSuchElementException on an empty map;
// firstEntry/lastEntry/ceilingEntry/... return a null Entry when there is none;
// ceilingKey/floorKey/higherKey/lowerKey return K{} (nullptr/0) then.
// headMap/tailMap/subMap/descendingMap return copies whose put/remove/clear also update this
// map (Java returns live views). Iteration: range-for and views iterate snapshots.
template<class K, class V>
class TreeMap : public Map<K, V> {
public:
    using Tree = detail::RBTree<K, V>;
    using Node = typename Tree::Node;

    TreeMap() {}
    explicit TreeMap(Comparator<K>* comparator) { tree_.comparator = comparator; }
    template<class U>
        requires(!std::is_same_v<U, K> && std::is_convertible_v<const K&, U>)
    explicit TreeMap(Comparator<U>* comparator) {
        if (comparator != nullptr) tree_.comparator = new detail::ComparatorAdapter<K, U>(comparator);
    }
    // new TreeMap(map) / new EnumMap(map): a TreeMap source keeps its comparator.
    explicit TreeMap(Map<K, V>* m) {
        if (m == nullptr) detail::throwNullPointer();
        if (auto* tm = dynamic_cast<TreeMap<K, V>*>(m)) tree_.comparator = tm->comparator();
        for (const auto& e : m->_entries()) putNoLockTree(e.getKey(), e.getValue());
    }

    Comparator<K>* comparator() { return tree_.comparator; }

    int32_t size() override {
        auto g = this->lock();
        return tree_.size;
    }
    bool isEmpty() override { return size() == 0; }
    V get(const K& key) override {
        auto g = this->lock();
        Node* p = tree_.getEntry(key);
        return p == nullptr ? V{} : V(p->value);
    }
    std::optional<V> getOptional(const K& key) override {
        auto g = this->lock();
        Node* p = tree_.getEntry(key);
        if (p == nullptr) return std::nullopt;
        return p->value;
    }
    bool containsKey(const K& key) override {
        auto g = this->lock();
        return tree_.getEntry(key) != nullptr;
    }
    bool containsValue(const V& value) override {
        auto g = this->lock();
        for (Node* e = tree_.first(); e != nullptr; e = Tree::successor(e)) {
            if (detail::javaEquals(value, static_cast<const V&>(e->value))) return true;
        }
        return false;
    }
    V put(const K& key, const V& value) override {
        auto g = this->lock();
        V old = putNoLockTree(key, value);
        if (backing_ != nullptr) backing_->put(key, value);
        return old;
    }
    V putIfAbsent(const K& key, const V& value) override {
        auto g = this->lock();
        Node* p = tree_.getEntry(key);
        if (p != nullptr) return p->value;
        put(key, value);
        return V{};
    }
    V remove(const K& key) override {
        auto g = this->lock();
        Node* p = tree_.getEntry(key);
        V old{};
        if (p != nullptr) {
            old = p->value;
            tree_.deleteEntry(p);
            if (backing_ != nullptr) backing_->remove(key);
        }
        return old;
    }
    using Map<K, V>::remove;
    using Map<K, V>::get;
    using Map<K, V>::getOptional;
    using Map<K, V>::containsKey;
    using Map<K, V>::containsValue;
    void clear() override {
        auto g = this->lock();
        if (backing_ != nullptr) {
            for (Node* e = tree_.first(); e != nullptr; e = Tree::successor(e)) backing_->remove(e->key);
        }
        tree_.clear();
    }
    TreeMap<K, V>* clone() override {
        auto* m = new TreeMap<K, V>(tree_.comparator);
        auto g = this->lock();
        for (Node* e = tree_.first(); e != nullptr; e = Tree::successor(e)) m->putNoLockTree(e->key, e->value);
        return m;
    }

    // ---- SortedMap / NavigableMap
    K firstKey() {
        auto g = this->lock();
        return key(tree_.first());
    }
    K lastKey() {
        auto g = this->lock();
        return key(tree_.last());
    }
    Entry<K, V> firstEntry() {
        auto g = this->lock();
        return entry(tree_.first());
    }
    Entry<K, V> lastEntry() {
        auto g = this->lock();
        return entry(tree_.last());
    }
    // Removes and returns the first/last mapping (a detached entry), null Entry when empty.
    Entry<K, V> pollFirstEntry() {
        auto g = this->lock();
        return pollNode(tree_.first());
    }
    Entry<K, V> pollLastEntry() {
        auto g = this->lock();
        return pollNode(tree_.last());
    }
    Entry<K, V> ceilingEntry(const K& k) {
        auto g = this->lock();
        return entry(tree_.ceiling(k));
    }
    Entry<K, V> floorEntry(const K& k) {
        auto g = this->lock();
        return entry(tree_.floor(k));
    }
    Entry<K, V> higherEntry(const K& k) {
        auto g = this->lock();
        return entry(tree_.higher(k));
    }
    Entry<K, V> lowerEntry(const K& k) {
        auto g = this->lock();
        return entry(tree_.lower(k));
    }
    K ceilingKey(const K& k) {
        auto g = this->lock();
        return keyOrNull(tree_.ceiling(k));
    }
    K floorKey(const K& k) {
        auto g = this->lock();
        return keyOrNull(tree_.floor(k));
    }
    K higherKey(const K& k) {
        auto g = this->lock();
        return keyOrNull(tree_.higher(k));
    }
    K lowerKey(const K& k) {
        auto g = this->lock();
        return keyOrNull(tree_.lower(k));
    }
    // headMap(toKey) = keys < toKey; tailMap(fromKey) = keys >= fromKey; subMap(from, to) =
    // from <= key < to (copies with write-through, see the class comment).
    TreeMap<K, V>* headMap(const K& toKey) { return headMap(toKey, false); }
    TreeMap<K, V>* headMap(const K& toKey, bool inclusive) {
        auto g = this->lock();
        auto* m = derived(tree_.comparator);
        for (Node* e = tree_.first(); e != nullptr; e = Tree::successor(e)) {
            const int32_t c = tree_.compare(e->key, toKey);
            if (c > 0 || (c == 0 && !inclusive)) break;
            m->putNoLockTree(e->key, e->value);
        }
        return m;
    }
    TreeMap<K, V>* tailMap(const K& fromKey) { return tailMap(fromKey, true); }
    TreeMap<K, V>* tailMap(const K& fromKey, bool inclusive) {
        auto g = this->lock();
        auto* m = derived(tree_.comparator);
        for (Node* e = inclusive ? tree_.ceiling(fromKey) : tree_.higher(fromKey); e != nullptr;
             e = Tree::successor(e)) {
            m->putNoLockTree(e->key, e->value);
        }
        return m;
    }
    TreeMap<K, V>* subMap(const K& fromKey, const K& toKey) { return subMap(fromKey, true, toKey, false); }
    TreeMap<K, V>* subMap(const K& fromKey, bool fromInclusive, const K& toKey, bool toInclusive) {
        auto g = this->lock();
        if (tree_.compare(fromKey, toKey) > 0) detail::throwIllegalArgument("fromKey > toKey");
        auto* m = derived(tree_.comparator);
        for (Node* e = fromInclusive ? tree_.ceiling(fromKey) : tree_.higher(fromKey); e != nullptr;
             e = Tree::successor(e)) {
            const int32_t c = tree_.compare(e->key, toKey);
            if (c > 0 || (c == 0 && !toInclusive)) break;
            m->putNoLockTree(e->key, e->value);
        }
        return m;
    }
    // The map in reverse order (a copy ordered by the reversed comparator).
    TreeMap<K, V>* descendingMap() {
        auto g = this->lock();
        auto* m = derived(new detail::ReverseComparator<K>(tree_.comparator));
        for (Node* e = tree_.last(); e != nullptr; e = Tree::predecessor(e)) m->putNoLockTree(e->key, e->value);
        return m;
    }
    List<K>* navigableKeySet() { return this->keySet(); }
    List<K>* descendingKeySet() {
        detail::Vec<K> keys;
        {
            auto g = this->lock();
            for (Node* e = tree_.last(); e != nullptr; e = Tree::predecessor(e)) keys.push_back(e->key);
        }
        return new detail::MapKeyView<K, V>(this, std::move(keys));
    }

    TreeMap<K, V>* shared() {
        this->shared_ = true;
        return this;
    }
    TreeMap<K, V>* synchronized_() { return shared(); }

    // Range-for over a snapshot of the entries (in key order).
    typename Map<K, V>::Range begin() { return typename Map<K, V>::Range(detail::heapVec(_entries())); }
    detail::IterEnd end() { return {}; }
    typename Map<K, V>::Range begin() const { return const_cast<TreeMap*>(this)->begin(); }
    detail::IterEnd end() const { return {}; }

    detail::Vec<Entry<K, V>> _entries() override {
        auto g = this->lock();
        detail::Vec<Entry<K, V>> out;
        out.reserve(static_cast<std::size_t>(tree_.size));
        for (Node* e = tree_.first(); e != nullptr; e = Tree::successor(e)) out.emplace_back(e->key, e->value, this);
        return out;
    }
    void _snapshot(detail::Vec<K>* keys, detail::Vec<V>* vals) override {
        auto g = this->lock();
        for (Node* e = tree_.first(); e != nullptr; e = Tree::successor(e)) {
            if (keys != nullptr) keys->push_back(e->key);
            if (vals != nullptr) vals->push_back(e->value);
        }
    }

protected:
    bool _hashBacked() override { return false; }
    V putNoLockTree(const K& key, const V& value) {
        bool inserted = false;
        Node* n = tree_.put(key, &inserted);
        V old = inserted ? V{} : V(n->value);
        n->value = value;
        return old;
    }
    TreeMap<K, V>* derived(Comparator<K>* c) {
        auto* m = new TreeMap<K, V>(c);
        m->backing_ = this;
        return m;
    }
    static K key(Node* n) {
        if (n == nullptr) detail::throwNoSuchElement();
        return n->key;
    }
    static K keyOrNull(Node* n) { return n == nullptr ? K{} : K(n->key); }
    Entry<K, V> entry(Node* n) { return n == nullptr ? Entry<K, V>() : Entry<K, V>(n->key, n->value, this); }
    Entry<K, V> pollNode(Node* n) {
        if (n == nullptr) return Entry<K, V>();
        Entry<K, V> e(n->key, n->value);
        tree_.deleteEntry(n);
        if (backing_ != nullptr) backing_->remove(e.getKey());
        return e;
    }

    Tree tree_;
    Map<K, V>* backing_ = nullptr;
};

// ---------------------------------------------------------------------------------------
// java.util.TreeSet<E> (SortedSet, NavigableSet, EnumSet): a jlang::Set ordered like
// TreeMap. Elements comparing equal (compare() == 0) are duplicates. first()/last() throw
// NoSuchElementException when empty; pollFirst/pollLast/ceiling/floor/higher/lower return
// T{} (nullptr/0) when there is none. headSet/tailSet/subSet/descendingSet return copies
// whose add/remove/clear also update this set. Iteration is over snapshots; iterator()
// removal removes the exact element (also with inconsistent comparators).
//   EnumSet.allOf(E.class)  -> jlang::TreeSet<E>::allOf(E::values())
//   EnumSet.noneOf(E.class) -> new jlang::TreeSet<E>()
template<class T>
class TreeSet : public Set<T> {
public:
    using Tree = detail::RBTree<T, detail::Unit>;
    using Node = typename Tree::Node;
    using Collection<T>::addAll;
    using Collection<T>::contains;
    using Collection<T>::removeObject;

    TreeSet() {}
    explicit TreeSet(Comparator<T>* comparator) { tree_.comparator = comparator; }
    template<class U>
        requires(!std::is_same_v<U, T> && std::is_convertible_v<const T&, U>)
    explicit TreeSet(Comparator<U>* comparator) {
        if (comparator != nullptr) tree_.comparator = new detail::ComparatorAdapter<T, U>(comparator);
    }
    // new TreeSet(collection): natural order (a TreeSet source keeps its comparator).
    explicit TreeSet(Collection<T>* c) {
        if (c == nullptr) detail::throwNullPointer();
        if (auto* ts = dynamic_cast<TreeSet<T>*>(c)) tree_.comparator = ts->comparator();
        for (const auto& e : c->_snapshot()) addNoLockTree(e);
    }
    TreeSet(std::initializer_list<T> values) {
        for (const auto& e : values) addNoLockTree(e);
    }

    // EnumSet.allOf / EnumSet.of
    static TreeSet<T>* allOf(Array<T>* values) {
        auto* s = new TreeSet<T>();
        if (values != nullptr) {
            for (const auto& e : *values) s->addNoLockTree(e);
        }
        return s;
    }
    static TreeSet<T>* noneOf() { return new TreeSet<T>(); }
    static TreeSet<T>* of(std::initializer_list<T> values) { return new TreeSet<T>(values); }

    Comparator<T>* comparator() { return tree_.comparator; }

    int32_t size() override {
        auto g = this->lock();
        return tree_.size;
    }
    bool isEmpty() override { return size() == 0; }
    bool contains(const T& o) override {
        auto g = this->lock();
        return tree_.getEntry(o) != nullptr;
    }
    bool add(const T& e) override {
        auto g = this->lock();
        const bool inserted = addNoLockTree(e);
        if (backing_ != nullptr) backing_->add(e);
        return inserted;
    }
    bool removeObject(const T& o) override {
        auto g = this->lock();
        Node* p = tree_.getEntry(o);
        if (p == nullptr) return false;
        tree_.deleteEntry(p);
        if (backing_ != nullptr) backing_->removeObject(o);
        return true;
    }
    void clear() override {
        auto g = this->lock();
        if (backing_ != nullptr) {
            for (Node* e = tree_.first(); e != nullptr; e = Tree::successor(e)) backing_->removeObject(e->key);
        }
        tree_.clear();
    }
    Iterator<T>* iterator() override { return makeIterator(false); }
    Iterator<T>* descendingIterator() { return makeIterator(true); }
    TreeSet<T>* clone() override {
        auto* s = new TreeSet<T>(tree_.comparator);
        auto g = this->lock();
        for (Node* e = tree_.first(); e != nullptr; e = Tree::successor(e)) s->addNoLockTree(e->key);
        return s;
    }

    // ---- SortedSet / NavigableSet
    T first() {
        auto g = this->lock();
        Node* n = tree_.first();
        if (n == nullptr) detail::throwNoSuchElement();
        return n->key;
    }
    T last() {
        auto g = this->lock();
        Node* n = tree_.last();
        if (n == nullptr) detail::throwNoSuchElement();
        return n->key;
    }
    T pollFirst() {
        auto g = this->lock();
        Node* n = tree_.first();
        if (n == nullptr) return T{};
        T k = n->key;
        tree_.deleteEntry(n);
        if (backing_ != nullptr) backing_->removeObject(k);
        return k;
    }
    T pollLast() {
        auto g = this->lock();
        Node* n = tree_.last();
        if (n == nullptr) return T{};
        T k = n->key;
        tree_.deleteEntry(n);
        if (backing_ != nullptr) backing_->removeObject(k);
        return k;
    }
    T ceiling(const T& e) {
        auto g = this->lock();
        return keyOrNull(tree_.ceiling(e));
    }
    T floor(const T& e) {
        auto g = this->lock();
        return keyOrNull(tree_.floor(e));
    }
    T higher(const T& e) {
        auto g = this->lock();
        return keyOrNull(tree_.higher(e));
    }
    T lower(const T& e) {
        auto g = this->lock();
        return keyOrNull(tree_.lower(e));
    }
    TreeSet<T>* headSet(const T& toElement) { return headSet(toElement, false); }
    TreeSet<T>* headSet(const T& toElement, bool inclusive) {
        auto g = this->lock();
        auto* s = derived(tree_.comparator);
        for (Node* e = tree_.first(); e != nullptr; e = Tree::successor(e)) {
            const int32_t c = tree_.compare(e->key, toElement);
            if (c > 0 || (c == 0 && !inclusive)) break;
            s->addNoLockTree(e->key);
        }
        return s;
    }
    TreeSet<T>* tailSet(const T& fromElement) { return tailSet(fromElement, true); }
    TreeSet<T>* tailSet(const T& fromElement, bool inclusive) {
        auto g = this->lock();
        auto* s = derived(tree_.comparator);
        for (Node* e = inclusive ? tree_.ceiling(fromElement) : tree_.higher(fromElement); e != nullptr;
             e = Tree::successor(e)) {
            s->addNoLockTree(e->key);
        }
        return s;
    }
    TreeSet<T>* subSet(const T& fromElement, const T& toElement) { return subSet(fromElement, true, toElement, false); }
    TreeSet<T>* subSet(const T& fromElement, bool fromInclusive, const T& toElement, bool toInclusive) {
        auto g = this->lock();
        if (tree_.compare(fromElement, toElement) > 0) detail::throwIllegalArgument("fromKey > toKey");
        auto* s = derived(tree_.comparator);
        for (Node* e = fromInclusive ? tree_.ceiling(fromElement) : tree_.higher(fromElement); e != nullptr;
             e = Tree::successor(e)) {
            const int32_t c = tree_.compare(e->key, toElement);
            if (c > 0 || (c == 0 && !toInclusive)) break;
            s->addNoLockTree(e->key);
        }
        return s;
    }
    TreeSet<T>* descendingSet() {
        auto g = this->lock();
        auto* s = derived(new detail::ReverseComparator<T>(tree_.comparator));
        for (Node* e = tree_.last(); e != nullptr; e = Tree::predecessor(e)) s->addNoLockTree(e->key);
        return s;
    }

    TreeSet<T>* shared() {
        this->shared_ = true;
        return this;
    }
    TreeSet<T>* synchronized_() { return shared(); }

    // Range-for over a snapshot (in order).
    detail::SnapshotRange<T> begin() { return detail::SnapshotRange<T>(detail::heapVec(_snapshot())); }
    detail::IterEnd end() { return {}; }
    detail::SnapshotRange<T> begin() const { return const_cast<TreeSet*>(this)->begin(); }
    detail::IterEnd end() const { return {}; }

    detail::Vec<T> _snapshot() override {
        auto g = this->lock();
        detail::Vec<T> out;
        out.reserve(static_cast<std::size_t>(tree_.size));
        for (Node* e = tree_.first(); e != nullptr; e = Tree::successor(e)) out.push_back(e->key);
        return out;
    }

protected:
    bool _hashBacked() override { return false; }
    bool addNoLockTree(const T& e) {
        bool inserted = false;
        tree_.put(e, &inserted);
        return inserted;
    }
    TreeSet<T>* derived(Comparator<T>* c) {
        auto* s = new TreeSet<T>(c);
        s->backing_ = this;
        return s;
    }
    static T keyOrNull(Node* n) { return n == nullptr ? T{} : T(n->key); }
    using TreeIt = detail::TreeIterator<T, Tree, TreeSet<T>>;
    Iterator<T>* makeIterator(bool descending) {
        auto g = this->lock();
        return new TreeIt(this, &tree_, descending);
    }

public:
    // jlang internal: remove() of an iterator().
    void _removeIterated(TreeIt* it, std::size_t i) {
        auto g = this->lock();
        T k = it->keyAt(i);
        it->unlink(i);
        if (backing_ != nullptr) backing_->removeObject(k);
    }
    using Set<T>::_removeIterated;

protected:
    Tree tree_;
    Set<T>* backing_ = nullptr;
};

}  // namespace jlang
