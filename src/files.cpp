#include "files.h"

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <sys/stat.h>

#include "msg.h"


namespace {

  std::string profileFilePath;
  std::string observedFilePath;

  std::string controlSocketPath;

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
      Msg::output("{}={}{}", envVar, dirPath,
        (useDefault ? " (defaulted)" : ""));

      struct stat statbuf;
      auto e = stat(dirPath.c_str(), &statbuf);
      if (e < 0)
        throw Msg::system_error("Checking directory {}", dirPath);

      if ((statbuf.st_mode & S_IFMT) != S_IFDIR)
        throw Msg::runtime_error("Checking directory {}: not a directory", dirPath);
    }

    return dirPath;
  }

  void initialize(bool checkForPresence) {
    std::string stateDirPath =
      directory(
        "STATE_DIRECTORY",
        "/var/lib/midiminder",
        checkForPresence);

    std::string runtimeDirPath =
      directory(
        "RUNTIME_DIRECTORY",
        "/run/midiminder",
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
    if (err != 0)
      throw Msg::system_error("Checking for {}", path);

    if ((statbuf.st_mode & S_IFMT) != S_IFREG)
      throw Msg::runtime_error("Checking for {}: not a regular file", path);

    return true;
  }

  std::string readFile(const std::string& path) {
    std::ifstream file(path);
    if (!file.good())
      throw Msg::system_error("Could not read {}", path);

    std::stringstream ss;
    ss << file.rdbuf();
    return ss.str();
  }

  void writeFile(const std::string& path, const std::string& contents) {
    std::string tempPath = path + ".save";

    std::ofstream file(tempPath);
    if (!file.good())
      throw Msg::system_error("Could not write {}", tempPath);

    file << contents;
    file.flush();
    // Should call fsync() here with the file descriptor underlying the file.
    // But, there is no way in current GCC implementation to get the FD.
    // file.rdbuf()->fd() used to work, but it is gone now.  ¯\_(ツ)_/¯
    file.close();

    int err = std::rename(tempPath.c_str(), path.c_str());
    if (err != 0)
      throw Msg::system_error("Could not rename {} to {}", tempPath, path);
  }

  std::string readUserFile(const std::string& path) {
    if (path == "-") {
      std::stringstream ss;
      ss << std::cin.rdbuf();
      return ss.str();
    }
    else
      return readFile(path);
  }

  void writeUserFile(const std::string& path, const std::string& contents) {
    if (path == "-") {
      std::cout << contents;
      std::cout.flush();
    }
    else
      writeFile(path, contents);
  }
}
