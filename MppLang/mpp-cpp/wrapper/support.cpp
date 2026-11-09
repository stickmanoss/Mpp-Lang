// MppxxCpp wrapper — files, processes, hashing, JSON and status lines.

#include "support.hpp"

#include <algorithm>
#include <cerrno>
#include <cctype>
#include <climits>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <spawn.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

extern char** environ;

namespace mppxx {

Config& config() {
    static Config c;
    return c;
}

std::optional<std::string> env(const char* name) {
    const char* v = std::getenv(name);
    if (!v || !*v) return std::nullopt;
    return std::string(v);
}
bool quiet() { return env("MPP_QUIET").value_or("") == "1"; }
bool verbose() { return env("MPP_VERBOSE").value_or("") == "1"; }
bool stderrIsTTY() { return isatty(2) != 0; }

void writeStderr(std::string_view s) {
    while (!s.empty()) {
        const ssize_t n = ::write(2, s.data(), s.size());
        if (n <= 0) return;
        s.remove_prefix(static_cast<std::size_t>(n));
    }
}

void status(std::string_view verb, std::string_view message) {
    if (quiet()) return;
    std::string v(verb.size() < 12 ? 12 - verb.size() : 0, ' ');
    v += verb;
    writeStderr(stderrIsTTY() ? "\x1b[1;32m" + v + "\x1b[0m " + std::string(message) + "\n" : v + " " + std::string(message) + "\n");
}

// MARK: - Paths

namespace path {
std::string join(std::string_view a, std::string_view b) {
    if (a.empty()) return std::string(b);
    if (!b.empty() && b.front() == '/') return std::string(b);
    std::string out(a);
    if (out.back() != '/') out += '/';
    out += b;
    return out;
}
std::string dirname(std::string_view p) {
    const auto k = p.rfind('/');
    if (k == std::string_view::npos) return ".";
    if (k == 0) return "/";
    return std::string(p.substr(0, k));
}
std::string basename(std::string_view p) {
    const auto k = p.rfind('/');
    return std::string(k == std::string_view::npos ? p : p.substr(k + 1));
}
std::string ext(std::string_view p) {
    const std::string b = basename(p);
    const auto k = b.rfind('.');
    return k == std::string::npos || k == 0 ? "" : b.substr(k);
}
std::string stem(std::string_view p) {
    const std::string b = basename(p), e = ext(b);
    return b.substr(0, b.size() - e.size());
}
static std::string cwd() {
    char buf[PATH_MAX];
    return getcwd(buf, sizeof buf) ? std::string(buf) : std::string(".");
}
std::string absolute(std::string_view p) { return !p.empty() && p.front() == '/' ? std::string(p) : join(cwd(), p); }
std::string display(std::string_view p) {
    std::string base = cwd();
    if (base.back() != '/') base += '/';
    return startsWith(p, base) ? std::string(p.substr(base.size())) : std::string(p);
}
}  // namespace path

// MARK: - Files

std::optional<FileInfo> info(const std::string& p) {
    struct stat st{};
    if (::stat(p.c_str(), &st) != 0) return std::nullopt;
    FileInfo i;
#if defined(__APPLE__)
    i.mtimeNanos = static_cast<long long>(st.st_mtimespec.tv_sec) * 1000000000LL + st.st_mtimespec.tv_nsec;
#else
    i.mtimeNanos = static_cast<long long>(st.st_mtim.tv_sec) * 1000000000LL + st.st_mtim.tv_nsec;
#endif
    i.size = static_cast<long long>(st.st_size);
    i.isDirectory = S_ISDIR(st.st_mode);
    return i;
}
bool isFile(const std::string& p) { const auto i = info(p); return i && !i->isDirectory; }
bool isDirectory(const std::string& p) { const auto i = info(p); return i && i->isDirectory; }

void makeDirectories(const std::string& p) {
    if (p.empty() || isDirectory(p)) return;
    makeDirectories(path::dirname(p));
    ::mkdir(p.c_str(), 0755);
}

std::optional<std::string> readIfExists(const std::string& p) {
    const int fd = ::open(p.c_str(), O_RDONLY);
    if (fd < 0) return std::nullopt;
    std::string out;
    char buf[65536];
    for (ssize_t n; (n = ::read(fd, buf, sizeof buf)) > 0;) out.append(buf, static_cast<std::size_t>(n));
    ::close(fd);
    return out;
}
std::string read(const std::string& p) {
    if (auto s = readIfExists(p)) return *s;
    throw Error{"cannot read " + p + ": " + std::strerror(errno)};
}
void write(const std::string& p, std::string_view text) {
    makeDirectories(path::dirname(p));
    const std::string tmp = p + ".tmp" + std::to_string(::getpid());
    const int fd = ::open(tmp.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) throw Error{"cannot write " + p + ": " + std::strerror(errno)};
    std::string_view rest = text;
    while (!rest.empty()) {
        const ssize_t n = ::write(fd, rest.data(), rest.size());
        if (n <= 0) { ::close(fd); ::unlink(tmp.c_str()); throw Error{"cannot write " + p}; }
        rest.remove_prefix(static_cast<std::size_t>(n));
    }
    ::close(fd);
    if (::rename(tmp.c_str(), p.c_str()) != 0) { ::unlink(tmp.c_str()); throw Error{"cannot replace " + p}; }
}
void writeIfChanged(const std::string& p, std::string_view text) {
    if (const auto old = readIfExists(p); old && *old == text) return;
    write(p, text);
}
Strings list(const std::string& dir) {
    Strings out;
    if (DIR* d = ::opendir(dir.c_str())) {
        while (const dirent* e = ::readdir(d)) {
            const std::string n = e->d_name;
            if (n != "." && n != "..") out.push_back(n);
        }
        ::closedir(d);
    }
    std::sort(out.begin(), out.end());
    return out;
}
std::string which(const std::string& name) {
    if (name.find('/') != std::string::npos) return ::access(name.c_str(), X_OK) == 0 ? name : "";
    for (const auto& dir : split(env("PATH").value_or("/usr/bin:/bin"), ':')) {
        const std::string p = path::join(dir, name);
        if (isFile(p) && ::access(p.c_str(), X_OK) == 0) return p;
    }
    return "";
}

// MARK: - Processes

RunResult run(const Strings& argv) {
    if (argv.empty()) throw Error{"nothing to run"};
    const std::string exe = which(argv[0]);
    if (exe.empty()) throw Error{argv[0] + ": command not found"};
    int fds[2];
    if (::pipe(fds) != 0) throw Error{"pipe failed"};
    posix_spawn_file_actions_t fa;
    posix_spawn_file_actions_init(&fa);
    posix_spawn_file_actions_adddup2(&fa, fds[1], 1);
    posix_spawn_file_actions_adddup2(&fa, fds[1], 2);
    posix_spawn_file_actions_addclose(&fa, fds[0]);
    posix_spawn_file_actions_addclose(&fa, fds[1]);
    std::vector<char*> args;
    for (const auto& a : argv) args.push_back(::strdup(a.c_str()));
    args.push_back(nullptr);
    pid_t pid = 0;
    const int rc = ::posix_spawn(&pid, exe.c_str(), &fa, nullptr, args.data(), environ);
    posix_spawn_file_actions_destroy(&fa);
    for (char* a : args) std::free(a);
    ::close(fds[1]);
    if (rc != 0) { ::close(fds[0]); throw Error{"cannot run " + exe + ": " + std::strerror(rc)}; }
    RunResult r;
    char buf[65536];
    for (ssize_t n; (n = ::read(fds[0], buf, sizeof buf)) > 0;) r.output.append(buf, static_cast<std::size_t>(n));
    ::close(fds[0]);
    int st = 0;
    while (::waitpid(pid, &st, 0) < 0 && errno == EINTR) {}
    r.status = WIFEXITED(st) ? WEXITSTATUS(st) : 128 + (WIFSIGNALED(st) ? WTERMSIG(st) : 0);
    return r;
}

// MARK: - Text

static bool isSpace(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; }
std::string trim(std::string_view s) {
    while (!s.empty() && isSpace(s.front())) s.remove_prefix(1);
    while (!s.empty() && isSpace(s.back())) s.remove_suffix(1);
    return std::string(s);
}
std::string trimNewlines(std::string_view s) {
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r' || s.back() == ' ')) s.remove_suffix(1);
    while (!s.empty() && (s.front() == '\n' || s.front() == ' ')) s.remove_prefix(1);
    return std::string(s);
}
Strings split(std::string_view s, char sep, bool keepEmpty) {
    Strings out;
    std::size_t start = 0;
    for (std::size_t i = 0; i <= s.size(); ++i) {
        if (i == s.size() || s[i] == sep) {
            if (keepEmpty || i > start) out.emplace_back(s.substr(start, i - start));
            start = i + 1;
        }
    }
    return out;
}
std::string join(const Strings& parts, std::string_view sep) {
    std::string out;
    for (std::size_t i = 0; i < parts.size(); ++i) { if (i) out += sep; out += parts[i]; }
    return out;
}
std::string replaceAll(std::string s, std::string_view from, std::string_view to) {
    if (from.empty()) return s;
    std::size_t at = 0;
    while ((at = s.find(from, at)) != std::string::npos) { s.replace(at, from.size(), to); at += to.size(); }
    return s;
}
bool startsWith(std::string_view s, std::string_view p) { return s.substr(0, p.size()) == p; }
bool endsWith(std::string_view s, std::string_view p) { return s.size() >= p.size() && s.substr(s.size() - p.size()) == p; }

std::string stamp(std::string_view s) {
    // Two independent 64-bit FNV-1a passes, each finished with a mix.
    auto fnv = [&](std::uint64_t h) {
        for (const unsigned char c : s) { h ^= c; h *= 0x100000001b3ULL; }
        h ^= h >> 33; h *= 0xff51afd7ed558ccdULL; h ^= h >> 33; h *= 0xc4ceb9fe1a85ec53ULL; h ^= h >> 33;
        return h;
    };
    const std::uint64_t a = fnv(0xcbf29ce484222325ULL), b = fnv(0x6d70705f73746d70ULL);
    static const char* hex = "0123456789abcdef";
    std::string out(32, '0');
    for (int i = 0; i < 16; ++i) { out[15 - i] = hex[(a >> (4 * i)) & 15]; out[31 - i] = hex[(b >> (4 * i)) & 15]; }
    return out;
}

std::string moduleName(std::string_view raw) {
    std::string out;
    for (const char c : raw) out += (std::isalnum(static_cast<unsigned char>(c)) || c == '_') ? c : '_';
    if (out.empty() || std::isdigit(static_cast<unsigned char>(out[0]))) out = "_" + out;
    return out;
}

// MARK: - JSON

std::string jsonString(std::string_view s) {
    std::string out = "\"";
    for (const unsigned char c : s) {
        switch (c) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (c < 0x20) { static const char* hex = "0123456789abcdef"; out += "\\u00"; out += hex[c >> 4]; out += hex[c & 15]; }
            else out += static_cast<char>(c);
        }
    }
    return out + "\"";
}
std::string jsonArray(const Strings& items) {
    std::string out = "[";
    for (std::size_t i = 0; i < items.size(); ++i) { if (i) out += ","; out += jsonString(items[i]); }
    return out + "]";
}
char* toC(const std::string& s) {
    char* p = static_cast<char*>(std::malloc(s.size() + 1));
    std::memcpy(p, s.data(), s.size());
    p[s.size()] = 0;
    return p;
}
char* errorJSON(const std::string& message) { return toC("{\"error\":" + jsonString(message) + "}"); }

}  // namespace mppxx
