#pragma once

// Manage being an ALSA Sequencer client

#include <alsa/asoundlib.h>
#include <fmt/format.h>
#include <functional>
#include <iostream>
#include <string>


class Address {
  public:
    Address()
      : valid(false), addr{0, 0}
      { }
    Address(const snd_seq_addr_t& a, unsigned int f, unsigned int t,
        const std::string& c, const std::string& p)
      : valid(true), addr(a), caps(f), types(t), client(c), port(p),
        primarySender(false), primaryDest(false)
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

    fmt::format_context::iterator format(fmt::format_context&) const;

    bool valid;
    snd_seq_addr_t addr;
    unsigned int caps;
    unsigned int types;
    std::string client;
    std::string port;
    bool primarySender;
    bool primaryDest;
};


class Seq {
  public:
    Seq() { }

    void begin();
    void end();
    operator bool() const { return seq; }

    std::string clientName(const snd_seq_addr_t&);
    Address address(const snd_seq_addr_t&);

    void scanFDs(std::function<void(int)>);
    snd_seq_event_t * eventInput();
      // if nullptr is returned, sleep and call again...

    void scanPorts(std::function<void(const snd_seq_addr_t&)>);
    void scanConnections(std::function<void(const snd_seq_connect_t&)>);
    void connect(const snd_seq_addr_t& sender, const snd_seq_addr_t& dest);
    void disconnect(const snd_seq_connect_t& conn);

    bool errCheck(int serr, const char* op);
    bool errFatal(int serr, const char* op);

  private:
    snd_seq_t *seq = nullptr;
    int evtPort;

  public:
    static void outputAddr(std::ostream&, const snd_seq_addr_t&);
    static void outputConnect(std::ostream&, const snd_seq_connect_t&);
    static void outputEvent(std::ostream& out, const snd_seq_event_t& ev);

    void outputAddrDetails(std::ostream&, const snd_seq_addr_t&);
};



template <> struct fmt::formatter<Address> : formatter<string_view> {
  auto format(const Address& a, format_context& ctx) const
    { return a.format(ctx); }
};

template <> struct fmt::formatter<snd_seq_addr_t> : formatter<string_view> {
  format_context::iterator
  format(const snd_seq_addr_t &, format_context &) const;
};

template <> struct fmt::formatter<snd_seq_connect_t> : formatter<string_view> {
  format_context::iterator
  format(const snd_seq_connect_t &, format_context &) const;
};

template <> struct fmt::formatter<snd_seq_event_t> : formatter<string_view> {
  format_context::iterator
  format(const snd_seq_event_t &, format_context &) const;
};


inline std::ostream& operator<<(std::ostream& s, const Address& a)
  { s << fmt::format("{}", a); return s; }

inline std::ostream& operator<<(std::ostream& s, const snd_seq_addr_t& a)
  { s << fmt::format("{}", a); return s; }

inline std::ostream& operator<<(std::ostream& s, const snd_seq_connect_t& c)
  { s << fmt::format("{}", c); return s; }

inline std::ostream& operator<<(std::ostream& s, const snd_seq_event_t& ev)
  { s << fmt::format("{}", ev); return s; }


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
