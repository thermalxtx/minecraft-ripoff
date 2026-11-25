// POSIX stand-ins for the platform layer so the game logic can be tested headless on Linux.
#include "../src/core/platform.h"
#include <sys/stat.h>
#include <dirent.h>
#include <cstdio>
#include <chrono>
namespace plat {
bool makeDir(const std::string& p) { mkdir(p.c_str(), 0755); return true; }
bool renameFile(const std::string& a, const std::string& b) { return rename(a.c_str(), b.c_str()) == 0; }
std::vector<std::string> listDirs(const std::string& path) {
    std::vector<std::string> out; DIR* d = opendir(path.c_str()); if (!d) return out;
    while (dirent* e = readdir(d)) { std::string n = e->d_name; if (n != "." && n != ".." && e->d_type == DT_DIR) out.push_back(n); }
    closedir(d); return out;
}
bool removeDirRecursive(const std::string& p) { std::string c = "rm -rf '" + p + "'"; return system(c.c_str()) == 0; }
std::string exeDir() { return "./"; }
double timeSeconds() { using namespace std::chrono; return duration<double>(steady_clock::now().time_since_epoch()).count(); }
void showError(const std::string& m) { fprintf(stderr, "%s\n", m.c_str()); }
}
