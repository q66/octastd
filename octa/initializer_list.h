/* Initializer list support for OctaSTD.
 *
 * This file is part of OctaSTD. See COPYING.md for futher information.
 */

#ifndef OCTA_INITIALIZER_LIST_H
#define OCTA_INITIALIZER_LIST_H

#include <stddef.h>

#include "octa/range.h"

#ifndef OCTA_ALLOW_CXXSTD
/* must be in std namespace otherwise the compiler won't know about it */
namespace std {

template<typename T>
class initializer_list {
    const T *p_buf;
    octa::Size p_len;

    initializer_list(const T *v, octa::Size n): p_buf(v), p_len(n) {}
public:
    initializer_list(): p_buf(nullptr), p_len(0) {}

    octa::Size size() const { return p_len; }

    const T *begin() const { return p_buf; }
    const T *end() const { return p_buf + p_len; }
};

}
#else
#include <initializer_list>
#endif

namespace octa {

template<typename T> using InitializerList = std::initializer_list<T>;

template<typename T>
octa::PointerRange<const T> each(std::initializer_list<T> init) {
    return octa::PointerRange<const T>(init.begin(), init.end());
}

template<typename T>
octa::PointerRange<const T> ceach(std::initializer_list<T> init) {
    return octa::PointerRange<const T>(init.begin(), init.end());
}

}

#endif