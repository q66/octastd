/** @addtogroup Utilities
 * @{
 */

/** @file filesystem.hh
 *
 * @brief Filesystem abstraction module.
 *
 * This module defines the namespace ostd::filesystem, which is either
 * std::experimental::filesystem or std::filesystem depending on which
 * is available. For portable applications, only use the subset of the
 * module covered by both versions.
 *
 * It also provides range integration for directory iterators and
 * ostd::format_traits specialization for std::filesystem::path.
 *
 * Additionally, it implements glob matching following POSIX with its
 * own extensions (mainly recursive glob matching via `**`).
 *
 * @include listdir.cc
 *
 * @copyright See COPYING.md in the project tree for further information.
 */

#ifndef OSTD_FILESYSTEM_HH
#define OSTD_FILESYSTEM_HH

#include <utility>

#if __has_include(<filesystem>)
#  include <filesystem>
namespace ostd {
    namespace filesystem = std::filesystem;
}
#elif __has_include(<experimental/filesystem>)
#  include <experimental/filesystem>
namespace ostd {
    namespace filesystem = std::experimental::filesystem;
}
#else
#  error "Unsupported platform"
#endif

#include "ostd/range.hh"
#include "ostd/format.hh"

namespace ostd {

/** @addtogroup Utilities
 * @{
 */

/** @brief Range integration for std::filesystem::directory_iterator.
 *
 * Allows directory iterators to be made into ranges via ostd::iter().
 *
 * @see ostd::ranged_traits<filesystem::recursive_directory_iterator>
 */
template<>
struct ranged_traits<filesystem::directory_iterator> {
    /** @brief The range type for the iterator. */
    using range = iterator_range<filesystem::directory_iterator>;

    /** @brief Creates a range for the iterator. */
    static range iter(filesystem::directory_iterator const &r) {
        return range{filesystem::begin(r), filesystem::end(r)};
    }
};

/** @brief Range integration for std::filesystem::recursive_directory_iterator.
 *
 * Allows recursive directory iterators to be made into ranges via ostd::iter().
 *
 * @see ostd::ranged_traits<filesystem::directory_iterator>
 */
template<>
struct ranged_traits<filesystem::recursive_directory_iterator> {
    /** @brief The range type for the iterator. */
    using range = iterator_range<filesystem::recursive_directory_iterator>;

    /** @brief Creates a range for the iterator. */
    static range iter(filesystem::recursive_directory_iterator const &r) {
        return range{filesystem::begin(r), filesystem::end(r)};
    }
};

/** @brief ostd::format_traits specialization for std::filesystem::path.
 *
 * This allows paths to be formatted as strings. The value is formatted
 * as if `path.string()` was formatted, using the exact ostd::format_spec.
 */
template<>
struct format_traits<filesystem::path> {
    /** @brief Formats the path's string value.
     *
     * This calls exactly
     *
     * ~~~{.cc}
     * fs.format_value(writer, p.string());
     * ~~~
     */
    template<typename R>
    static void to_format(
        filesystem::path const &p, R &writer, format_spec const &fs
    ) {
        fs.format_value(writer, p.string());
    }
};

namespace detail {
    inline bool glob_matches_filename(
        typename filesystem::path::value_type const *fname,
        typename filesystem::path::value_type const *wname
    ) {
        /* skip any matching prefix if present */
        if (*wname && (*wname != '*')) {
            for (;;) {
                /* wildcard/other escapes */
                if (*wname == '\\') {
                    ++wname;
                }
                if (!*wname || (*wname == '*')) {
                    break;
                }
                if (*fname++ != *wname++) {
                    return false;
                }
            }
        }
        /* skip wildcards; a wildcard matches 0 or more */
        if (*wname == '*') {
            while (*wname == '*') {
                ++wname;
            }
            /* was trailing so everything matches */
            if (!*wname) {
                return true;
            }
        }
        /* prefix skipping matched entire filename */
        if (!*fname) {
            return true;
        }
        /* empty pattern and non-empty filename */
        if (!*wname) {
            return false;
        }
        /* keep incrementing filename until it matches */
        while (*fname) {
            if (glob_matches_filename(fname, wname)) {
                return true;
            }
            ++fname;
        }
        return false;
    }

    template<typename OR>
    inline void glob_match_impl(
        OR &out,
        typename filesystem::path::iterator beg,
        typename filesystem::path::iterator &end,
        filesystem::path pre
    ) {
        while (beg != end) {
            auto cur = *beg;
            auto *cs = cur.c_str();
            /* this part of the path might contain wildcards */
            for (auto c = *cs; c; c = *++cs) {
                /* escape sequences for wildcards */
                if (c == '\\') {
                    if (!*++cs) {
                        break;
                    }
                    continue;
                }
                auto ip = pre;
                if (ip.empty()) {
                    ip = ".";
                }
                /* ** as a name does recursive expansion */
                if ((c == '*') && (*(cs + 1) == '*') && !*(cs + 2)) {
                    ++beg;
                    filesystem::recursive_directory_iterator it{ip};
                    for (auto &de: it) {
                        /* followed by more path, only consider dirs */
                        auto dp = de.path();
                        if ((beg != end) && !filesystem::is_directory(dp)) {
                            continue;
                        }
                        /* otherwise also match files */
                        auto p = dp.filename();
                        if (
                            !glob_matches_filename(p.c_str(), cur.c_str())
                        ) {
                            continue;
                        }
                        glob_match_impl(out, beg, end, dp);
                    }
                    return;
                }
                /* a regular * wildcard */
                if (c == '*') {
                    ++beg;
                    filesystem::directory_iterator it{ip};
                    for (auto &de: it) {
                        auto dp = de.path();
                        auto p = dp.filename();
                        if (
                            !glob_matches_filename(p.c_str(), cur.c_str())
                        ) {
                            continue;
                        }
                        glob_match_impl(out, beg, end, dp);
                    }
                    return;
                }
            }
            pre /= cur;
            ++beg;
        }
        out.put(pre);
    }
}

template<typename OR>
inline OR &&glob_match(OR &&out, filesystem::path const &path) {
    auto pend = path.end();
    detail::glob_match_impl(out, path.begin(), pend, filesystem::path{});
    return std::forward<OR>(out);
}

/** @} */

} /* namespace ostd */

#endif

/** @} */
