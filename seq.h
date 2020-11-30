#pragma once

// Manage being an ALSA Sequencer client

#include <alsa/asoundlib.h>
#include <functional>
#include <iostream>
#include <string>


class Address {
  public:
    Address()
      : valid(false), addr{0, 0}
      { }
    Address(const snd_seq_addr_t& a, unsigned int f,
        const std::string& c, const std::string& p)
      : valid(true), addr(a), caps(f), client(c), port(p)
      { }

    // allow copying
    Address(const Address&) = default;
    Address& operator=(const Address&) = default;

    static const Address null;

    operator bool() const { return valid; }

    bool canBeSender() const { return valid && caps & SND_SEQ_PORT_CAP_SUBS_READ; }
    bool canBeDest() const   { return valid && caps & SND_SEQ_PORT_CAP_SUBS_WRITE; }

    bool matches(const snd_seq_addr_t& a) const
      { return valid && addr.client == a.client && addr.port == a.port; }

    void output(std::ostream&) const;

    bool valid;
    snd_seq_addr_t addr;
    unsigned int caps;
    std::string client;
    std::string port;
};


class Seq {
  public:
    Seq() { }

    void begin();
    void end();
    operator bool() const { return seq; }

    Address address(const snd_seq_addr_t&);

    snd_seq_event_t * eventInput();
      // if nullptr is returned, sleep and call again...

    void scanPorts(std::function<void(const snd_seq_addr_t&)>);
    void scanConnections(std::function<void(const snd_seq_connect_t&)>);
    void connect(const snd_seq_addr_t& sender, const snd_seq_addr_t& dest);

    bool errCheck(int serr, const char* op);
    bool errFatal(int serr, const char* op);

  private:
    snd_seq_t *seq = nullptr;
    int evtPort;

  public:
    static void outputAddr(std::ostream&, const snd_seq_addr_t&);
    static void outputConnect(std::ostream&, const snd_seq_connect_t&);

    void outputAddrDetails(std::ostream&, const snd_seq_addr_t&);
};


inline std::ostream& operator<<(std::ostream& s, const Address& a)
  { a.output(s); return s; }

inline std::ostream& operator<<(std::ostream& s, const snd_seq_addr_t& a)
  { Seq::outputAddr(s, a); return s; }

inline std::ostream& operator<<(std::ostream& s, const snd_seq_connect_t& c)
  { Seq::outputConnect(s, c); return s; }


inline bool operator==(const snd_seq_addr_t& a, const snd_seq_addr_t& b) {
  return a.client == b.client && a.port == b.port;
}

inline bool operator<(const snd_seq_addr_t& a, const snd_seq_addr_t& b) {
  return a.client == b.client ? a.port < b.port : a.client < b.client;
}

inline bool operator==(const snd_seq_connect_t& a, const snd_seq_connect_t& b) {
  return a.sender == b.sender && a.dest == b.dest;
}

inline bool operator<(const snd_seq_connect_t& a, const snd_seq_connect_t& b) {
  return a.sender == b.sender ? a.dest < b.dest : a.sender < b.sender;
}
