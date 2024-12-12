#include "seqsnapshot.h"
#include <algorithm>

namespace {
  using Client = SeqSnapshot::Client;
  using Connection = SeqSnapshot::Connection;

  bool lexicalClientLess(const Client& a, const Client& b) {
    if (a.name != b.name) return a.name < b.name;
    else                  return a.id < b.id;
  }

  bool lexicalAddressLess(const Address& a, const Address& b) {
    if (!a.valid || !b.valid) return b.valid;
      // in this case, a < b iff a is invalid, and b is valid.

    if (a.client != b.client) return a.client < b.client;
    return a.addr.port < b.addr.port;  // always sort the port numerically!
  }

  bool lexicalConnectionLess(const Connection& a, const Connection& b) {
    if (lexicalAddressLess(a.sender, b.sender)) return true;
    if (lexicalAddressLess(b.sender, a.sender)) return false;
    return lexicalAddressLess(a.dest, b.dest);
  }


  bool numericClientLess(const Client& a, const Client& b)
    { return a.id < b.id; }

  bool numericAddressLess(const Address& a, const Address& b) {
    if (a.addr.client == b.addr.client) return a.addr.port < b.addr.port;
    else                                return a.addr.client < b.addr.client;
  }

  bool numericConnectionLess(const Connection& a, const Connection& b) {
    if (numericAddressLess(a.sender, b.sender)) return true;
    if (numericAddressLess(b.sender, a.sender)) return false;
    return numericAddressLess(a.dest, b.dest);
  }
}

SeqSnapshot::SeqSnapshot()  { seq.begin("midiwala"); }
SeqSnapshot::~SeqSnapshot() { seq.end(); }

void SeqSnapshot::refresh() {
  clients.clear();
  ports.clear();
  connections.clear();

  seq.scanClients([&](client_id_t c) {
    if (includeAllItems || seq.isMindableClient(c)) {
      Client client = { c, seq.clientName(c), seq.clientDetails(c) };
      clients.push_back(client);
    }
  });

  std::sort(clients.begin(), clients.end(),
    numericSort ? numericClientLess : lexicalClientLess);

  seq.scanPorts([&](const snd_seq_addr_t& a) {
    auto address = seq.address(a);
    if (includeAllItems || address.mindable) {
      // Compute primary port status. See comment in service.cpp for
      // details and caveats about this computation.

      bool foundPrimarySender = false;
      bool foundPrimaryDest = false;
      for (auto& p : ports) {
        if (p.addr.client == a.client) {
          foundPrimarySender = foundPrimarySender || p.primarySender;
          foundPrimaryDest   = foundPrimaryDest   || p.primaryDest;
        }
        if (foundPrimarySender && foundPrimaryDest)
          break;
      }
      address.primarySender = !foundPrimarySender && address.canBeSender();
      address.primaryDest   = !foundPrimaryDest   && address.canBeDest();

      if (useLongPortNames)
        address.port = address.portLong;

      addrMap[a] = address;
      ports.push_back(address);
    }
  });
  std::sort(ports.begin(), ports.end(),
    numericSort ? numericAddressLess : lexicalAddressLess);

  seq.scanConnections([&](const snd_seq_connect_t& c) {
    auto si = addrMap.find(c.sender);
    auto di = addrMap.find(c.dest);
    if (si != addrMap.end() && di != addrMap.end()) {
      Connection conn = {si->second, di->second};
      connections.push_back(conn);
    }
  });
  std::sort(connections.begin(), connections.end(),
    numericSort ? numericConnectionLess : lexicalConnectionLess);

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

bool SeqSnapshot::addressStillValid(const Address& priorA) const {
  auto i = addrMap.find(priorA.addr);
  if (i == addrMap.end()) return false;

  auto& currA = i->second;
  if (priorA.client != currA.client)  return false;
  if (priorA.port != currA.port)      return false;
  if (priorA.caps != currA.caps)      return false;
  if (priorA.types != currA.types)    return false;
  return true;
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

