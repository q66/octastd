/* Process handling implementation bits.
 * For Windows systems only, other implementations are stored elsewhere.
 *
 * This file is part of libostd. See COPYING.md for futher information.
 */

#include "ostd/platform.hh"

#ifndef OSTD_PLATFORM_WIN32
#  error "Incorrect platform"
#endif

#include <cstddef>
#include <cstdlib>
#include <cerrno>
#include <system_error>
#include <string>
#include <memory>
#include <new>

#include <fcntl.h>
#include <windows.h>

#include "ostd/process.hh"
#include "ostd/format.hh"

#include "ostd/filesystem.hh"

namespace fs = ostd::filesystem;

namespace ostd {
namespace detail {

OSTD_EXPORT void split_args_impl(
    string_range const &str, void (*func)(string_range, void *), void *data
) {
    if (!str.size()) {
        return;
    }
    std::unique_ptr<wchar_t[]> wstr{new wchar_t[str.size() + 1]};
    memset(wstr.get(), 0, (str.size() + 1) * sizeof(wchar_t));

    if (!MultiByteToWideChar(
        CP_UTF8, 0, str.data(), str.size(), wstr.get(), str.size() + 1
    )) {
        throw word_error{"unicode conversion failed"};
    }

    int argc = 0;
    wchar_t **pwargs = CommandLineToArgvW(wstr.get(), &argc);

    if (!pwargs) {
        throw word_error{"command line parsing failed"};
    }

    /* if anything throws, make sure stuff gets freed */
    struct wchar_guard {
        void operator()(wchar_t **p) const {
            LocalFree(p);
        }
    };
    std::unique_ptr<wchar_t *, wchar_guard> wguard{pwargs};

    for (int i = 0; i < argc; ++i) {
        wchar_t *arg = pwargs[i];
        std::size_t arglen = wcslen(arg);

        std::size_t req = 0;
        if (!(req = WideCharToMultiByte(
            CP_UTF8, 0, arg, arglen + 1, nullptr, 0, nullptr, nullptr
        ))) {
            throw word_error{"unicode conversion failed"};
        }

        std::unique_ptr<char[]> buf{new char[req]};
        if (!WideCharToMultiByte(
            CP_UTF8, 0, arg, arglen + 1, buf.get(), req, nullptr, nullptr
        )) {
            throw word_error{"unicode conversion failed"};
        }

        func(string_range{buf.get(), buf.get() + req - 1}, data);
    }
}

} /* namespace detail */

struct pipe {
    HANDLE p_r = nullptr, p_w = nullptr;

    ~pipe() {
        if (p_r) {
            CloseHandle(p_r);
        }
        if (p_w) {
            CloseHandle(p_w);
        }
    }

    void open(subprocess_stream use, SECURITY_ATTRIBUTES &sa, bool read) {
        if (use != subprocess_stream::PIPE) {
            return;
        }
        if (!CreatePipe(&p_r, &p_w, &sa, 0)) {
            throw subprocess_error{"could not open pipe"};
        }
        if (!SetHandleInformation(read ? p_r : p_w, HANDLE_FLAG_INHERIT, 0)) {
            throw subprocess_error{"could not set pipe parameters"};
        }
    }

    void fdopen(file_stream &s, bool read) {
        int fd = _open_osfhandle(
            reinterpret_cast<intptr_t>(read ? p_r : p_w),
            read ? _O_RDONLY : 0
        );
        if (fd < 0) {
            throw subprocess_error{"could not open redirected stream"};
        }
        if (read) {
            p_r = nullptr;
        } else {
            p_w = nullptr;
        }
        auto p = _fdopen(fd, read ? "r" : "w");
        if (!p) {
            _close(fd);
            throw subprocess_error{"could not open redirected stream"};
        }
        s.open(p, [](FILE *f) {
            std::fclose(f);
        });
    }
};

/* because there is no way to have CreateProcess do a lookup in standard
 * paths AND specify a custom separate argv[0], we need to implement the
 * path resolution ourselves; fortunately the standard filesystem API
 * makes this kind of easy, but it's still a lot of code I'd rather
 * not write... oh well
 */
static std::wstring resolve_file(wchar_t const *cmd) {
    /* a reused buffer, TODO: allow longer paths */
    wchar_t buf[1024];

    auto is_maybe_exec = [](fs::path const &p) {
        auto st = fs::status(p);
        return (fs::is_regular_file(st) || fs::is_symlink(st));
    };

    fs::path p{cmd};
    /* deal with some easy cases */
    if ((p.filename() != p) || (p == L".") || (p == L"..")) {
        return cmd;
    }
    /* no extension appends .exe as is done normally */
    if (!p.has_extension()) {
        p.replace_extension(L".exe");
    }
    /* the directory from which the app loaded */
    if (GetModuleFileNameW(nullptr, buf, sizeof(buf))) {
        fs::path rp{buf};
        rp.replace_filename(p);
        if (is_maybe_exec(rp)) {
            return rp.native();
        }
    }
    /* the current directory */
    {
        auto rp = fs::path{L"."} / p;
        if (is_maybe_exec(rp)) {
            return rp.native();
        }
    }
    /* the system directory */
    if (GetSystemDirectoryW(buf, sizeof(buf))) {
        auto rp = fs::path{buf} / p;
        if (is_maybe_exec(rp)) {
            return rp.native();
        }
    }
    /* the windows directory */
    if (GetWindowsDirectoryW(buf, sizeof(buf))) {
        auto rp = fs::path{buf} / p;
        if (is_maybe_exec(rp)) {
            return rp.native();
        }
    }
    /* the PATH envvar */
    std::size_t req = GetEnvironmentVariableW(L"PATH", buf, sizeof(buf));
    if (req) {
        wchar_t *envp = buf;
        std::vector<wchar_t> dynbuf;
        if (req > sizeof(buf)) {
            dynbuf.reserve(req);
            for (;;) {
                req = GetEnvironmentVariableW(
                    L"PATH", dynbuf.data(), dynbuf.capacity()
                );
                if (!req) {
                    return cmd;
                }
                if (req > dynbuf.capacity()) {
                    dynbuf.reserve(req);
                } else {
                    envp = dynbuf.data();
                    break;
                }
            }
        }
        for (;;) {
            auto sp = wcschr(envp, L';');
            fs::path rp;
            if (!sp) {
                rp = fs::path{envp} / p;
            } else if (sp == envp) {
                envp = sp + 1;
                continue;
            } else {
                rp = fs::path{envp, sp} / p;
                envp = sp + 1;
            }
            if (is_maybe_exec(rp)) {
                return rp.native();
            }
        }
    }
    /* nothing found */
    return cmd;
}

/* windows follows a dumb set of rules for parsing command line params;
 * a single \ is normally interpreted literally, unless it precedes a ",
 * in which case it acts as an escape character for the quotation mark;
 * if multiple backslashes precedes the quotation mark, each pair is
 * treated as a single backslash
 *
 * we need to replicate this awful behavior here, hence the extra code
 */
static std::string concat_args(std::vector<std::string> const &args) {
    std::string ret;
    for (auto &s: args) {
        if (!ret.empty()) {
            ret += ' ';
        }
        ret += '\"';
        for (char const *sp = s.data();;) {
            char const *p = strpbrk(sp, "\"\\");
            if (!p) {
                ret += sp;
                break;
            }
            ret.append(sp, p);
            if (*p == '\"') {
                /* not preceded by \, so it's safe */
                ret += "\\\"";
                ++p;
            } else {
                /* handle any sequence of \ optionally followed by a " */
                char const *op = p;
                while (*p == '\\') {
                    ++p;
                }
                if (*p == '\"') {
                    /* double all the backslashes plus one for the " */
                    ret.append((p - op) * 2 + 1, '\\');
                    ret += '\"';
                } else {
                    ret.append(p - op, '\\');
                }
            }
            sp = p;
        }
        ret += '\"';
    }
    return ret;
}

OSTD_EXPORT void subprocess::open_impl(
    std::string const &cmd, std::vector<std::string> const &args, bool use_path
) {
    if (use_in == subprocess_stream::STDOUT) {
        throw subprocess_error{"could not redirect stdin to stdout"};
    }

    /* pipes */

    SECURITY_ATTRIBUTES sa;
    sa.nLength = sizeof(SECURITY_ATTRIBUTES);
    sa.bInheritHandle = true;
    sa.lpSecurityDescriptor = nullptr;

    pipe pipe_in, pipe_out, pipe_err;

    pipe_in.open(use_in, sa, false);
    pipe_out.open(use_out, sa, true);
    pipe_err.open(use_err, sa, true);

    /* process creation */

    PROCESS_INFORMATION pi;
    STARTUPINFOW si;

    memset(&pi, 0, sizeof(PROCESS_INFORMATION));
    memset(&si, 0, sizeof(STARTUPINFOW));

    si.cb = sizeof(STARTUPINFOW);

    if (use_in == subprocess_stream::PIPE) {
        si.hStdInput = pipe_in.p_r;
        pipe_in.fdopen(in, false);
    } else {
        si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
        if (si.hStdInput == INVALID_HANDLE_VALUE) {
            throw subprocess_error{"could not get standard input handle"};
        }
    }
    if (use_out == subprocess_stream::PIPE) {
        si.hStdOutput = pipe_out.p_w;
        pipe_out.fdopen(out, true);
    } else {
        si.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
        if (si.hStdOutput == INVALID_HANDLE_VALUE) {
            throw subprocess_error{"could not get standard output handle"};
        }
    }
    if (use_err == subprocess_stream::PIPE) {
        si.hStdError = pipe_err.p_w;
        pipe_err.fdopen(err, true);
    } else if (use_err == subprocess_stream::STDOUT) {
        si.hStdError = si.hStdOutput;
    } else {
        si.hStdError = GetStdHandle(STD_ERROR_HANDLE);
        if (si.hStdError == INVALID_HANDLE_VALUE) {
            throw subprocess_error{"could not get standard error handle"};
        }
    }
    si.dwFlags |= STARTF_USESTDHANDLES;

    std::wstring cmdpath;
    /* convert and optionally resolve PATH and other lookup locations */
    {
        std::unique_ptr<wchar_t[]> wcmd{new wchar_t[cmd.size() + 1]};
        if (!MultiByteToWideChar(
            CP_UTF8, 0, cmd.data(), cmd.size() + 1, wcmd.get(), cmd.size() + 1
        )) {
            throw subprocess_error{"unicode conversion failed"};
        }
        if (!use_path) {
            cmdpath = wcmd.get();
        } else {
            cmdpath = resolve_file(wcmd.get());
        }
    }

    /* cmdline gets an ordinary conversion... */
    auto astr = concat_args(args);

    std::unique_ptr<wchar_t[]> cmdline{new wchar_t[astr.size() + 1]};
    if (!MultiByteToWideChar(
        CP_UTF8, 0, astr.data(), astr.size() + 1, cmdline.get(), astr.size() + 1
    )) {
        throw subprocess_error{"unicode conversion failed"};
    }

    /* owned by CreateProcess, do not close explicitly */
    pipe_in.p_r = nullptr;
    pipe_out.p_w = nullptr;
    pipe_err.p_w = nullptr;

    auto success = CreateProcessW(
        cmdpath.data(),
        cmdline.get(),
        nullptr,        /* process security attributes */
        nullptr,        /* primary thread security attributes */
        true,           /* inherit handles */
        0,              /* creation flags */
        nullptr,        /* use parent env */
        nullptr,        /* use parent cwd */
        &si,
        &pi
    );

    if (!success) {
        throw subprocess_error{"could not execute subprocess"};
    }

    CloseHandle(pi.hThread);
    p_current = static_cast<void *>(pi.hProcess);
}

OSTD_EXPORT void subprocess::reset() {
    p_current = nullptr;
}

OSTD_EXPORT int subprocess::close() {
    if (!p_current) {
        throw subprocess_error{"no child process"};
    }

    HANDLE *proc = static_cast<HANDLE *>(p_current);

    if (WaitForSingleObject(proc, INFINITE) == WAIT_FAILED) {
        CloseHandle(proc);
        reset();
        throw subprocess_error{"child process wait failed"};
    }

    DWORD ec = 0;
    if (!GetExitCodeProcess(proc, &ec)) {
        CloseHandle(proc);
        reset();
        throw subprocess_error{"could not retrieve exit code"};
    }

    CloseHandle(proc);
    reset();

    return int(ec);
}

OSTD_EXPORT void subprocess::move_data(subprocess &i) {
    std::swap(p_current, i.p_current);
}

OSTD_EXPORT void subprocess::swap_data(subprocess &i) {
    move_data(i);
}

} /* namespace ostd */
