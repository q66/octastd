/* Self-expanding dynamic array implementation for OctaSTD.
 *
 * This file is part of OctaSTD. See COPYING.md for futher information.
 */

#ifndef OCTA_VECTOR_H
#define OCTA_VECTOR_H

#include <string.h>
#include <stddef.h>

#include "octa/type_traits.h"
#include "octa/utility.h"
#include "octa/range.h"
#include "octa/algorithm.h"
#include "octa/initializer_list.h"
#include "octa/memory.h"

namespace octa {

namespace detail {
    template<typename T, typename A, bool = octa::IsEmpty<A>::value>
    struct VectorPair;

    template<typename T, typename A>
    struct VectorPair<T, A, false> { /* non-empty allocator */
        T *p_ptr;
        A  p_a;

        template<typename U>
        VectorPair(T *ptr, U &&a): p_ptr(ptr),
            p_a(octa::forward<U>(a)) {}

        A &get_alloc() { return p_a; }
        const A &get_alloc() const { return p_a; }

        void swap(VectorPair &v) {
            octa::swap(p_ptr, v.p_ptr);
            octa::swap(p_a  , v.p_a  );
        }
    };

    template<typename T, typename A>
    struct VectorPair<T, A, true>: A { /* empty allocator */
        T *p_ptr;

        template<typename U>
        VectorPair(T *ptr, U &&a):
            A(octa::forward<U>(a)), p_ptr(ptr) {}

        A &get_alloc() { return *this; }
        const A &get_alloc() const { return *this; }

        void swap(VectorPair &v) {
            octa::swap(p_ptr, v.p_ptr);
        }
    };
} /* namespace detail */

template<typename T, typename A = octa::Allocator<T>>
class Vector {
    typedef octa::detail::VectorPair<T, A> _vp_type;

    _vp_type p_buf;
    size_t p_len, p_cap;

    void insert_base(size_t idx, size_t n) {
        if (p_len + n > p_cap) reserve(p_len + n);
        p_len += n;
        for (size_t i = p_len - 1; i > idx + n - 1; --i) {
            p_buf.p_ptr[i] = octa::move(p_buf.p_ptr[i - n]);
        }
    }

    template<typename R>
    void ctor_from_range(R &range, octa::EnableIf<
        octa::IsFiniteRandomAccessRange<R>::value, bool
    > = true) {
        octa::RangeSize<R> l = range.size();
        reserve(l);
        p_len = l;
        for (size_t i = 0; !range.empty(); range.pop_front()) {
            octa::allocator_construct(p_buf.get_alloc(),
                &p_buf.p_ptr[i], range.front());
            ++i;
        }
    }

    template<typename R>
    void ctor_from_range(R &range, EnableIf<
        !octa::IsFiniteRandomAccessRange<R>::value, bool
    > = true) {
        size_t i = 0;
        for (; !range.empty(); range.pop_front()) {
            reserve(i + 1);
            octa::allocator_construct(p_buf.get_alloc(),
                &p_buf.p_ptr[i], range.front());
            ++i;
            p_len = i;
        }
    }

    void copy_contents(const Vector &v) {
        if (octa::IsPod<T>()) {
            memcpy(p_buf.p_ptr, v.p_buf.p_ptr, p_len * sizeof(T));
        } else {
            T *cur = p_buf.p_ptr, *last = p_buf.p_ptr + p_len;
            T *vbuf = v.p_buf.p_ptr;
            while (cur != last) {
                octa::allocator_construct(p_buf.get_alloc(),
                   cur++, *vbuf++);
            }
        }
    }

public:
    enum { MIN_SIZE = 8 };

    typedef size_t                 Size;
    typedef ptrdiff_t              Difference;
    typedef       T                Value;
    typedef       T               &Reference;
    typedef const T               &ConstReference;
    typedef       T               *Pointer;
    typedef const T               *ConstPointer;
    typedef PointerRange<      T>  Range;
    typedef PointerRange<const T>  ConstRange;
    typedef A                     Allocator;

    Vector(const A &a = A()): p_buf(nullptr, a), p_len(0), p_cap(0) {}

    explicit Vector(size_t n, const T &val = T(),
    const A &al = A()): Vector(al) {
        p_buf.p_ptr = octa::allocator_allocate(p_buf.get_alloc(), n);
        p_len = p_cap = n;
        T *cur = p_buf.p_ptr, *last = p_buf.p_ptr + n;
        while (cur != last)
            octa::allocator_construct(p_buf.get_alloc(), cur++, val);
    }

    Vector(const Vector &v): p_buf(nullptr,
    octa::allocator_container_copy(v.p_buf.get_alloc())), p_len(0),
    p_cap(0) {
        reserve(v.p_cap);
        p_len = v.p_len;
        copy_contents(v);
    }

    Vector(const Vector &v, const A &a): p_buf(nullptr, a),
    p_len(0), p_cap(0) {
        reserve(v.p_cap);
        p_len = v.p_len;
        copy_contents(v);
    }

    Vector(Vector &&v): p_buf(v.p_buf.p_ptr,
    octa::move(v.p_buf.get_alloc())), p_len(v.p_len), p_cap(v.p_cap) {
        v.p_buf.p_ptr = nullptr;
        v.p_len = v.p_cap = 0;
    }

    Vector(Vector &&v, const A &a) {
        if (a != v.a) {
            p_buf.get_alloc() = a;
            reserve(v.p_cap);
            p_len = v.p_len;
            if (octa::IsPod<T>()) {
                memcpy(p_buf.p_ptr, v.p_buf.p_ptr, p_len * sizeof(T));
            } else {
                T *cur = p_buf.p_ptr, *last = p_buf.p_ptr + p_len;
                T *vbuf = v.p_buf.p_ptr;
                while (cur != last) {
                    octa::allocator_construct(p_buf.get_alloc(), cur++,
                        octa::move(*vbuf++));
                }
            }
            return;
        }
        new (&p_buf) _vp_type(v.p_buf.p_ptr,
            octa::move(v.p_buf.get_alloc()));
        p_len = v.p_len;
        p_cap = v.p_cap;
        v.p_buf.p_ptr = nullptr;
        v.p_len = v.p_cap = 0;
    }

    Vector(InitializerList<T> v, const A &a = A()): Vector(a) {
        size_t l = v.end() - v.begin();
        const T *ptr = v.begin();
        reserve(l);
        for (size_t i = 0; i < l; ++i)
            octa::allocator_construct(p_buf.get_alloc(),
                &p_buf.p_ptr[i], ptr[i]);
        p_len = l;
    }

    template<typename R> Vector(R range, const A &a = A()):
    Vector(a) {
        ctor_from_range(range);
    }

    ~Vector() {
        clear();
        octa::allocator_deallocate(p_buf.get_alloc(), p_buf.p_ptr, p_cap);
    }

    void clear() {
        if (p_len > 0 && !octa::IsPod<T>()) {
            T *cur = p_buf.p_ptr, *last = p_buf.p_ptr + p_len;
            while (cur != last)
                octa::allocator_destroy(p_buf.get_alloc(), cur++);
        }
        p_len = 0;
    }

    Vector &operator=(const Vector &v) {
        if (this == &v) return *this;
        clear();
        reserve(v.p_cap);
        p_len = v.p_len;
        copy_contents(v);
        return *this;
    }

    Vector &operator=(Vector &&v) {
        clear();
        octa::allocator_deallocate(p_buf.get_alloc(), p_buf.p_ptr, p_cap);
        p_len = v.p_len;
        p_cap = v.p_cap;
        p_buf.~_vp_type();
        new (&p_buf) _vp_type(v.disown(), octa::move(v.p_buf.get_alloc()));
        return *this;
    }

    Vector &operator=(InitializerList<T> il) {
        clear();
        size_t ilen = il.end() - il.begin();
        reserve(ilen);
        if (octa::IsPod<T>()) {
            memcpy(p_buf.p_ptr, il.begin(), ilen);
        } else {
            T *tbuf = p_buf.p_ptr, *ibuf = il.begin(),
                *last = il.end();
            while (ibuf != last) {
                octa::allocator_construct(p_buf.get_alloc(),
                    tbuf++, *ibuf++);
            }
        }
        p_len = ilen;
        return *this;
    }

    template<typename R>
    Vector &operator=(R range) {
        clear();
        ctor_from_range(range);
    }

    void resize(size_t n, const T &v = T()) {
        size_t l = p_len;
        reserve(n);
        p_len = n;
        if (octa::IsPod<T>()) {
            for (size_t i = l; i < p_len; ++i) {
                p_buf.p_ptr[i] = T(v);
            }
        } else {
            T *first = p_buf.p_ptr + l;
            T *last  = p_buf.p_ptr + p_len;
            while (first != last)
                octa::allocator_construct(p_buf.get_alloc(), first++, v);
        }
    }

    void reserve(size_t n) {
        if (n <= p_cap) return;
        size_t oc = p_cap;
        if (!oc) {
            p_cap = octa::max(n, size_t(MIN_SIZE));
        } else {
            while (p_cap < n) p_cap *= 2;
        }
        T *tmp = octa::allocator_allocate(p_buf.get_alloc(), p_cap);
        if (oc > 0) {
            if (octa::IsPod<T>()) {
                memcpy(tmp, p_buf.p_ptr, p_len * sizeof(T));
            } else {
                T *cur = p_buf.p_ptr, *tcur = tmp,
                    *last = tmp + p_len;
                while (tcur != last) {
                    octa::allocator_construct(p_buf.get_alloc(), tcur++,
                        octa::move(*cur));
                    octa::allocator_destroy(p_buf.get_alloc(), cur);
                    ++cur;
                }
            }
            octa::allocator_deallocate(p_buf.get_alloc(), p_buf.p_ptr, oc);
        }
        p_buf.p_ptr = tmp;
    }

    T &operator[](size_t i) { return p_buf.p_ptr[i]; }
    const T &operator[](size_t i) const { return p_buf.p_ptr[i]; }

    T &at(size_t i) { return p_buf.p_ptr[i]; }
    const T &at(size_t i) const { return p_buf.p_ptr[i]; }

    T &push(const T &v) {
        if (p_len == p_cap) reserve(p_len + 1);
        octa::allocator_construct(p_buf.get_alloc(),
            &p_buf.p_ptr[p_len], v);
        return p_buf.p_ptr[p_len++];
    }

    T &push() {
        if (p_len == p_cap) reserve(p_len + 1);
        octa::allocator_construct(p_buf.get_alloc(), &p_buf.p_ptr[p_len]);
        return p_buf.p_ptr[p_len++];
    }

    template<typename ...U>
    T &emplace_back(U &&...args) {
        if (p_len == p_cap) reserve(p_len + 1);
        octa::allocator_construct(p_buf.get_alloc(), &p_buf.p_ptr[p_len],
            octa::forward<U>(args)...);
        return p_buf.p_ptr[p_len++];
    }

    void pop() {
        if (!octa::IsPod<T>()) {
            octa::allocator_destroy(p_buf.get_alloc(),
                &p_buf.p_ptr[--p_len]);
        } else {
            --p_len;
        }
    }

    T &front() { return p_buf.p_ptr[0]; }
    const T &front() const { return p_buf.p_ptr[0]; }

    T &back() { return p_buf.p_ptr[p_len - 1]; }
    const T &back() const { return p_buf.p_ptr[p_len - 1]; }

    T *data() { return p_buf.p_ptr; }
    const T *data() const { return p_buf.p_ptr; }

    size_t size() const { return p_len; }
    size_t capacity() const { return p_cap; }

    bool empty() const { return (p_len == 0); }

    bool in_range(size_t idx) { return idx < p_len; }
    bool in_range(int idx) { return idx >= 0 && size_t(idx) < p_len; }
    bool in_range(const T *ptr) {
        return ptr >= p_buf.p_ptr && ptr < &p_buf.p_ptr[p_len];
    }

    T *disown() {
        T *r = p_buf.p_ptr;
        p_buf.p_ptr = nullptr;
        p_len = p_cap = 0;
        return r;
    }

    T *insert(size_t idx, T &&v) {
        insert_base(idx, 1);
        p_buf.p_ptr[idx] = octa::move(v);
        return &p_buf.p_ptr[idx];
    }

    T *insert(size_t idx, const T &v) {
        insert_base(idx, 1);
        p_buf.p_ptr[idx] = v;
        return &p_buf.p_ptr[idx];
    }

    T *insert(size_t idx, size_t n, const T &v) {
        insert_base(idx, n);
        for (size_t i = 0; i < n; ++i) {
            p_buf.p_ptr[idx + i] = v;
        }
        return &p_buf.p_ptr[idx];
    }

    template<typename U>
    T *insert_range(size_t idx, U range) {
        size_t l = range.size();
        insert_base(idx, l);
        for (size_t i = 0; i < l; ++i) {
            p_buf.p_ptr[idx + i] = range.front();
            range.pop_front();
        }
        return &p_buf.p_ptr[idx];
    }

    T *insert(size_t idx, InitializerList<T> il) {
        return insert_range(idx, octa::each(il));
    }

    Range each() {
        return Range(p_buf.p_ptr, p_buf.p_ptr + p_len);
    }
    ConstRange each() const {
        return ConstRange(p_buf.p_ptr, p_buf.p_ptr + p_len);
    }

    void swap(Vector &v) {
        octa::swap(p_len, v.p_len);
        octa::swap(p_cap, v.p_cap);
        p_buf.swap(v.p_buf);
    }
};

} /* namespace octa */

#endif