#pragma once

#include <map>
#include <set>

#include "seq.h"

struct SeqSnapshot {
  Seq seq;

  struct Client {
    client_id_t id;
    std::string name;
    std::string details;
  };

  struct Connection {
    Address sender;
    Address dest;
  };

  bool includeAllItems = false;
  bool numericSort = false;
  bool useLongPortNames = false;

  std::map<snd_seq_addr_t, Address> addrMap;
  std::vector<Client> clients;
  std::vector<Address> ports;
  std::vector<Connection> connections;

  std::string::size_type clientWidth = 0;
  std::string::size_type portWidth = 0;

  SeqSnapshot();
  ~SeqSnapshot();

  void refresh();
  bool checkIfNeedsRefresh();

  bool addressStillValid(const Address& a) const;
  bool hasConnectionBetween(const Address& sender, const Address& dest) const;

  static const char* dirStr(bool sender, bool dest);
  static const char* addressDirStr(const Address&);
};
