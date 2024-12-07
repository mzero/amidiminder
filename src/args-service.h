#pragma once

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

    ConnectionLogicTest,
  };
  extern Command command;

  // Check command options
  extern std::string rulesFilePath;

  // Reset command options
  extern bool keepObserved;
  extern bool resetHard;

  extern int exitCode;
  bool parse(int argc, char* argv[]);
}

