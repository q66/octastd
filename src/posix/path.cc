/* Path implementation bits.
 *
 * This file is part of libostd. See COPYING.md for futher information.
 */

#include <cstdlib>
#include <dirent.h>
#include <sys/stat.h>

#include "ostd/path.hh"

namespace ostd {
namespace fs {

static perms mode_to_perms(mode_t mode) {
    perms ret = perms::none;
    switch (mode & S_IRWXU) {
        case S_IRUSR: ret |= perms::owner_read;
        case S_IWUSR: ret |= perms::owner_write;
        case S_IXUSR: ret |= perms::owner_exec;
        case S_IRWXU: ret |= perms::owner_all;
    }
    switch (mode & S_IRWXG) {
        case S_IRGRP: ret |= perms::group_read;
        case S_IWGRP: ret |= perms::group_write;
        case S_IXGRP: ret |= perms::group_exec;
        case S_IRWXG: ret |= perms::group_all;
    }
    switch (mode & S_IRWXO) {
        case S_IROTH: ret |= perms::others_read;
        case S_IWOTH: ret |= perms::others_write;
        case S_IXOTH: ret |= perms::others_exec;
        case S_IRWXO: ret |= perms::others_all;
    }
    if (mode & S_ISUID) {
        ret |= perms::set_uid;
    }
    if (mode & S_ISGID) {
        ret |= perms::set_gid;
    }
    if (mode & S_ISVTX) {
        ret |= perms::sticky_bit;
    }
    return ret;
}

static file_type mode_to_type(mode_t mode) {
    switch (mode & S_IFMT) {
        case S_IFBLK: return file_type::block;
        case S_IFCHR: return file_type::character;
        case S_IFIFO: return file_type::fifo;
        case S_IFREG: return file_type::regular;
        case S_IFDIR: return file_type::directory;
        case S_IFLNK: return file_type::symlink;
        case S_IFSOCK: return file_type::socket;
    }
    return file_type::unknown;
}

OSTD_EXPORT file_status status(path const &p) {
    struct stat sb;
    if (stat(p.string().data(), &sb) < 0) {
        /* FIXME: throw */
        abort();
    }
    return file_status{mode_to_type(sb.st_mode), mode_to_perms(sb.st_mode)};
}

OSTD_EXPORT file_status symlink_status(path const &p) {
    struct stat sb;
    if (lstat(p.string().data(), &sb) < 0) {
        /* FIXME: throw */
        abort();
    }
    return file_status{mode_to_type(sb.st_mode), mode_to_perms(sb.st_mode)};
}

} /* namespace fs */
} /* namespace ostd */

namespace ostd {
namespace fs {
namespace detail {

static void dir_read_next(DIR *dh, directory_entry &cur, path const &base) {
    struct dirent d;
    struct dirent *o;
    for (;;) {
        if (readdir_r(dh, &d, &o)) {
            /* FIXME: throw */
            abort();
        }
        if (!o) {
            cur.clear();
            return;
        }
        string_range nm{static_cast<char const *>(o->d_name)};
        if ((nm == ".") || (nm == "..")) {
            continue;
        }
        path p{base};
        p.append(nm);
        cur.assign(std::move(p));
        break;
    }
}

/* dir range */

OSTD_EXPORT void dir_range_impl::open(path const &p) {
    DIR *d = opendir(p.string().data());
    if (!d) {
        /* FIXME: throw */
        abort();
    }
    p_dir = p;
    p_handle = d;
    read_next();
}

OSTD_EXPORT void dir_range_impl::close() noexcept {
    closedir(static_cast<DIR *>(p_handle));
}

OSTD_EXPORT void dir_range_impl::read_next() {
    dir_read_next(static_cast<DIR *>(p_handle), p_current, p_dir);
}

/* recursive dir range */

OSTD_EXPORT void rdir_range_impl::open(path const &p) {
    DIR *d = opendir(p.string().data());
    if (!d) {
        /* FIXME: throw */
        abort();
    }
    p_dir = p;
    p_handles.push(d);
    read_next();
}

OSTD_EXPORT void rdir_range_impl::close() noexcept {
    while (!p_handles.empty()) {
        closedir(static_cast<DIR *>(p_handles.top()));
        p_handles.pop();
    }
}

OSTD_EXPORT void rdir_range_impl::read_next() {
    if (p_handles.empty()) {
        return;
    }
    if (is_directory(p_current.path())) {
        /* directory, recurse into it and if it contains stuff, return */
        DIR *nd = opendir(p_current.path().string().data());
        if (!nd) {
            abort();
        }
        directory_entry based = p_current, curd;
        dir_read_next(nd, curd, based);
        if (!curd.path().empty()) {
            p_dir = based;
            p_handles.push(nd);
            p_current = curd;
            return;
        } else {
            closedir(nd);
        }
    }
    /* didn't recurse into a directory, go to next file */
    dir_read_next(static_cast<DIR *>(p_handles.top()), p_current, p_dir);
    /* end of dir, pop while at it */
    if (p_current.path().empty()) {
        closedir(static_cast<DIR *>(p_handles.top()));
        p_handles.pop();
        if (!p_handles.empty()) {
            /* got back to another dir, read next so it's valid */
            p_dir.remove_name();
            dir_read_next(static_cast<DIR *>(p_handles.top()), p_current, p_dir);
        } else {
            p_current.clear();
        }
    }
}

} /* namespace detail */
} /* namesapce fs */
} /* namespace ostd */