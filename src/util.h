// Pantomimus - the little things: where things live, file dialogs, opening folders and links, the clock.
#pragma once
#include <string>
#include <vector>

namespace Util {

std::string exeDir();          // with a trailing slash
std::string configDir();       // %APPDATA%\Pantomimus (settings, layout)
std::string documentsDir();    // Documents\Pantomimus (projects, exports)
std::string tempDir();
// where the models are, or will be downloaded: the Preferences folder if set, else beside the program if they're
// there, else an AIModels\pantomimus folder on any drive that has AIModels, else %LOCALAPPDATA%\Pantomimus\models
std::string defaultModelsDir(const std::string& detectorFile);
bool fileExists(const std::string& p);
bool makeDirs(const std::string& p);
long long fileSize(const std::string& p);
std::string fileName(const std::string& p);   // without the folder
std::string stem(const std::string& p);
std::string extension(const std::string& p);  // ".bvh", lower case
std::string join(const std::string& dir, const std::string& name);

// dialogs ("" when cancelled); filters: {"Video", "*.mp4;*.mov"} pairs
std::string openFile(const std::string& title, const std::vector<std::pair<std::string, std::string>>& filters);
std::string saveFile(const std::string& title, const std::string& suggested, const std::vector<std::pair<std::string, std::string>>& filters);
std::string pickFolder(const std::string& title);
void openFolder(const std::string& p);
void openUrl(const std::string& url);

double now();                  // seconds, steady
std::string timestamp();       // 2026-11-23 11-42-05 (for file names)
std::string trim(const std::string& s);
std::string lower(std::string s);

}  // namespace Util
