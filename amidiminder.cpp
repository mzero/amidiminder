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
          case SND_SEQ_EVENT_CLIENT_START: {
            // std::cout << "client start " << ev->data.addr << std::endl;
            break;
          }

          case SND_SEQ_EVENT_CLIENT_EXIT: {
            // std::cout << "client exit " << ev->data.addr << std::endl;
            break;
          }

          case SND_SEQ_EVENT_CLIENT_CHANGE: {
            // std::cout << "client change " << ev->data.addr << std::endl;
            break;
          }

          case SND_SEQ_EVENT_PORT_START: {
            if (Args::outputPortDetails) {
              std::cout << "port start " << ev->data.addr << std::endl;
              seq.outputAddrDetails(std::cout, ev->data.addr);
            }
            addPort(ev->data.addr);
            break;
          }

          case SND_SEQ_EVENT_PORT_EXIT: {
            if (Args::outputPortDetails) {
              std::cout << "port exit " << ev->data.addr << std::endl;
            }
            delPort(ev->data.addr);
            break;
          }

          case SND_SEQ_EVENT_PORT_CHANGE: {
            if (Args::outputPortDetails) {
              std::cout << "port change " << ev->data.addr << std::endl;
              seq.outputAddrDetails(std::cout, ev->data.addr);
            }
            // FIXME: treat this as a add/remove?
            break;
          }

          case SND_SEQ_EVENT_PORT_SUBSCRIBED: {
            // std::cout << "port subscribed " << ev->data.connect << std::endl;
            addConnection(ev->data.connect);
            break;
          }

          case SND_SEQ_EVENT_PORT_UNSUBSCRIBED: {
            // std::cout << "port unsubscribed " << ev->data.connect << std::endl;
            delConnection(ev->data.connect);
            break;
          }

          default: {
            std::cout << "some other event ("
               << std::dec << static_cast<int>(ev->type) << ")" << std::endl;
          }
        }
      }
    }

    ConnectionRules configRules;
    ConnectionRules observedRules;
    std::map<snd_seq_addr_t, Address> activePorts;
    std::map<snd_seq_connect_t, Reason> activeConnections;

    void makeConnection(
      const Address& a, const Address& b,
      const ConnectionRule& rule, Reason r)
    {
      snd_seq_connect_t conn;
      conn.sender = a.addr;
      conn.dest = b.addr;
      if (activeConnections.find(conn) == activeConnections.end()) {
        seq.connect(conn.sender, conn.dest);
        activeConnections[conn] = r;
        switch (r) {
          case Reason::byRule:
            std::cout << "making connection by rule: " << rule << std::endl;
            break;

          case Reason::observed:
            std::cout << "reactiving observed connection: " << rule << std::endl;
            break;
        }
      }
    }

    void connectEachActiveSender(
        const Address& a, const ConnectionRule& rule, Reason r)
    {
      for (auto& p : activePorts) {
        auto& b = p.second;
        if (b.canBeSender() && rule.senderMatch(b))
          makeConnection(b, a, rule, r);
      }
    }

    void connectEachActiveDest(
        const Address& a, const ConnectionRule& rule, Reason r)
    {
      for (auto& p : activePorts) {
        auto& b = p.second;
        if (b.canBeDest() && rule.destMatch(b))
          makeConnection(a, b, rule, r);
      }
    }

    void connectByRule(const Address& a, ConnectionRules& rules, Reason r) {
      for (auto& rule : rules) {
        if (a.canBeSender() && rule.senderMatch(a))   connectEachActiveDest(a, rule, r);
        if (a.canBeDest()   && rule.destMatch(a))     connectEachActiveSender(a, rule, r);
      }
    }

    void addPort(const snd_seq_addr_t& addr) {
      Address a = seq.address(addr);
      if (!a) return;
      activePorts[addr] = a;

      connectByRule(a, observedRules, Reason::observed);
      connectByRule(a, configRules, Reason::byRule);
    }

    void delPort(const snd_seq_addr_t& addr) {
      activePorts.erase(addr);

      std::vector<snd_seq_connect_t> doomed;

      for (auto& c : activeConnections) {
        if (c.first.sender == addr || c.first.dest == addr) {
          doomed.push_back(c.first);
          std::cout << c.second << " connection deactivated: " << c.first << std::endl;
        }
      }

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
      std::cout << "adding observed connection " << c << std::endl;
    }

    void delConnection(const snd_seq_connect_t& conn) {
      auto i = activeConnections.find(conn);
      if (i == activeConnections.end())
        // don't know anything about this connection
        return;

      Address sender = seq.address(conn.sender);
      Address dest = seq.address(conn.dest);

      switch (i->second) {
        case Reason::byRule:
          std::cout << "rule connection explicitly disconnected " << conn << std::endl;
          break;

        case Reason::observed:
          std::remove_if(observedRules.begin(), observedRules.end(),
            [&](auto r){ return r.match(sender, dest); });
          std::cout << "removing observed connection " << conn << std::endl;
          break;
      }

      activeConnections.erase(i);
    }

    bool readRules() {
      std::ifstream rulesFile(Args::rulesFilePath);
      if (!rulesFile) {
        std::cout << "could not read " << Args::rulesFilePath << std::endl;
        return false;
      }
      std::cout << "reading rules from " << Args::rulesFilePath << std::endl;
      bool rulesReadOkay = parseRulesFile(rulesFile, configRules);

      for (auto& r : configRules)
      std::cout << "adding connection rule " << r << std::endl;

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
