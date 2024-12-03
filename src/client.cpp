#include "client.h"

#include <functional>
#include <map>
#include <set>

#include "msg.h"
#include "seq.h"

namespace {
  struct lexicalAddressLess {
    bool operator()(const Address& a, const Address& b) const {
      if (!a.valid || !b.valid) return b.valid;
        // in this case, a < b iff a is invalid, and b is valid.

      if (a.client != b.client) return a.client < b.client;

      return a.port < b.port;
    }
  };

  struct Connection {
    Address sender;
    Address dest;
  };

  struct lexicalConnectionLess {
    bool operator()(const Connection& a, const Connection& b) const {
      lexicalAddressLess cmp;
      if (cmp(a.sender, b.sender)) return true;
      if (cmp(b.sender, a.sender)) return false;
      return cmp(a.dest, b.dest);
    }
  };

}
namespace Client {

  void listCommand() {
    Seq seq;
    seq.begin();

    std::map<snd_seq_addr_t, Address> addrMap;
    std::set<Address, lexicalAddressLess> ports;
    std::set<Connection, lexicalConnectionLess> connections;

    seq.scanPorts([&](const snd_seq_addr_t& a) {
      auto address = seq.address(a);
      if (address) {
        addrMap[a] = address;
        ports.insert(address);
      }
    });
    seq.scanConnections([&](const snd_seq_connect_t& c) {
      auto si = addrMap.find(c.sender);
      auto di = addrMap.find(c.dest);
      if (si != addrMap.end() && di != addrMap.end()) {
        Connection conn = {si->second, di->second};
        connections.insert(conn);
      }
    });

    Msg::output("Ports:");
    for (const auto& p : ports)
      Msg::output("    {}", p);

    Msg::output("Connections:");
    for (const auto& c : connections) {
          Msg::output("    {} --> {}", c.sender, c.dest);
    }
  }


}