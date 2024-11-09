#ifndef _INCLUDE_ARGS_H_
#define _INCLUDE_ARGS_H_

#include <string>

namespace Args {

  enum class Command {
    Help,
    Minder,
    Check,

    CommTest,
  };
  extern Command command;

  // Generic options

  // Minder command options
  extern bool outputPortDetails;

  // Check command options
  extern std::string rulesFilePath;

  extern int exitCode;
  bool parse(int argc, char* argv[]);
}

#endif // _INCLUDE_ARGS_H_
