#include <algorithm>
#include <alsa/asoundlib.h>
#include <fstream>
#include <iostream>
#include <list>

#include "rule.h"
#include "seq.h"


/** TODO

  [] remember subscription options

  [] verbosity controls
  [] connection list on ^t
  [] persistent file of connections, read on startup
  [] auto connection file
    [] allow and disallow like amidiauto?
    [] patterns? regex?
    [] support for matching port types?
    [] support for subscription options (like aconnect?)

 **/

namespace {

  struct OptionalAddress {
    bool valid;
    snd_seq_addr_t addr;

    OptionalAddress () : valid(false) { }
    OptionalAddress(Address a) : valid(a.valid), addr(a.addr) { }

    void set(const snd_seq_addr& a) { valid = true; addr = a; }
    void clear()                    { valid = false; }

    operator bool() const { return valid; }

    bool matches(const snd_seq_addr_t& a) const
      { return valid && addr == a; }

    void output(std::ostream& s) const {
      if (valid)  s << addr;
      else        s << "--:--";
    }
  };

  inline std::ostream& operator<<(std::ostream& s, const OptionalAddress& a)
    { a.output(s); return s; }


  struct Connection {
    ConnectionRule rule;
    bool observed;

    OptionalAddress senderAddr;
    OptionalAddress destAddr;

    Connection(const Address& s, const Address& d)
      : rule(ConnectionRule::exact(s, d)),
        observed(true),
        senderAddr(s), destAddr(d)
      { }

    Connection(const ConnectionRule& r)
      : rule(r), observed(false)
      { }

    bool matches(const snd_seq_connect_t& conn) const {
      return observed
        && senderAddr.matches(conn.sender)
        && destAddr.matches(conn.dest);
    }

    void output(std::ostream& s) const {
      s << rule;
      if (senderAddr || destAddr)
        s << " (" << senderAddr << " --> " << destAddr << ")";
    }
  };

  inline std::ostream& operator<<(std::ostream& s, const Connection& c)
    { c.output(s); return s; }

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

    void setup() {
      readRules();
      seq.scanPorts([&](auto p){ this->addPort(p); });
      seq.scanConnections([&](auto c){ this->addConnection(c); });
    }

    void run() {
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
            // std::cout << "port start " << ev->data.addr << std::endl;
            addPort(ev->data.addr);
            break;
          }

          case SND_SEQ_EVENT_PORT_EXIT: {
            // std::cout << "port exit " << ev->data.addr << std::endl;
            delPort(ev->data.addr);
            break;
          }

          case SND_SEQ_EVENT_PORT_CHANGE: {
            // std::cout << "port change " << ev->data.addr << std::endl;
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

    typedef std::list<Connection> Connections;
    Connections connections;

    void addPort(const snd_seq_addr_t& addr) {
      Address a = seq.address(addr);
      if (!a) return;

      for (auto&& c : connections) {
        bool activated = false;

        if (c.rule.senderMatch(a)) {
          c.senderAddr.set(a.addr);
          activated = true;
        }

        if (c.rule.destMatch(a)) {
          c.destAddr.set(a.addr);
          activated = true;
        }

        if (activated && c.senderAddr && c.destAddr) {
          std::cout << "connection re-activated: " << c << std::endl;
          seq.connect(c.senderAddr.addr, c.destAddr.addr);
        }
      }
    }

    void delPort(const snd_seq_addr_t& addr) {
      for (auto&& c : connections) {
        bool deactivated = false;

        if (c.senderAddr.matches(addr)) {
          c.senderAddr.clear();
          deactivated = true;
        }

        if (c.destAddr.matches(addr)) {
          c.destAddr.clear();
          deactivated = true;
        }

        if (deactivated)
          std::cout << "connection deactivated: " << c << std::endl;
      }
    }


    void addConnection(const snd_seq_connect_t& conn) {
      auto i =
        find_if(connections.begin(), connections.end(),
          [&](auto c){ return c.matches(conn); });

      if (i != connections.end())
          return; // already have it

      Address sender = seq.address(conn.sender);
      Address dest = seq.address(conn.dest);

      if (sender && dest) {
        Connection c(sender, dest);
        connections.push_back(c);
        std::cout << "adding connection " << c << std::endl;
      }
    }

    void delConnection(const snd_seq_connect_t& conn) {
      for (const auto& c : connections)
        if (c.matches(conn))
          std::cout << "removing connection " << c << std::endl;

      connections.remove_if(
        [&](auto c){ return c.matches(conn); });
    }

    void readRules() {
      const auto rulesPath = "midi-rules";

      std::ifstream rulesFile(rulesPath);
      if (!rulesFile) {
        std::cout << "rules file " << rulesPath << " not found" << std::endl;
        return;
      }

      ConnectionRules rules;
      if (!parseRulesFile(rulesFile, rules)) {
        std::cout <<  "rules file " << rulesPath
          << " had errors; proceeding anyway" << std::endl;
      }

      for (auto&& r : rules) {
        connections.push_back(Connection(r));
        std::cout << "adding connection rule " << r << std::endl;
      }
    }
};



int main() {
  MidiMinder mm;
  mm.setup();
  mm.run();
  return 0;
}
