// Pantomimus - the little things (see util.h)
#include "util.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <ctime>
#include <filesystem>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <commdlg.h>
#include <shellapi.h>
#include <shlobj.h>
#else
#include <unistd.h>
#endif

namespace fs = std::filesystem;

namespace Util {

#ifdef _WIN32
static std::wstring W(const std::string& s) { return fs::u8path(s).wstring(); }
static std::string U(const std::wstring& w) { return fs::path(w).u8string(); }
static std::string knownFolder(REFKNOWNFOLDERID id) {
    PWSTR p = nullptr;
    std::string r;
    if (SUCCEEDED(SHGetKnownFolderPath(id, 0, nullptr, &p))) r = U(p);
    CoTaskMemFree(p);
    return r;
}
#endif

std::string exeDir() {
#ifdef _WIN32
    wchar_t p[MAX_PATH]; GetModuleFileNameW(nullptr, p, MAX_PATH);
    return fs::path(p).parent_path().u8string() + "/";
#else
    char p[4096]; const ssize_t n = readlink("/proc/self/exe", p, sizeof p - 1);
    if (n <= 0) return "./";
    p[n] = 0; return fs::path(p).parent_path().u8string() + "/";
#endif
}

std::string configDir() {
#ifdef _WIN32
    std::string d = knownFolder(FOLDERID_RoamingAppData) + "/Pantomimus";
#else
    const char* x = getenv("XDG_CONFIG_HOME");
    std::string d = x ? std::string(x) + "/pantomimus" : std::string(getenv("HOME") ? getenv("HOME") : ".") + "/.config/pantomimus";
#endif
    makeDirs(d);
    return d;
}

std::string documentsDir() {
#ifdef _WIN32
    std::string d = knownFolder(FOLDERID_Documents) + "/Pantomimus";
#else
    std::string d = std::string(getenv("HOME") ? getenv("HOME") : ".") + "/Pantomimus";
#endif
    makeDirs(d);
    return d;
}

std::string tempDir() {
    std::error_code ec;
    std::string d = (fs::temp_directory_path(ec) / "Pantomimus").u8string();
    makeDirs(d);
    return d;
}

std::string defaultModelsDir(const std::string& detector) {
    const std::string here = exeDir() + "models";
    if (fileExists(join(here, detector))) return here;
#ifdef _WIN32
    // Atelier's convention: big models on a drive's AIModels folder
    const DWORD drives = GetLogicalDrives();
    std::string firstAi;
    for (int i = 2; i < 26; ++i) {
        if (!(drives & (1u << i))) continue;
        const std::string root = std::string(1, (char)('A' + i)) + ":/AIModels";
        if (GetDriveTypeA((std::string(1, (char)('A' + i)) + ":\\").c_str()) != DRIVE_FIXED) continue;
        if (!fs::is_directory(fs::u8path(root))) continue;
        const std::string d = root + "/pantomimus";
        if (fileExists(join(d, detector))) return d;
        if (firstAi.empty()) firstAi = d;
    }
    if (!firstAi.empty()) return firstAi;
    return knownFolder(FOLDERID_LocalAppData) + "/Pantomimus/models";
#else
    return std::string(getenv("HOME") ? getenv("HOME") : ".") + "/.local/share/pantomimus/models";
#endif
}

bool fileExists(const std::string& p) { std::error_code ec; return !p.empty() && fs::is_regular_file(fs::u8path(p), ec); }
bool makeDirs(const std::string& p) { std::error_code ec; fs::create_directories(fs::u8path(p), ec); return fs::is_directory(fs::u8path(p), ec); }
long long fileSize(const std::string& p) { std::error_code ec; const auto n = fs::file_size(fs::u8path(p), ec); return ec ? -1 : (long long)n; }
std::string fileName(const std::string& p) { return fs::u8path(p).filename().u8string(); }
std::string stem(const std::string& p) { return fs::u8path(p).stem().u8string(); }
std::string extension(const std::string& p) { return lower(fs::u8path(p).extension().u8string()); }
std::string join(const std::string& dir, const std::string& name) { return (fs::u8path(dir) / fs::u8path(name)).u8string(); }

#ifdef _WIN32
static std::wstring filterString(const std::vector<std::pair<std::string, std::string>>& filters) {
    std::wstring f;
    for (const auto& p : filters) { f += W(p.first + " (" + p.second + ")"); f.push_back(0); f += W(p.second); f.push_back(0); }
    f += L"All files (*.*)"; f.push_back(0); f += L"*.*"; f.push_back(0); f.push_back(0);
    return f;
}
#endif

std::string openFile(const std::string& title, const std::vector<std::pair<std::string, std::string>>& filters) {
#ifdef _WIN32
    wchar_t buf[4096] = {0};
    const std::wstring f = filterString(filters), t = W(title);
    OPENFILENAMEW o{}; o.lStructSize = sizeof o; o.lpstrFilter = f.c_str(); o.lpstrFile = buf; o.nMaxFile = 4096; o.lpstrTitle = t.c_str();
    o.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    return GetOpenFileNameW(&o) ? U(buf) : std::string();
#else
    std::string cmd = "zenity --file-selection --title=\"" + title + "\" 2>/dev/null";
    FILE* p = popen(cmd.c_str(), "r"); if (!p) return "";
    char b[4096] = {0}; std::string r = fgets(b, sizeof b, p) ? b : ""; pclose(p);
    return trim(r);
#endif
}

std::string saveFile(const std::string& title, const std::string& suggested, const std::vector<std::pair<std::string, std::string>>& filters) {
#ifdef _WIN32
    wchar_t buf[4096] = {0};
    const std::wstring s = W(suggested);
    wcsncpy(buf, s.c_str(), 4095);
    const std::wstring f = filterString(filters), t = W(title);
    std::wstring ext;
    if (!filters.empty()) { const std::string e = filters[0].second; const size_t d = e.find('.'); if (d != std::string::npos) ext = W(e.substr(d + 1, e.find(';') == std::string::npos ? std::string::npos : e.find(';') - d - 1)); }
    OPENFILENAMEW o{}; o.lStructSize = sizeof o; o.lpstrFilter = f.c_str(); o.lpstrFile = buf; o.nMaxFile = 4096; o.lpstrTitle = t.c_str();
    o.lpstrDefExt = ext.empty() ? nullptr : ext.c_str();
    o.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    return GetSaveFileNameW(&o) ? U(buf) : std::string();
#else
    std::string cmd = "zenity --file-selection --save --confirm-overwrite --filename=\"" + suggested + "\" --title=\"" + title + "\" 2>/dev/null";
    FILE* p = popen(cmd.c_str(), "r"); if (!p) return "";
    char b[4096] = {0}; std::string r = fgets(b, sizeof b, p) ? b : ""; pclose(p);
    return trim(r);
#endif
}

std::string pickFolder(const std::string& title) {
#ifdef _WIN32
    const std::wstring t = W(title);
    BROWSEINFOW bi{}; bi.lpszTitle = t.c_str(); bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    PIDLIST_ABSOLUTE id = SHBrowseForFolderW(&bi);
    if (!id) return "";
    wchar_t p[MAX_PATH] = {0};
    SHGetPathFromIDListW(id, p);
    CoTaskMemFree(id);
    return U(p);
#else
    std::string cmd = "zenity --file-selection --directory --title=\"" + title + "\" 2>/dev/null";
    FILE* p = popen(cmd.c_str(), "r"); if (!p) return "";
    char b[4096] = {0}; std::string r = fgets(b, sizeof b, p) ? b : ""; pclose(p);
    return trim(r);
#endif
}

void openFolder(const std::string& p) {
#ifdef _WIN32
    ShellExecuteW(nullptr, L"open", W(p).c_str(), nullptr, nullptr, SW_SHOWNORMAL);
#else
    const std::string c = "xdg-open \"" + p + "\" >/dev/null 2>&1 &"; (void)!system(c.c_str());
#endif
}

void openUrl(const std::string& url) { openFolder(url); }

double now() { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }

std::string timestamp() {
    const std::time_t t = std::time(nullptr);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    char b[64]; std::strftime(b, sizeof b, "%Y-%m-%d %H-%M-%S", &tm);
    return b;
}

std::string trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && (unsigned char)s[a] <= ' ') ++a;
    while (b > a && (unsigned char)s[b - 1] <= ' ') --b;
    return s.substr(a, b - a);
}
std::string lower(std::string s) { std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)std::tolower(c); }); return s; }

}  // namespace Util
