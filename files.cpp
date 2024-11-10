#include "files.h"

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string.h>   // for ::stderror
#include <sys/stat.h>

namespace {

  std::string profileFilePath;
  std::string observedFilePath;

  std::string controlSocketPath;


  void errCheck(int err, const char* op, const std::string& arg) {
    if (err < 0) {
      auto errStr = ::strerror(errno); // grab it before anything changes it
      std::cerr << op << " " << arg << ": " << errStr << std::endl;
      std::exit(1);
    }
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
      errCheck(stat(dirPath.c_str(), &statbuf), "Checking", dirPath);

      if ((statbuf.st_mode & S_IFMT) != S_IFDIR) {
        std::cerr << dirPath << " exists, but it is not a directory" << std::endl;
        std::exit(1);
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

    profileFilePath   = stateDirPath + "/profile.rules";
    observedFilePath  = stateDirPath + "/observed.rules";

    controlSocketPath = runtimeDirPath + "/control.socket";
  }
}

namespace Files {
  void initializeAsClient()   { initialize(false); }
  void initializeAsService()  { initialize(true); }

  const std::string& profileFilePath()    { return ::profileFilePath; }
  const std::string& observedFilePath()   { return ::observedFilePath; }
  const std::string& controlSocketPath()  { return ::controlSocketPath; }

  bool fileExists(const std::string& path) {
    struct stat statbuf;
    auto err = stat(path.c_str(), &statbuf);
    if (err < 0 && errno == ENOENT) return false;
    errCheck(err, "Checking", path);

    if ((statbuf.st_mode & S_IFMT) != S_IFREG) {
      std::cerr << path << " exists, but it is not a file" << std::endl;
      std::exit(1);
    }

    return true;
  }

  std::string readFile(const std::string& path) {
    std::ifstream file(path);
    if (!file.good()) {
      std::string errStr = ::strerror(errno);
      std::cerr << "Could not read " << path << ", " << errStr << std::endl;
      std::exit(1);
    }

    std::stringstream ss;
    ss << file.rdbuf();
    return ss.str();
  }

  void writeFile(const std::string& path, const std::string& contents) {
    std::ofstream file(path);
    if (!file.good()) {
      std::string errStr = ::strerror(errno);
      std::cerr << "Could not write " << path << ", " << errStr << std::endl;
      std::exit(1);
    }

    file << contents;
  }
}
