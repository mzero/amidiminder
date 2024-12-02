#include "client.h"

#include <map>
#include <set>

#include "msg.h"
#include "seq.h"

namespace Client {

  void listCommand() {
    Seq seq;
    seq.begin();

    std::map<snd_seq_addr_t, Address> ports;
    std::set<snd_seq_connect_t> connections;

    seq.scanPorts([&](const snd_seq_addr_t& a) {
      auto address = seq.address(a);
      if (address) ports[a] = address;
    });
    seq.scanConnections([&](const snd_seq_connect_t& c) {
      if (ports.count(c.sender) && ports.count(c.dest))
        connections.insert(c);
    });

    Msg::output("Ports:");
    for (const auto& p : ports)
      Msg::output("    {}", p.second);

    Msg::output("Connections:");
    for (const auto& c : connections) {
      auto si = ports.find(c.sender);
      auto di = ports.find(c.dest);
      if (si != ports.end() && di != ports.end())
          Msg::output("    {} --> {}", si->second, di->second);
    }
  }


}