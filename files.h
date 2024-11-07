#ifndef _INCLUDE_FILES_H_
#define _INCLUDE_FILES_H_

#include <string>

namespace Files {
  void initializeAsService();
  void initializeAsClient();

  const std::string& rulesFilePath();
  const std::string& observedFilePath();

  const std::string& controlSocketPath();
}


#endif // _INCLUDE_FILES_H_
