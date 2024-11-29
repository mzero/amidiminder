#include <algorithm>
#include <alsa/asoundlib.h>
#include <csignal>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <list>
#include <map>
#include <set>
#include <sstream>
#include <sys/epoll.h>

#include "args.h"
#include "files.h"
#include "ipc.h"
#include "msg.h"
#include "rule.h"
#include "seq.h"



namespace {

  enum class RuleSource {
    profile,
    observed,
  };

  const char* ruleSourceName(RuleSource r) {
    switch (r) {
      case RuleSource::profile:    return "profile";
      case RuleSource::observed:   return "observed";
      default:                     return "???";
    }
  }


  struct CandidateConnection {
    const Address& sender;
    const Address& dest;
    const ConnectionRule& rule;
    RuleSource source;
  };

  using CandidateConnections = std::vector<CandidateConnection>;


  void readRules(const std::string& filePath,
    std::string& contents,    // receives contents of the file
    ConnectionRules& rules)   // receives parsed rules
  {
    if (!Files::fileExists(filePath)) {
      Msg::output("Rules file {} dosn't exist, no rules read.", filePath);
      contents.clear();
      rules.clear();
      return;
    }

    std::string newContents = Files::readFile(filePath);

    ConnectionRules newRules;
    if (!parseRules(newContents, newRules))
      throw Msg::runtime_error("Parse error reading rules file {}", filePath);

    contents.swap(newContents);
    rules.swap(newRules);

    Msg::output("Rules file {} read, {} rules.", filePath, rules.size());
    if (Args::detail())
      for (auto& r : rules)
        Msg::detail("    {}", r);
  }

  ConnectionRules::const_iterator
  findRule(const ConnectionRules& rules,
    const Address& sender, const Address& dest)
  {
    auto r = std::find_if(rules.rbegin(), rules.rend(),
      [&](const ConnectionRule& r){ return r.match(sender, dest); });
    return r == rules.rend() ? rules.end() : std::next(r).base();
  }

  enum class FDSource : uint32_t {
    Seq,
    Server,
  };

  void addFDToEpoll(int epollFD, int fd, FDSource src) {
    struct epoll_event evt;
    evt.events = EPOLLIN | EPOLLERR;
    evt.data.u32 = (uint32_t)src;

    if (epoll_ctl(epollFD, EPOLL_CTL_ADD, fd, &evt) != 0) {
      auto e = errno;
      Msg::error("Failed adding to epoll: {}", strerror(e));
      exit(1);
    }
  }

  volatile std::sig_atomic_t caughtSignal = 0;

  void signal_handler(int signal)
    { caughtSignal = signal; }


}



class MidiMinder {
  private:
    Seq seq;
    IPC::Server server;

    ConnectionRules profileRules;
    std::string profileText;

    ConnectionRules observedRules;
    std::string observedText;

    std::map<snd_seq_addr_t, Address> activePorts;
    std::set<snd_seq_connect_t> activeConnections;

    std::set<snd_seq_connect_t> expectedDisconnects;
    std::set<snd_seq_connect_t> expectedConnects;

  public:
    MidiMinder() {
      std::signal(SIGHUP, signal_handler);
      std::signal(SIGINT, signal_handler);
      std::signal(SIGTERM, signal_handler);
      seq.begin();
    }

    ~MidiMinder() {
      seq.end();
      std::signal(SIGHUP, SIG_DFL);
      std::signal(SIGINT, SIG_DFL);
      std::signal(SIGTERM, SIG_DFL);
    }

  private:
    void handleSeqEvent(snd_seq_event_t& ev) {
      Msg::debug("ALSA Seq event: {}", ev);

      switch (ev.type) {
        case SND_SEQ_EVENT_CLIENT_START: {
          std::string name = seq.clientName(ev.data.addr);
          if (name.find("Client-", 0) == 0) {
            // Should use .starts_with(), but that is only in C++20

            // The kernel assigns sprintf(..., "Client-%d", client_num) as the
            // name of a new client. Most clients immediately change the name to
            // something more useful before doing anything else. Some
            // applications (looking at you, PureData), create their ports first
            // then set their client name. As a consequence, This code may see
            // the PORT_START event from the port creation, before the client
            // has changed its name.

            // This is a cheap hack: If we learn about a client before it has
            // changed its name, just sleep for a little bit, and it should be
            // updated by the time we're back.

            // Empirically, on a RPi4b, using PureData as the test app, 10ms
            // wasn't enough, 25ms was. Using 100ms to be safe. We want this as
            // low as reasonable to keep the code being responsive.
            timespec nap = { 0, 100000 };
            nanosleep(&nap, nullptr);
          }
        }

        case SND_SEQ_EVENT_CLIENT_EXIT:
          // We will have received PORT_EXIT events for all ports, so there
          // is nothing left to do here.
          break;

        case SND_SEQ_EVENT_CLIENT_CHANGE:
          // The kernel has a bug in that it never sends this event. If it did
          // this code should look to see if the name of the client has changed
          // and if so, remove and re-add all it's ports under the new name.
          break;

        case SND_SEQ_EVENT_PORT_START: {
          if (Args::outputPortDetails) {
            seq.outputAddrDetails(std::cout, ev.data.addr);
          }
          addPort(ev.data.addr);
          break;
        }

        case SND_SEQ_EVENT_PORT_EXIT: {
          delPort(ev.data.addr);
          break;
        }

        case SND_SEQ_EVENT_PORT_CHANGE: {
          // The kernel has a bug in that it doesn't send this event in most
          // cases. If it did, then this code should look to see if the name
          // or the capabilities we care about have changed, and if so, remove
          // and re-add the port.
          break;
        }

        case SND_SEQ_EVENT_PORT_SUBSCRIBED: {
          if (expectedConnects.erase(ev.data.connect)) break;
          addConnection(ev.data.connect);
          break;
        }

        case SND_SEQ_EVENT_PORT_UNSUBSCRIBED: {
          if (expectedDisconnects.erase(ev.data.connect)) break;
          delConnection(ev.data.connect);
          break;
        }

        default:
          Msg::error("Unknown ALSA Seq event: {}, ignoring.", ev.type);
      }
    }

    void handleResetCommand(IPC::Connection& conn, const IPC::Options& opts);
    void handleLoadCommand(IPC::Connection& conn);
    void handleSaveCommand(IPC::Connection& conn);
    void handleStatusCommand(IPC::Connection& conn);

    void handleConnection() {
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

  void saveObserved() {
    std::ostringstream text;
    for (auto& r : observedRules)
      text << r << '\n';
    observedText = text.str();
    Files::writeFile(Files::observedFilePath(), observedText);
    Msg::debug("Observed rules written.");
  }

  void clearObserved() {
    observedText.clear();
    observedRules.clear();
    saveObserved();
  }

  void resetConnectionsHard() {
  // reset ports & connections from scratch, rescanning ALSA Seq

    std::vector<snd_seq_connect_t> doomed;
    activeConnections.clear();
      // a little afraid to disconnect connections while scanning them!
    seq.scanConnections([&](auto c){
      const Address& sender = knownPort(c.sender);
      const Address& dest = knownPort(c.dest);
      if (sender && dest) // check if it's a connection we would manage
        doomed.push_back(c);
    });
    for (auto& c : doomed) {
      seq.disconnect(c);  // will generate UNSUB events that should be ignored
      expectedDisconnects.insert(c);
    }

    activePorts.clear();
    seq.scanPorts([&](auto p){
      if (Args::outputPortDetails)
        seq.outputAddrDetails(std::cout, p);
      this->addPort(p, true);
    });
  }

  void resetConnectionsSoft() {
  // reset ports & connections without rescanning ALSA Seq
    std::set<snd_seq_connect_t> doomed;
    doomed.swap(activeConnections);
    for (auto& c : doomed) {
      seq.disconnect(c);  // will generate UNSUB events that should be ignored
      expectedDisconnects.insert(c);
    }

    std::map<snd_seq_addr_t, Address> ports;
    ports.swap(activePorts);
    for (auto& p: ports)
      addPort(p.first, true); // does regenreate the Address from Seq::address()
  }


  public:
    void run() {
      readRules();
      resetConnectionsHard();

      int epollFD = epoll_create1(0);
      if (epollFD == -1)
        throw Msg::system_error("epoll_create failed");

      seq.scanFDs([epollFD](int fd){ addFDToEpoll(epollFD, fd, FDSource::Seq); });
      server.scanFDs([epollFD](int fd){ addFDToEpoll(epollFD, fd, FDSource::Server); });

      while (true) {
        switch (caughtSignal) {
          case 0: break;
          case SIGHUP: {
            auto s = caughtSignal;
            caughtSignal = 0;
            Msg::output("Reset requested by signal {}", s);
            resetConnectionsHard();
            break;
          }
          default:
            throw Msg::runtime_error("Interrupted by signal {}", caughtSignal);
        }

        struct epoll_event evt;
        int nfds = epoll_wait(epollFD, &evt, 1, -1);
        if (nfds == -1) {
          if (errno == EINTR) continue;   // this was a signal
          else                throw Msg::system_error("epoll_wait failed");
        }
        if (nfds == 0)
          continue;

        switch ((FDSource)evt.data.u32) {
          case FDSource::Server: {
            handleConnection();
            break;
          }

          case FDSource::Seq: {
            while (snd_seq_event_t* ev = seq.eventInput())
              handleSeqEvent(*ev);
            break;
          }

          default:
            // should never happen... but who cares if it does!
            break;
        }
      }
    }

  private:
    const Address& knownPort(snd_seq_addr_t addr) {
      const auto i = activePorts.find(addr);
      if (i == activePorts.end()) return Address::null;
      return i->second;
    }

    void makeConnection(const CandidateConnection& cc)
    {
      snd_seq_connect_t conn;
      conn.sender = cc.sender.addr;
      conn.dest = cc.dest.addr;
      if (activeConnections.find(conn) == activeConnections.end()) {
        seq.connect(conn.sender, conn.dest);
        expectedConnects.insert(conn);
        activeConnections.insert(conn);
        Msg::output("Connecting {} --> {}\n    by {} rule: {}",
          cc.sender, cc.dest, ruleSourceName(cc.source), cc.rule);
      }
    }

    void considerConnection(
      const Address& sender, const Address& dest,
      const ConnectionRule& rule, RuleSource source, CandidateConnections& ccs)
    {
      if (!rule.isBlockingRule()) {
        CandidateConnection cc = {sender, dest, rule, source};
        ccs.push_back(cc);
      }
      else {
        CandidateConnections filteredCCs;

        for (auto& cc : ccs)
          if (!rule.match(cc.sender, cc.dest))
            filteredCCs.push_back(cc);

        ccs.swap(filteredCCs);
      }
    }

    void connectEachActiveSender(
        const Address& a, const ConnectionRule& rule, RuleSource source, CandidateConnections& ccs)
    {
      for (auto& p : activePorts) {
        auto& b = p.second;
        if (b.canBeSender() && rule.senderMatch(b))
          considerConnection(b, a, rule, source, ccs);
      }
    }

    void connectEachActiveDest(
        const Address& a, const ConnectionRule& rule, RuleSource source, CandidateConnections& ccs)
    {
      for (auto& p : activePorts) {
        auto& b = p.second;
        if (b.canBeDest() && rule.destMatch(b))
          considerConnection(a, b, rule, source, ccs);
      }
    }

    void connectByRule(const Address& a, ConnectionRules& rules, RuleSource source, CandidateConnections& ccs) {
      for (auto& rule : rules) {
        if (a.canBeSender() && rule.senderMatch(a))   connectEachActiveDest(a, rule, source, ccs);
        if (a.canBeDest()   && rule.destMatch(a))     connectEachActiveSender(a, rule, source, ccs);
      }
    }

    // Note: The computation of primary port status assumes that
    // applications will create their ports from zero, in order,
    // and when they delete ports, they delete them all. All
    // applications we tested do this. Should an application
    // create ports with explicit port numbers out of order, or
    // delete low numbered ports, then recreate them... it
    // would logically cause the priamry port to jump around.
    // This code base doesn't handle that case, though invoking
    // the reset function will clear up any mess that was made.

    void addPort(const snd_seq_addr_t& addr, bool fromReset = false) {
      if (knownPort(addr)) return;

      Address a = seq.address(addr);
      if (!a) return;

      bool foundPrimarySender = false;
      bool foundPrimaryDest = false;
      for (const auto& i : activePorts) {
        if (i.first.client == addr.client) {
          foundPrimarySender = foundPrimarySender || i.second.primarySender;
          foundPrimaryDest   = foundPrimaryDest   || i.second.primaryDest;
        }
        if (foundPrimarySender && foundPrimaryDest)
          break;
      }
      if (a.canBeSender() && !foundPrimarySender)   a.primarySender = true;
      if (a.canBeDest() && !foundPrimaryDest)       a.primaryDest = true;

      activePorts[addr] = a;
      Msg::output("{} port: {}", fromReset ? "Reviewing" : "System added", a);

      CandidateConnections candidates;
      connectByRule(a, profileRules, RuleSource::profile, candidates);
      connectByRule(a, observedRules, RuleSource::observed, candidates);
      for (auto& cc : candidates)
        makeConnection(cc);
    }

    void delPort(const snd_seq_addr_t& addr) {
      const Address& port = knownPort(addr);
      if (!port)
        return;

      Msg::output("System removed port: {}", port);

      std::vector<snd_seq_connect_t> doomed;
      for (auto& c : activeConnections) {
        if (c.sender == addr || c.dest == addr) {
          doomed.push_back(c);

          const Address& sender = knownPort(c.sender);
          const Address& dest = knownPort(c.dest);
          if (sender && dest)
            Msg::detail("    disconnected {} --> {}", sender, dest);
        }
      }

      activePorts.erase(addr);
      for (auto& d : doomed)
        activeConnections.erase(d);
    }


    void addConnection(const snd_seq_connect_t& conn) {
      auto i = activeConnections.find(conn);
      if (i != activeConnections.end())
        // already know about this connection
        return;

      const Address& sender = knownPort(conn.sender);
      const Address& dest = knownPort(conn.dest);
      if (!sender || !dest)
        return;

      Msg::output("Observed connection: {} --> {}", sender, dest);

      activeConnections.insert(conn);

      auto oRule = findRule(observedRules, sender, dest);
      if (oRule == observedRules.end())    ; // just continue onward
      else if (!oRule->isBlockingRule())    return; // already known
      else {
        observedRules.erase(oRule);
        saveObserved();
        // and continue onward
      }

      auto pRule = findRule(profileRules, sender , dest);
      if (pRule == profileRules.end())     ; // just continue onward
      else if (!pRule->isBlockingRule())    return; // already known
      else                                  ; // just continue ownard

      // At this point, no rule would add this connection,
      // so add an observed rule for it.
      ConnectionRule c = ConnectionRule::exact(sender, dest);
      observedRules.push_back(c);
      saveObserved();
    }

    void delConnection(const snd_seq_connect_t& conn) {
      // TODO: did we expect this disconnection? do nothing, we made it

      auto i = activeConnections.find(conn);
      if (i == activeConnections.end())
        // don't know anything about this connection
        return;
      activeConnections.erase(i);

      const Address& sender = knownPort(conn.sender);
      const Address& dest = knownPort(conn.dest);
      if (!sender || !dest)
        return;

      Msg::output("Observed connection: {} --> {}", sender, dest);

      auto oRule = findRule(observedRules, sender, dest);
      if (oRule == observedRules.end())    ; // just continue onward
      else if (oRule->isBlockingRule())     return; // already blocked
      else {
        observedRules.erase(oRule);
        saveObserved();
        // and continue onward
      }

      auto pRule = findRule(profileRules, sender , dest);
      if (pRule == profileRules.end())   return; // no rule would ever add it
      else if (pRule->isBlockingRule())   return; // already known
      else                                ; // just continue onward

      // At this point there is a profileRule that would make this connection,
      // so add an observed rule to block it.
      ConnectionRule c = ConnectionRule::exactBlock(sender, dest);
      observedRules.push_back(c);
      saveObserved();
    }

    void readRules() {
      ::readRules(Files::profileFilePath(), profileText, profileRules);
      ::readRules(Files::observedFilePath(), observedText, observedRules);
    }
};


void sendResetCommand() {
  IPC::Client client;
  IPC::Options opts;
  if (Args::keepObserved)   opts.push_back("keepObserved");
  if (Args::resetHard)      opts.push_back("resetHard");
  client.sendCommandAndOptions("reset", opts);
}

void MidiMinder::handleResetCommand(IPC::Connection& conn, const IPC::Options& opts) {
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

void sendLoadCommand() {
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
  if (Args::detail())
    for (auto& r : profileRules)
      Msg::detail("    {}", r);

  clearObserved();
  resetConnectionsSoft();
}

void sendSaveCommand() {
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

void sendStatusCommand() {
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


int main(int argc, char *argv[]) {
  if (!Args::parse(argc, argv))
    return Args::exitCode;

  const char* exitPrefix = "";

  try {
    switch (Args::command) {
      case Args::Command::Help:
        // should never happen, handled in Args::parse()
        break;

      case Args::Command::Check: {
        std::string contents;
        ConnectionRules rules;
        ::readRules(Args::rulesFilePath, contents, rules);
        break;
      }

      case Args::Command::Daemon: {
        exitPrefix = "Fatal: ";
        MidiMinder mm;
        mm.run();
        break;
      }

      case Args::Command::Reset:      sendResetCommand();     break;
      case Args::Command::Load:       sendLoadCommand();      break;
      case Args::Command::Save:       sendSaveCommand();      break;
      case Args::Command::Status:     sendStatusCommand();    break;
    }
  }
  catch (const std::exception& e) {
    if (strlen(e.what()) > 0)
      Msg::error("{}{}", exitPrefix, e.what());
    return 1;
  }

  return 0;
}
