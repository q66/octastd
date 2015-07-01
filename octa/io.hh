/* Standard I/O implementation for OctaSTD.
 *
 * This file is part of OctaSTD. See COPYING.md for futher information.
 */

#ifndef OCTA_IO_HH
#define OCTA_IO_HH

#include <stdio.h>

#include "octa/platform.hh"
#include "octa/string.hh"
#include "octa/stream.hh"
#include "octa/format.hh"

namespace octa {

enum class StreamMode {
    read, write, append,
    update = 1 << 2
};

namespace detail {
    static const char *filemodes[] = {
        "rb", "wb", "ab", nullptr, "rb+", "wb+", "ab+"
    };
}

struct FileStream: Stream {
    FileStream(): p_f(), p_owned(false) {}
    FileStream(const FileStream &) = delete;
    FileStream(FileStream &&s): p_f(s.p_f), p_owned(s.p_owned) {
        s.p_f = nullptr;
        s.p_owned = false;
    }

    FileStream(const char *path, StreamMode mode): p_f() {
        open(path, mode);
    }

    template<typename A>
    FileStream(const octa::AnyString<A> &path, StreamMode mode): p_f() {
        open(path, mode);
    }

    FileStream(FILE *f): p_f(f), p_owned(false) {}

    ~FileStream() { close(); }

    FileStream &operator=(const FileStream &) = delete;
    FileStream &operator=(FileStream &&s) {
        close();
        swap(s);
        return *this;
    }

    bool open(const char *path, StreamMode mode) {
        if (p_f) return false;
        p_f = fopen(path, octa::detail::filemodes[octa::Size(mode)]);
        p_owned = true;
        return is_open();
    }

    template<typename A>
    bool open(const octa::AnyString<A> &path, StreamMode mode) {
        return open(path.data(), mode);
    }

    bool open(FILE *f) {
        if (p_f) return false;
        p_f = f;
        p_owned = false;
        return is_open();
    }

    bool is_open() const { return p_f != nullptr; }
    bool is_owned() const { return p_owned; }

    void close() {
        if (p_f && p_owned) fclose(p_f);
        p_f = nullptr;
        p_owned = false;
    }

    bool end() const {
        return feof(p_f) != 0;
    }

    bool seek(StreamOffset pos, StreamSeek whence = StreamSeek::set) {
#ifndef OCTA_PLATFORM_WIN32
        return fseeko(p_f, pos, int(whence)) >= 0;
#else
        return _fseeki64(p_f, pos, int(whence)) >= 0;
#endif
    }

    StreamOffset tell() const {
#ifndef OCTA_PLATFORM_WIN32
        return ftello(p_f);
#else
        return _ftelli64(p_f);
#endif
    }

    bool flush() { return !fflush(p_f); }

    octa::Size read_bytes(void *buf, octa::Size count) {
        return fread(buf, 1, count, p_f);
    }

    octa::Size write_bytes(const void *buf, octa::Size count) {
        return fwrite(buf, 1, count, p_f);
    }

    int getchar() {
        return fgetc(p_f);
    }

    bool putchar(int c) {
        return  fputc(c, p_f) != EOF;
    }

    bool write(const char *s) {
        return fputs(s, p_f) != EOF;
    }

    void swap(FileStream &s) {
        octa::swap(p_f, s.p_f);
        octa::swap(p_owned, s.p_owned);
    }

    FILE *get_file() { return p_f; }

private:
    FILE *p_f;
    bool p_owned;
};

static FileStream in(::stdin);
static FileStream out(::stdout);
static FileStream err(::stderr);

/* no need to call anything from FileStream, prefer simple calls... */

static inline void write(const char *s) {
    fputs(s, ::stdout);
}

template<typename A>
static inline void write(const octa::AnyString<A> &s) {
    fwrite(s.data(), 1, s.size(), ::stdout);
}

template<typename T>
static inline void write(const T &v) {
    octa::write(octa::to_string(v));
}

template<typename T, typename ...A>
static inline void write(const T &v, const A &...args) {
    octa::write(v);
    write(args...);
}

static inline void writeln(const char *s) {
    octa::write(s);
    putc('\n', ::stdout);
}

template<typename A>
static inline void writeln(const octa::AnyString<A> &s) {
    octa::write(s);
    putc('\n', ::stdout);
}

template<typename T>
static inline void writeln(const T &v) {
    octa::writeln(octa::to_string(v));
}

template<typename T, typename ...A>
static inline void writeln(const T &v, const A &...args) {
    octa::write(v);
    write(args...);
    putc('\n', ::stdout);
}

template<typename ...A>
static inline void writef(const char *fmt, const A &...args) {
    char buf[512];
    octa::Size need = octa::formatted_write(octa::detail::FormatOutRange<
        sizeof(buf)>(buf), fmt, args...);
    if (need < sizeof(buf)) {
        fwrite(buf, 1, need, ::stdout);
        return;
    }
    octa::String s;
    s.reserve(need);
    s[need] = '\0';
    octa::formatted_write(s.iter(), fmt, args...);
    fwrite(s.data(), 1, need, ::stdout);
}

template<typename AL, typename ...A>
static inline void writef(const octa::AnyString<AL> &fmt,
                          const A &...args) {
    writef(fmt.data(), args...);
}

template<typename ...A>
static inline void writefln(const char *fmt, const A &...args) {
    writef(fmt, args...);
    putc('\n', ::stdout);
}

template<typename AL, typename ...A>
static inline void writefln(const octa::AnyString<AL> &fmt,
                            const A &...args) {
    writef(fmt, args...);
    putc('\n', ::stdout);
}

} /* namespace octa */

#endif