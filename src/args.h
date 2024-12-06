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

    Status,

    List,
    View,

    ConnectionLogicTest,
  };
  extern Command command;

  // Generic options
  extern int verbosity;
  inline bool quiet()   { return verbosity <= 0; }
  inline bool output()  { return verbosity >= 1; }
  inline bool detail()  { return verbosity >= 2; }
  inline bool debug()   { return verbosity >= 3; }


  // Check command options
  extern std::string rulesFilePath;

  // Reset command options
  extern bool keepObserved;
  extern bool resetHard;

  // List command options
  extern bool listClients;
  extern bool listPorts;
  extern bool listConnections;

  extern bool listAll;
  extern bool listPlain;
  extern bool listDetails;

  extern bool listNumericSort;


  extern int exitCode;
  bool parse(int argc, char* argv[]);
}

#endif // _INCLUDE_ARGS_H_
