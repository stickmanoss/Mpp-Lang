// MppxxCpp wrapper — files, processes, hashing, JSON and status lines.
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace mppxx {

using Strings = std::vector<std::string>;

/// A failure reported to the user (the driver prints it as `error: ...`).
struct Error {
    std::string message;
};

// MARK: Configuration (from the driver)

struct Config {
    std::string home, cache, swiftc, toolchainKey, selfExe;
};
Config& config();

std::optional<std::string> env(const char* name);
bool quiet();
bool verbose();
bool stderrIsTTY();
void status(std::string_view verb, std::string_view message);
void writeStderr(std::string_view s);

// MARK: Paths and files

namespace path {
std::string join(std::string_view a, std::string_view b);
std::string dirname(std::string_view p);
std::string basename(std::string_view p);
std::string stem(std::string_view p);
std::string ext(std::string_view p);             // ".cpp" (with the dot), or ""
std::string absolute(std::string_view p);
std::string display(std::string_view p);         // relative to the current directory when inside it
}  // namespace path

struct FileInfo {
    long long mtimeNanos = -1, size = -1;
    bool isDirectory = false;
};
std::optional<FileInfo> info(const std::string& p);
bool isFile(const std::string& p);
bool isDirectory(const std::string& p);
void makeDirectories(const std::string& p);
std::optional<std::string> readIfExists(const std::string& p);
std::string read(const std::string& p);             // throws Error
void write(const std::string& p, std::string_view text);   // atomic; throws Error
void writeIfChanged(const std::string& p, std::string_view text);
Strings list(const std::string& dir);                // names, sorted
std::string which(const std::string& name);          // "" if not on PATH

// MARK: Processes

struct RunResult {
    int status = -1;
    std::string output;   // stdout and stderr together
};
RunResult run(const Strings& argv);                   // throws Error if it can't start

// MARK: Text

std::string trim(std::string_view s);                 // spaces, tabs, newlines
std::string trimNewlines(std::string_view s);
Strings split(std::string_view s, char sep, bool keepEmpty = false);
std::string join(const Strings& parts, std::string_view sep);
std::string replaceAll(std::string s, std::string_view from, std::string_view to);
bool startsWith(std::string_view s, std::string_view p);
bool endsWith(std::string_view s, std::string_view p);

/// A 128-bit content hash as 32 hex digits.
std::string stamp(std::string_view s);
/// Swift module name for a file stem.
std::string moduleName(std::string_view raw);

// MARK: JSON output

std::string jsonString(std::string_view s);
std::string jsonArray(const Strings& items);          // of strings
char* toC(const std::string& s);                      // malloc'd copy
char* errorJSON(const std::string& message);

}  // namespace mppxx
