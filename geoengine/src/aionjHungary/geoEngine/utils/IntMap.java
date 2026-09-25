/*
 * geoEngine 0.1 (package aionjHungary.geoEngine) - a cut-down copy of the jMonkeyEngine3 math,
 * scene-graph and collision code (BSD licence, jmonkeyengine.org) repackaged by "aionjHungary".
 * Source recovered from gameserver/lib/geoEngine-0.1.jar with CFR 0.152 and repaired so that it
 * recompiles to the jar's API and (modulo javac-version differences) bytecode; see ../../README.md.
 */
package aionjHungary.geoEngine.utils;

import aionjHungary.geoEngine.utils.IntMap.Entry;
import java.util.Iterator;

public final class IntMap<T>
implements Iterable<Entry>,
Cloneable {
    private Entry[] table;
    private final float loadFactor;
    private int size;
    private int mask;
    private int capacity;
    private int threshold;

    public IntMap() {
        this(16, 0.75f);
    }

    public IntMap(int initialCapacity) {
        this(initialCapacity, 0.75f);
    }

    public IntMap(int initialCapacity, float loadFactor) {
        if (initialCapacity > 0x40000000) {
            throw new IllegalArgumentException("initialCapacity is too large.");
        }
        if (initialCapacity < 0) {
            throw new IllegalArgumentException("initialCapacity must be greater than zero.");
        }
        if (loadFactor <= 0.0f) {
            throw new IllegalArgumentException("initialCapacity must be greater than zero.");
        }
        this.capacity = 1;
        while (this.capacity < initialCapacity) {
            this.capacity <<= 1;
        }
        this.loadFactor = loadFactor;
        this.threshold = (int)((float)this.capacity * loadFactor);
        this.table = new Entry[this.capacity];
        this.mask = this.capacity - 1;
    }

    public IntMap<T> clone() {
        try {
            IntMap clone = (IntMap)super.clone();
            Entry[] newTable = new Entry[this.table.length];
            for (int i = this.table.length - 1; i >= 0; --i) {
                if (this.table[i] == null) continue;
                newTable[i] = this.table[i].clone();
            }
            clone.table = newTable;
            return clone;
        }
        catch (CloneNotSupportedException ex) {
            return null;
        }
    }

    public boolean containsValue(Object value) {
        Entry[] table = this.table;
        int i = table.length;
        while (i-- > 0) {
            Entry e = table[i];
            while (e != null) {
                if (e.value.equals(value)) {
                    return true;
                }
                e = e.next;
            }
        }
        return false;
    }

    public boolean containsKey(int key) {
        int index = key & this.mask;
        Entry e = this.table[index];
        while (e != null) {
            if (e.key == key) {
                return true;
            }
            e = e.next;
        }
        return false;
    }

    public T get(int key) {
        int index = key & this.mask;
        Entry e = this.table[index];
        while (e != null) {
            if (e.key == key) {
                return (T)e.value;
            }
            e = e.next;
        }
        return null;
    }

    public T put(int key, T value) {
        int index = key & this.mask;
        for (Entry e = this.table[index]; e != null; e = e.next) {
            if (e.key == key) {
                T oldValue = (T)e.value;
                e.value = value;
                return oldValue;
            }
        }
        this.table[index] = new Entry<T>(key, value, this.table[index]);
        if (this.size++ >= this.threshold) {
            int newCapacity = 2 * this.capacity;
            Entry[] newTable = new Entry[newCapacity];
            Entry[] src = this.table;
            int bucketmask = newCapacity - 1;
            for (int j = 0; j < src.length; ++j) {
                Entry e = src[j];
                if (e != null) {
                    src[j] = null;
                    do {
                        Entry next = e.next;
                        index = e.key & bucketmask;
                        e.next = newTable[index];
                        newTable[index] = e;
                        e = next;
                    } while (e != null);
                }
            }
            this.table = newTable;
            this.capacity = newCapacity;
            this.threshold = (int)((float)newCapacity * this.loadFactor);
            this.mask = this.capacity - 1;
        }
        return null;
    }

    public T remove(int key) {
        int index = key & this.mask;
        Entry prev = this.table[index];
        Entry e = prev;
        while (e != null) {
            Entry next = e.next;
            if (e.key == key) {
                --this.size;
                if (prev == e) {
                    this.table[index] = next;
                } else {
                    prev.next = next;
                }
                return (T)e.value;
            }
            prev = e;
            e = next;
        }
        return null;
    }

    public int size() {
        return this.size;
    }

    public void clear() {
        Entry[] table = this.table;
        int index = table.length;
        while (--index >= 0) {
            table[index] = null;
        }
        this.size = 0;
    }

    @Override
    public Iterator<Entry> iterator() {
        return new IntMapIterator();
    }

    public static final class Entry<T>
    implements Cloneable {
        final int key;
        T value;
        Entry next;

        Entry(int k, T v, Entry n) {
            this.key = k;
            this.value = v;
            this.next = n;
        }

        public int getKey() {
            return this.key;
        }

        public T getValue() {
            return this.value;
        }

        public String toString() {
            return this.key + " => " + this.value;
        }

        public Entry<T> clone() {
            try {
                Entry clone = (Entry)super.clone();
                clone.next = this.next != null ? this.next.clone() : null;
                return clone;
            }
            catch (CloneNotSupportedException ex) {
                return null;
            }
        }
    }

    final class IntMapIterator
    implements Iterator<Entry> {
        private Entry cur;
        private int idx = 0;
        private int el = 0;

        public IntMapIterator() {
            this.cur = IntMap.this.table[0];
        }

        @Override
        public boolean hasNext() {
            return this.el < IntMap.this.size;
        }

        @Override
        public Entry next() {
            if (this.el >= IntMap.this.size) {
                throw new IllegalStateException("No more elements!");
            }
            if (this.cur != null) {
                Entry e = this.cur;
                this.cur = this.cur.next;
                ++this.el;
                return e;
            }
            do {
                this.cur = IntMap.this.table[++this.idx];
            } while (this.cur == null);
            Entry e = this.cur;
            this.cur = this.cur.next;
            ++this.el;
            return e;
        }

        @Override
        public void remove() {
        }
    }
}

