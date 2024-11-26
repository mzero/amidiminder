#ifndef _INCLUDE_ARGS_H_
#define _INCLUDE_ARGS_H_

#include <string>

namespace Args {

  enum class Command {
    Help,
    Daemon,
    Check,

    Reset,
    Load,
    Save,

    CommTest,
  };
  extern Command command;

  // Generic options
  extern int verbosity;
  inline bool quiet()   { return verbosity <= 0; }
  inline bool output()  { return verbosity >= 1; }
  inline bool chatty()  { return verbosity >= 2; }
  inline bool debug()   { return verbosity >= 3; }

  // Minder command options
  extern bool outputPortDetails;

  // Check command options
  extern std::string rulesFilePath;

  // Reset command options
  extern bool keepObserved;
  extern bool resetHard;

  extern int exitCode;
  bool parse(int argc, char* argv[]);
}

#endif // _INCLUDE_ARGS_H_
