#ifndef _INCLUDE_ARGS_H_
#define _INCLUDE_ARGS_H_

#include <string>

namespace Args {

  enum class Command {
    Help,
    Minder,
    Check,
  };
  extern Command command;

  // Generic options
  extern std::string rulesFilePath;

  // Minder command options
  extern bool outputPortDetails;

  extern int exitCode;
  bool parse(int argc, char* argv[]);
}

#endif // _INCLUDE_ARGS_H_
