#include "seqsnapshot.h"

namespace {
  using Connection = SeqSnapshot::Connection;

  struct lexicalAddressLess {
    bool operator()(const Address& a, const Address& b) const {
      if (!a.valid || !b.valid) return b.valid;
        // in this case, a < b iff a is invalid, and b is valid.

      if (a.client != b.client) return a.client < b.client;

      return a.port < b.port;
    }
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

void SeqSnapshot::refresh() {
  ports.clear();
  connections.clear();

  seq.scanPorts([&](const snd_seq_addr_t& a) {
    auto address = seq.address(a);
    if (address) {
      addrMap[a] = address;
      ports.push_back(address);
    }
  });
  std::sort(ports.begin(), ports.end(), lexicalAddressLess());

  seq.scanConnections([&](const snd_seq_connect_t& c) {
    auto si = addrMap.find(c.sender);
    auto di = addrMap.find(c.dest);
    if (si != addrMap.end() && di != addrMap.end()) {
      Connection conn = {si->second, di->second};
      connections.push_back(conn);
    }
  });
  std::sort(connections.begin(), connections.end(), lexicalConnectionLess());

  numPorts = ports.size();
  numConnections = connections.size();

  clientWidth = 0;
  portWidth = 0;
  for (const auto& p : ports) {
    clientWidth = std::max(clientWidth, p.client.size());
    portWidth = std::max(portWidth, p.port.size());
  }
}


const char* SeqSnapshot::dirStr(bool sender, bool dest) {
  if (sender) {
    if (dest)   return "<->";
    else        return "-->";
  }
  else {
    if (dest)   return "<--";
    else        return "   ";
  }
}
const char* SeqSnapshot::addressDirStr(const Address& a) {
  return dirStr(a.canBeSender(), a.canBeDest());
}

