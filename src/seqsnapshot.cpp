#include "seqsnapshot.h"
#include <algorithm>

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
  clients.clear();
  ports.clear();
  connections.clear();

  seq.scanClients([&](client_id_t c) {
    if (!seq.isMindableClient(c)) return;
    Client client = { c, seq.clientName(c), seq.clientDetails(c) };
    clients.push_back(client);
  });

  seq.scanPorts([&](const snd_seq_addr_t& a) {
    auto address = seq.address(a);
    if (address.mindable) {
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

  clientWidth = 0;
  portWidth = 0;
  for (const auto& p : ports) {
    clientWidth = std::max(clientWidth, p.client.size());
    portWidth = std::max(portWidth, p.port.size());
  }
}

bool SeqSnapshot::checkIfNeedsRefresh() {
  bool needsRefresh = false;

  while (snd_seq_event_t* ev = seq.eventInput())
    switch (ev->type) {
      case SND_SEQ_EVENT_CLIENT_START:
      case SND_SEQ_EVENT_CLIENT_EXIT:
      case SND_SEQ_EVENT_CLIENT_CHANGE:
      case SND_SEQ_EVENT_PORT_START:
      case SND_SEQ_EVENT_PORT_EXIT:
      case SND_SEQ_EVENT_PORT_CHANGE:
      case SND_SEQ_EVENT_PORT_SUBSCRIBED:
      case SND_SEQ_EVENT_PORT_UNSUBSCRIBED:
        needsRefresh = true;
        break;
      default:
        break;
    }

  return needsRefresh;
}


bool SeqSnapshot::hasConnectionBetween(
  const Address& sender, const Address& dest) const
{
  return std::any_of(connections.begin(), connections.end(),
    [&](const auto& c){
      return c.sender.addr == sender.addr && c.dest.addr == dest.addr;
     });
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

