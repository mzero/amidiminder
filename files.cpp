#include "files.h"

#include <cstdlib>
#include <iostream>
#include <string.h>
#include <sys/stat.h>

namespace {

  std::string rulesFilePath;
  std::string observedFilePath;

  std::string controlSocketPath;


  bool errCheck(int err, const char* op, const std::string& arg) {
    if (err == 0) return false;
    auto errStr = ::strerror(errno); // grab it before anything changes it
    std::cerr << op << " " << arg << ": " << errStr << std::endl;
    return true;
  }

  std::string directory(
      const char* envVar,
      const char* defaultPath,
      bool checkForPresence) {

    std::string dirPath;

    const char* envValue = std::getenv(envVar);
    if (envValue)
      dirPath = envValue;

    bool useDefault = dirPath.empty();
    if (useDefault)
      dirPath = defaultPath;

    if (checkForPresence) {
      std::cerr << envVar << "=" << dirPath
        << (useDefault ? " (defaulted)" : "") << std::endl;

      struct stat statbuf;
      if (!errCheck(stat(dirPath.c_str(), &statbuf), "Checking", dirPath)) {
        if ((statbuf.st_mode & S_IFMT) != S_IFDIR) {
        std::cerr << dirPath << " exists, but is not a direcotry" << std::endl;
        }
      }
    }

    return dirPath;
  }

  void initialize(bool checkForPresence) {
    std::string stateDirPath =
      directory(
        "STATE_DIRECTORY",
        "/var/lib/amidiminder",
        checkForPresence);

    std::string runtimeDirPath =
      directory(
        "RUNTIME_DIRECTORY",
        "/run/amidiminder",
        checkForPresence);

    rulesFilePath     = stateDirPath + "/rules";
    observedFilePath  = stateDirPath + "/observed";

    controlSocketPath = runtimeDirPath + "/control";
  }
}

namespace Files {
  void initializeAsClient()   { initialize(false); }
  void initializeAsService()  { initialize(true); }

  const std::string& rulesFilePath()      { return ::rulesFilePath; }
  const std::string& observedFilePath()   { return ::observedFilePath; }
  const std::string& controlSocketPath()  { return ::controlSocketPath; }
}
