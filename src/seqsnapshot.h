#pragma once

#include <map>
#include <set>

#include "seq.h"

struct SeqSnapshot {
  Seq seq;

  struct Connection {
    Address sender;
    Address dest;
  };

  std::map<snd_seq_addr_t, Address> addrMap;
  std::vector<Address> ports;
  std::vector<Connection> connections;

  std::string::size_type clientWidth;
  std::string::size_type portWidth;

  SeqSnapshot()  { seq.begin(); refresh(); }
  ~SeqSnapshot() { seq.end(); }

  void refresh();

  static const char* dirStr(bool sender, bool dest);
  static const char* addressDirStr(const Address&);
};
