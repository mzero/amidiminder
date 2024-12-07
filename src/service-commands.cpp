#include "service.h"

#include <iomanip>
#include <sstream>

#include "args-service.h"
#include "files.h"
#include "msg.h"



void MidiMinder::checkCommand() {
  std::string contents = Files::readUserFile(Args::rulesFilePath);
  ConnectionRules rules;
  if (!parseRules(contents, rules))
    throw Msg::runtime_error("Rules had parse errors.");

  Msg::output("Parsed {} rule(s).", rules.size());
  if (Msg::detail())
    for (auto& r : rules)
      Msg::detail("    {}", r);
}


void MidiMinder::sendResetCommand() {
  IPC::Client client;
  IPC::Options opts;
  if (Args::keepObserved)   opts.push_back("keepObserved");
  if (Args::resetHard)      opts.push_back("resetHard");
  client.sendCommandAndOptions("reset", opts);
}

void MidiMinder::handleResetCommand(
  IPC::Connection& /* conn */, const IPC::Options& opts)
{
  bool keepObserved = false;
  bool resetHard = false;
  for (auto& o : opts) {
    if (o == "keepObserved")          keepObserved = true;
    else if (o == "resetHard")        resetHard = true;
    else
      Msg::error("Option to reset command not recognized: {}, ignoring.", o);
  }

  if (!keepObserved)
    clearObserved();

  if (resetHard)
    resetConnectionsHard();
  else
    resetConnectionsSoft();
}

void MidiMinder::sendLoadCommand() {
  std::string newContents = Files::readUserFile(Args::rulesFilePath);
  ConnectionRules newRules;
  if (!parseRules(newContents, newRules))
    throw Msg::runtime_error("Did not load rules due to errors.");

  IPC::Client client;
  client.sendCommand("load");

  std::istringstream newFile(newContents);
  client.sendFile(newFile);
}

void MidiMinder::handleLoadCommand(IPC::Connection& conn) {
  std::stringstream newFile;
  conn.receiveFile(newFile);

  std::string newContents = newFile.str();
  ConnectionRules newRules;
  if (!parseRules(newFile, newRules)) {
    Msg::error("Received profile rules didn't parse, ignoring.");
    return;
  }

  Files::writeFile(Files::profileFilePath(), newContents);
  profileText.swap(newContents);
  profileRules.swap(newRules);

  Msg::output("Loading profile, {} rules.", profileRules.size());
  if (Msg::detail())
    for (auto& r : profileRules)
      Msg::detail("    {}", r);

  clearObserved();
  resetConnectionsSoft();
}

void MidiMinder::sendSaveCommand() {
  IPC::Client client;
  client.sendCommand("save");

  std::stringstream saveFile;
  client.receiveFile(saveFile);

  Files::writeUserFile(Args::rulesFilePath, saveFile.str());
}

void MidiMinder::handleSaveCommand(IPC::Connection& conn) {
  std::stringstream combinedProfile;
  if (!profileText.empty()) {
    combinedProfile << "# Profile rules:\n";
    combinedProfile << profileText;

  }
  if (!observedText.empty()) {
    combinedProfile << "# Observed rules:\n";
    combinedProfile << observedText;
  }
  if (profileText.empty() && observedText.empty()) {
    combinedProfile << "# No rules defined.\n";
  }

  conn.sendFile(combinedProfile);
}

void MidiMinder::sendStatusCommand() {
  IPC::Client client;
  client.sendCommand("status");
  client.receiveFile(std::cout);
}

void MidiMinder::handleStatusCommand(IPC::Connection& conn) {
  std::stringstream report;
  auto w = std::setw(4);
  report << "Daemon is running.\n";
  report << w << profileRules.size()        << " profile rules.\n";
  report << w << observedRules.size()       << " observed rules.\n";
  report << w << activePorts.size()         << " active ports.\n";
  report << w << activeConnections.size()   << " active connections\n";
  conn.sendFile(report);
}


void MidiMinder::handleConnection() {
  try {
    auto ac = server.accept();
    if (!ac.has_value()) return;
    auto conn = std::move(ac.value());

    auto msg = conn.receiveCommandAndOptions();
    auto command = msg.first;
    auto& options = msg.second;

    if      (command == "reset") handleResetCommand(conn, options);
    else if (command == "load")  handleLoadCommand(conn);
    else if (command == "save")  handleSaveCommand(conn);
    else if (command == "status")  handleStatusCommand(conn);
    else
      Msg::error("Unrecognized user command \"{}\", ignoring.", command);
  }
  catch (const IPC::SocketError& se) {
    Msg::error("Client connection failed: {}, ignoring", se.what());
  }
}
