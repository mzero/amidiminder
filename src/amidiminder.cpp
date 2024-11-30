#include "amidiminder.h"

#include <algorithm>
#include <csignal>
#include <sstream>
#include <sys/epoll.h>

#include "args.h"
#include "files.h"
#include "msg.h"


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
  using ActivePorts = std::map<snd_seq_addr_t, Address>;

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
      const Address& a, const ConnectionRule& rule, RuleSource source,
      const ActivePorts& activePorts, CandidateConnections& ccs)
  {
    for (auto& p : activePorts) {
      auto& b = p.second;
      if (b.canBeSender() && rule.senderMatch(b))
        considerConnection(b, a, rule, source, ccs);
    }
  }

  void connectEachActiveDest(
      const Address& a, const ConnectionRule& rule, RuleSource source,
      const ActivePorts& activePorts, CandidateConnections& ccs)
  {
    for (auto& p : activePorts) {
      auto& b = p.second;
      if (b.canBeDest() && rule.destMatch(b))
        considerConnection(a, b, rule, source, ccs);
    }
  }

  void connectByRule(const Address& a,
    ConnectionRules& rules, RuleSource source,
    const ActivePorts& activePorts, CandidateConnections& ccs) {
    for (auto& rule : rules) {
      if (a.canBeSender() && rule.senderMatch(a))
        connectEachActiveDest(a, rule, source, activePorts, ccs);

      if (a.canBeDest()   && rule.destMatch(a))
        connectEachActiveSender(a, rule, source, activePorts, ccs);
    }
  }




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

  enum class Found {
    NoRule,
    ConnectRule,
    DisallowRule,
  };

  std::pair<Found, ConnectionRules::const_iterator>
  findRule(const ConnectionRules& rules,
    const Address& sender, const Address& dest)
  {
    auto r = std::find_if(rules.rbegin(), rules.rend(),
      [&](const ConnectionRule& r){ return r.match(sender, dest); });
    auto i = r == rules.rend() ? rules.end() : std::next(r).base();
    auto f =
      i == rules.end()
        ? Found::NoRule
        : (r->isBlockingRule() ? Found::DisallowRule: Found::ConnectRule);
    return {f, i};
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

  void signal_handler(int signal) {
    caughtSignal = signal;
    if (signal != SIGHUP)
      std::signal(signal, SIG_DFL);
  }

}



MidiMinder::MidiMinder() {
  std::signal(SIGHUP, signal_handler);
  std::signal(SIGINT, signal_handler);
  std::signal(SIGTERM, signal_handler);
  seq.begin();
}

MidiMinder::~MidiMinder() {
  seq.end();
  std::signal(SIGHUP, SIG_DFL);
  std::signal(SIGINT, SIG_DFL);
  std::signal(SIGTERM, SIG_DFL);
}

void MidiMinder::run() {
  readRules(Files::profileFilePath(), profileText, profileRules);
  readRules(Files::observedFilePath(), observedText, observedRules);
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

void MidiMinder::handleSeqEvent(snd_seq_event_t& ev) {
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


void MidiMinder::saveObserved() {
  std::ostringstream text;
  for (auto& r : observedRules)
    text << r << '\n';
  observedText = text.str();
  Files::writeFile(Files::observedFilePath(), observedText);
  Msg::debug("Observed rules written.");
}

void MidiMinder::clearObserved() {
  observedText.clear();
  observedRules.clear();
  saveObserved();
}

void MidiMinder::resetConnectionsHard() {
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

void MidiMinder::resetConnectionsSoft() {
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


const Address& MidiMinder::knownPort(snd_seq_addr_t addr) {
  const auto i = activePorts.find(addr);
  if (i == activePorts.end()) return Address::null;
  return i->second;
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

void MidiMinder::addPort(const snd_seq_addr_t& addr, bool fromReset ) {
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
  connectByRule(a, profileRules, RuleSource::profile, activePorts, candidates);
  connectByRule(a, observedRules, RuleSource::observed, activePorts, candidates);
  for (auto& cc : candidates) {
    snd_seq_connect_t conn = {cc.sender.addr, cc.dest.addr};
    if (activeConnections.find(conn) == activeConnections.end()) {
      seq.connect(conn.sender, conn.dest);
      expectedConnects.insert(conn);
      activeConnections.insert(conn);
      Msg::output("Connecting {} --> {}\n    by {} rule: {}",
        cc.sender, cc.dest, ruleSourceName(cc.source), cc.rule);
    }
  }
}

void MidiMinder::delPort(const snd_seq_addr_t& addr) {
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


void MidiMinder::addConnection(const snd_seq_connect_t& conn) {
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

  auto [oFind, oRule] = findRule(observedRules, sender, dest);
  auto [pFind, pRule] = findRule(profileRules, sender , dest);

  bool removeObsRule = false;
  bool addNewObsRule = false;

  switch (oFind) {
    case Found::NoRule:
      if (pFind == Found::ConnectRule)
        Msg::output("    already have a profile rule {}", *pRule);
      else
        addNewObsRule = true;
      break;

    case Found::ConnectRule:
      Msg::output("    already have an observed rule {}", *oRule);
      if (pFind == Found::ConnectRule) {
        Msg::output("    removing, as also have a profile rule {}", *pRule);
        removeObsRule = true;
      }
      break;

    case Found::DisallowRule:
      Msg::output("    removing observed disallow rule {}", *oRule);
      removeObsRule = true;
      switch (pFind) {
        case Found::NoRule:
          Msg::output("    no expected profile rule found");
          addNewObsRule = true;
          break;
        case Found::ConnectRule:
          break;
        case Found::DisallowRule:
          Msg::output("    also have a profile disallow rule {}", *pRule);
          addNewObsRule = true;
      }
  }

  if (removeObsRule)
    observedRules.erase(oRule);

  if (addNewObsRule) {
    ConnectionRule c = ConnectionRule::exact(sender, dest);
    observedRules.push_back(c);
    Msg::output("    adding observed rule {}", c);
  }

  if (removeObsRule || addNewObsRule)
    saveObserved();
}

void MidiMinder::delConnection(const snd_seq_connect_t& conn) {
  auto i = activeConnections.find(conn);
  if (i == activeConnections.end())
    // don't know anything about this connection
    return;
  activeConnections.erase(i);

  const Address& sender = knownPort(conn.sender);
  const Address& dest = knownPort(conn.dest);
  if (!sender || !dest)
    return;

  Msg::output("Observed disconnection: {} --> {}", sender, dest);

  auto [oFind, oRule] = findRule(observedRules, sender, dest);
  auto [pFind, pRule] = findRule(profileRules, sender , dest);

  bool removeObsRule = false;
  bool addNewObsRule = false;

  switch (oFind) {
    case Found::NoRule:
      switch (pFind) {
        case Found::NoRule:
          Msg::output("    no rules found, doing nothing");
          break;
        case Found::ConnectRule:
          addNewObsRule = true;
          break;
        case Found::DisallowRule:
          Msg::output("    already have a profile rule {}", *pRule);
          break;
      }
      break;

    case Found::ConnectRule:
      Msg::output("    removing observed rule {}", *oRule);
      removeObsRule = true;
      if (pFind == Found::ConnectRule) {
        Msg::output("    also have a profile rule {}", *pRule);
        addNewObsRule = true;
      }
      break;

    case Found::DisallowRule:
      Msg::output("    already have an observed rule {}", *oRule);
      switch (pFind) {
        case Found::NoRule:
          Msg::output("    but no profile rule, so removing");
          removeObsRule = true;
          break;
        case Found::ConnectRule:
          break;
        case Found::DisallowRule:
          Msg::output("    removing, as also have a profile rule {}", *pRule);
          removeObsRule = true;
      }
      break;
  }

  if (removeObsRule)
    observedRules.erase(oRule);

  if (addNewObsRule) {
    ConnectionRule c = ConnectionRule::exactBlock(sender, dest);
    observedRules.push_back(c);
    Msg::output("    adding observed rule {}", c);
  }

  if (removeObsRule || addNewObsRule)
    saveObserved();
}
