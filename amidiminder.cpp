#include <algorithm>
#include <alsa/asoundlib.h>
#include <fstream>
#include <iostream>
#include <list>
#include <map>
#include <set>
#include <sstream>
#include <sys/epoll.h>

#include "args.h"
#include "files.h"
#include "ipc.h"
#include "rule.h"
#include "seq.h"



namespace {

  enum class Reason {
    byRule,
    observed,
  };

  void output(std::ostream&s, Reason r) {
    switch (r) {
      case Reason:: byRule:     s << "by rule";  break;
      case Reason:: observed:   s << "observed"; break;
    }
  }

  inline std::ostream& operator<<(std::ostream& s, Reason r)
    { output(s, r); return s; }

  struct CandidateConnection {
    const Address& sender;
    const Address& dest;
    const ConnectionRule& rule;
  };

  using CandidateConnections = std::vector<CandidateConnection>;


  void readRules(const std::string& filePath,
    std::string& contents,    // receives contents of the file
    ConnectionRules& rules)   // receives parsed rules
  {
    if (!Files::fileExists(filePath)) {
      std::cout << "rules file " << filePath << " doesn't exist, no rules read" << std::endl;
      contents.clear();
      rules.clear();
      return;
    }

    contents = Files::readFile(filePath);
    std::cout << "reading rules from " << filePath << std::endl;

    rules.clear();
    std::istringstream rulesStream(contents);
    if (!parseRulesFile(rulesStream, rules)) {
      std::cerr << "parse error reading rules" << std::endl;
      std::exit(1);
    }

    std::cout << "read " << rules.size() << " rules" << std::endl;
    for (auto& r : rules)
      std::cout << "    " << r << std::endl;
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
      std::cerr << "Failed adding to epoll, " << strerror(errno) << std::endl;
      exit(1);
    }
  }

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
    std::map<snd_seq_connect_t, Reason> activeConnections;


  public:
    MidiMinder() {
      seq.begin();
    }

    ~MidiMinder() {
      seq.end();
    }

  private:
    void handleSeqEvent(snd_seq_event_t& ev) {
      switch (ev.type) {
        case SND_SEQ_EVENT_CLIENT_START:
        case SND_SEQ_EVENT_CLIENT_EXIT:
        case SND_SEQ_EVENT_CLIENT_CHANGE:
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
          std::cout << "port change " << ev.data.addr << std::endl;
          // FIXME: treat this as a add/remove?
          break;
        }

        case SND_SEQ_EVENT_PORT_SUBSCRIBED: {
          addConnection(ev.data.connect);
          break;
        }

        case SND_SEQ_EVENT_PORT_UNSUBSCRIBED: {
          delConnection(ev.data.connect);
          break;
        }

        default: {
          std::cout << "unknown event ("
              << std::dec << static_cast<int>(ev.type) << ")" << std::endl;
        }
      }
    }

    void handleResetCommand(IPC::Connection& conn);
    void handleLoadCommand(IPC::Connection& conn);
    void handleSaveCommand(IPC::Connection& conn);
    void handleCommTestCommand(IPC::Connection& conn);

    void handleConnection(IPC::Connection& conn) {
      std::string command = conn.receiveCommand();
      std::cout << "Received client command: " << command << std::endl;

      if      (command == "reset") handleResetCommand(conn);
      else if (command == "load")  handleLoadCommand(conn);
      else if (command == "save")  handleSaveCommand(conn);
      else if (command == "ahoy")  handleCommTestCommand(conn);
      else
        std::cerr << "No idea what to do with that!" << std::endl;
    }

  public:
    void run() {
      readRules();

      seq.scanPorts([&](auto p){
        if (Args::outputPortDetails)
          seq.outputAddrDetails(std::cout, p);
        this->addPort(p);
      });
      seq.scanConnections([&](auto c){ this->addConnection(c); });

      int epollFD = epoll_create1(0);
      if (epollFD == -1) {
        auto errStr = strerror(errno);
        std::cerr << "Couldn't create epoll, " << errStr << std::endl;
        std::exit(1);
      }

      seq.scanFDs([epollFD](int fd){ addFDToEpoll(epollFD, fd, FDSource::Seq); });
      server.scanFDs([epollFD](int fd){ addFDToEpoll(epollFD, fd, FDSource::Server); });

      while (true) {
        struct epoll_event evt;
        int nfds = epoll_wait(epollFD, &evt, 1, -1);
        if (nfds == -1) {
          std::cerr << "Failed epoll_wait, " << strerror(errno) << std::endl;
          exit(1);
        };
        if (nfds == 0)
          continue;

        switch ((FDSource)evt.data.u32) {
          case FDSource::Server: {
            auto conn = server.accept();
            if (conn)
              handleConnection(conn.value());
            break;
          }

          case FDSource::Seq: {
            snd_seq_event_t *ev = seq.eventInput();
            if (ev)
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
    Address knownPort(snd_seq_addr_t addr) {
      auto i = activePorts.find(addr);
      if (i == activePorts.end()) return {};
      return i->second;
    }

    void makeConnection(const CandidateConnection& cc, Reason r)
    {
      snd_seq_connect_t conn;
      conn.sender = cc.sender.addr;
      conn.dest = cc.dest.addr;
      if (activeConnections.find(conn) == activeConnections.end()) {
        seq.connect(conn.sender, conn.dest);
        activeConnections[conn] = r;
        std::cout << "connecting " << cc.sender << " --> " << cc.dest;
        switch (r) {
          case Reason::byRule:
            std::cout << ", by rule: " << cc.rule << std::endl;
            break;

          case Reason::observed:
            std::cout << ", restoring prior connection" << std::endl;
            break;
        }
      }
    }

    void considerConnection(
      const Address& sender, const Address& dest,
      const ConnectionRule& rule, CandidateConnections& ccs)
    {
      if (!rule.isBlockingRule()) {
        CandidateConnection cc = {sender, dest, rule};
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
        const Address& a, const ConnectionRule& rule, CandidateConnections& ccs)
    {
      for (auto& p : activePorts) {
        auto& b = p.second;
        if (b.canBeSender() && rule.senderMatch(b))
          considerConnection(b, a, rule, ccs);
      }
    }

    void connectEachActiveDest(
        const Address& a, const ConnectionRule& rule, CandidateConnections& ccs)
    {
      for (auto& p : activePorts) {
        auto& b = p.second;
        if (b.canBeDest() && rule.destMatch(b))
          considerConnection(a, b, rule, ccs);
      }
    }

    void connectByRule(const Address& a, ConnectionRules& rules, CandidateConnections& ccs) {
      for (auto& rule : rules) {
        if (a.canBeSender() && rule.senderMatch(a))   connectEachActiveDest(a, rule, ccs);
        if (a.canBeDest()   && rule.destMatch(a))     connectEachActiveSender(a, rule, ccs);
      }
    }

    void addPort(const snd_seq_addr_t& addr) {
      Address a = seq.address(addr);
      if (!a) return;
      activePorts[addr] = a;

      std::cout << "port added " << a << std::endl;

      CandidateConnections candidates;
      connectByRule(a, observedRules, candidates);
      for (auto& cc : candidates)
        makeConnection(cc, Reason::observed);

      candidates.clear();
      connectByRule(a, profileRules, candidates);
      for (auto& cc : candidates) {
        bool alreadyConnected = false;

        // TODO: skip this test if rule isn't wildcard at all
        for (const auto& ac : activeConnections) {
          if (cc.sender.addr.client != ac.first.sender.client) continue;
          if (cc.dest.addr.client != ac.first.dest.client) continue;

          // The clients match. See if the proposed rule could have made
          // the connection:
          const auto& si = activePorts.find(ac.first.sender);
          if (si == activePorts.end()) continue;
          const auto& di = activePorts.find(ac.first.dest);
          if (di == activePorts.end()) continue;

          if (cc.rule.match(si->second, di->second)) {
            alreadyConnected = true;
            break;
          }
        }

        if (!alreadyConnected)
          makeConnection(cc, Reason::byRule);
      }
    }

    void delPort(const snd_seq_addr_t& addr) {
      Address port = knownPort(addr);
      if (port)
        std::cout << "port removed " << port << std::endl;

      std::vector<snd_seq_connect_t> doomed;
      for (auto& c : activeConnections) {
        if (c.first.sender == addr || c.first.dest == addr) {
          doomed.push_back(c.first);

          Address sender = knownPort(c.first.sender);
          Address dest = knownPort(c.first.dest);
          if (sender && dest)
            std::cout << "disconnected " << sender << "-->" << dest << std::endl;
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

      Address sender = seq.address(conn.sender);
      Address dest = seq.address(conn.dest);
      if (!sender || !dest) // if either is an ignored port, ignore this
        return;

      activeConnections[conn] = Reason::observed;
      ConnectionRule c = ConnectionRule::exact(sender, dest);
      observedRules.push_back(c);
      std::cout << "observed connection " << c << std::endl;
    }

    void delConnection(const snd_seq_connect_t& conn) {
      auto i = activeConnections.find(conn);
      if (i == activeConnections.end())
        // don't know anything about this connection
        return;

      Address sender = seq.address(conn.sender);
      Address dest = seq.address(conn.dest);
      std::cout << "observed disconnection "
        << sender << " --> " << dest << std::endl;

      if (i->second == Reason::observed) {
        observedRules.erase(
        std::remove_if(observedRules.begin(), observedRules.end(),
            [&](auto r){ return r.match(sender, dest); }),
          observedRules.end()
        );
      }

      activeConnections.erase(i);
    }

    void readRules() {
      ::readRules(Files::profileFilePath(), profileText, profileRules);
      ::readRules(Files::observedFilePath(), observedText, observedRules);
    }
};


void sendResetCommand() {
  IPC::Client client;
  client.sendCommand("reset");
}

void MidiMinder::handleResetCommand(IPC::Connection& conn) {
  std::cerr << "Reset command recieved, but not yet handled" << std::endl;
}

void sendLoadCommand() {
  IPC::Client client;
  client.sendCommand("load");
  // TODO: Send file
}

void MidiMinder::handleLoadCommand(IPC::Connection& conn) {
  std::cerr << "Load command recieved, but not yet handled" << std::endl;
}

void sendSaveCommand() {
  IPC::Client client;
  client.sendCommand("save");
  // TODO: receive file
}

void MidiMinder::handleSaveCommand(IPC::Connection& conn) {
  std::cerr << "Save command recieved, but not yet handled" << std::endl;
}

void sendCommTestCommand() {
  IPC::Client client;
  std::cout << "Sending ahoy..." << std::endl;
  client.sendCommand("ahoy");
  std::cout << "Waiting for file:" << std::endl;
  std::cout << "--------" << std::endl;
  client.receiveFile(std::cout);
  std::cout << "--------" << std::endl;
}

void MidiMinder::handleCommTestCommand(IPC::Connection& conn) {
  std::cerr << "Connection test command recieved" << std::endl;
  std::string text =
    "A sailor went to sea, sea, sea,\n"
    "To see what he could see, see, see.\n"
    "But all that he could see, see, see,\n"
    "Was the bottom of the deep blue sea, sea, sea.\n";
  std::istringstream file(text);
  conn.sendFile(file);
}


int main(int argc, char *argv[]) {
  if (!Args::parse(argc, argv))
    return Args::exitCode;


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
      MidiMinder mm;
      mm.run();
      break;
    }

    case Args::Command::CommTest:
      sendCommTestCommand();
      break;
  }

  return 0;
}
