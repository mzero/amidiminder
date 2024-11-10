#ifndef _INCLUDE_FILES_H_
#define _INCLUDE_FILES_H_

#include <string>

namespace Files {
  void initializeAsService();
  void initializeAsClient();

  const std::string& profileFilePath();
  const std::string& observedFilePath();

  const std::string& controlSocketPath();

  // Note: On error, these functions report to cerr, and exit
  bool fileExists(const std::string& path);
  std::string readFile(const std::string& path);
  void writeFile(const std::string& path, const std::string& contents);
}


#endif // _INCLUDE_FILES_H_
