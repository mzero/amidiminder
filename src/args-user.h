#pragma once

#include <string>

namespace Args {

  enum class Command {
    Help,

    List,
    View,
    Connect,
    Disconnect,
  };
  extern Command command;

  // List command options
  extern bool listClients;
  extern bool listPorts;
  extern bool listConnections;

  extern bool listAll;
  extern bool listPlain;
  extern bool listDetails;

  extern bool listNumericSort;

  // Connect and Disconnect args
  extern std::string portSender;
  extern std::string portDest;

  extern int exitCode;
  bool parse(int argc, char* argv[]);
}
