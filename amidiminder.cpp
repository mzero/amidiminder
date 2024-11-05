#include <algorithm>
#include <alsa/asoundlib.h>
#include <fstream>
#include <iostream>
#include <list>
#include <map>
#include <set>

#include "args.h"
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
}



class MidiMinder {
  private:
    Seq seq;

  public:
    MidiMinder() {
      seq.begin();
    }

    ~MidiMinder() {
      seq.end();
    }

    void run() {
      seq.scanPorts([&](auto p){
        if (Args::outputPortDetails)
          seq.outputAddrDetails(std::cout, p);
        this->addPort(p);
      });
      seq.scanConnections([&](auto c){ this->addConnection(c); });

      while (seq) {
        snd_seq_event_t *ev = seq.eventInput();
        if (!ev) {
          sleep(1);
          continue;
        }

        switch (ev->type) {
          case SND_SEQ_EVENT_CLIENT_START:
          case SND_SEQ_EVENT_CLIENT_EXIT:
          case SND_SEQ_EVENT_CLIENT_CHANGE:
            break;

          case SND_SEQ_EVENT_PORT_START: {
            if (Args::outputPortDetails) {
              seq.outputAddrDetails(std::cout, ev->data.addr);
            }
            addPort(ev->data.addr);
            break;
          }

          case SND_SEQ_EVENT_PORT_EXIT: {
            delPort(ev->data.addr);
            break;
          }

          case SND_SEQ_EVENT_PORT_CHANGE: {
            std::cout << "port change " << ev->data.addr << std::endl;
            // FIXME: treat this as a add/remove?
            break;
          }

          case SND_SEQ_EVENT_PORT_SUBSCRIBED: {
            addConnection(ev->data.connect);
            break;
          }

          case SND_SEQ_EVENT_PORT_UNSUBSCRIBED: {
            delConnection(ev->data.connect);
            break;
          }

          default: {
            std::cout << "unknown event ("
               << std::dec << static_cast<int>(ev->type) << ")" << std::endl;
          }
        }
      }
    }

    ConnectionRules configRules;
    ConnectionRules observedRules;
    std::map<snd_seq_addr_t, Address> activePorts;
    std::map<snd_seq_connect_t, Reason> activeConnections;

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
      connectByRule(a, configRules, candidates);
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

    bool readRules() {
      if (Args::rulesFilePath.empty()) {
        std::cout << "no rules file specified, so no rules added" << std::endl;
        return true;
      }
      std::ifstream rulesFile(Args::rulesFilePath);
      if (!rulesFile) {
        std::cout << "could not read " << Args::rulesFilePath << std::endl;
        return false;
      }
      std::cout << "reading rules from " << Args::rulesFilePath << std::endl;
      bool rulesReadOkay = parseRulesFile(rulesFile, configRules);

      for (auto& r : configRules)
        std::cout << "adding rule " << r << std::endl;

      return rulesReadOkay;
    }
};


int main(int argc, char *argv[]) {
  if (!Args::parse(argc, argv))
    return Args::exitCode;

  MidiMinder mm;

  bool rulesReadOkay = mm.readRules();
  if (Args::rulesCheckOnly)
    return rulesReadOkay ? 0 : 1;

  mm.run();
  return 0;
}
