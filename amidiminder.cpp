#include <algorithm>
#include <alsa/asoundlib.h>
#include <iostream>
#include <list>

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

  struct AddressSpec {
    std::string client;
    std::string port;

    AddressSpec(Address a) : client(a.client), port(a.port) { }

    bool matches(const Address& a)
      { return client == a.client && port == a.port; }
  };

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
  };

  struct Connection {
    AddressSpec senderSpec;
    AddressSpec destSpec;

    OptionalAddress senderAddr;
    OptionalAddress destAddr;

    Connection(const Address& s, const Address& d)
      : senderSpec(s), destSpec(d),
        senderAddr(s), destAddr(d)
      { }

    bool matches(const snd_seq_connect_t& conn) const {
      return senderAddr.matches(conn.sender)
        && destAddr.matches(conn.dest);
    }
  };

  inline std::ostream& operator<<(std::ostream& s, const Connection& c) {
    s << c.senderAddr << " ==>> " << c.destAddr;
    return s;
  }

}



class MidiMinder {
  private:
    Seq seq;

  public:
    MidiMinder() {
      seq.begin();
      seq.scanConnections([&](auto c){ this->addConnection(c); });
    }

    ~MidiMinder() {
      seq.end();
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

        if (c.senderSpec.matches(a)) {
          c.senderAddr.set(a.addr);
          activated = true;
        }

        if (c.destSpec.matches(a)) {
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

};



int main() {
  MidiMinder mm;
  mm.run();
  return 0;
}
