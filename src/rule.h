#pragma once

#include <fmt/format.h>
#include <iostream>
#include <string>
#include <vector>

#include "seq.h"


class ClientSpec {
  public:
    static ClientSpec exact(const std::string&);
    static ClientSpec partial(const std::string&);
    static ClientSpec wildcard();

    bool match (const Address&) const;

    ClientSpec(const ClientSpec&) = default;
    ClientSpec& operator=(const ClientSpec&) = default;

    bool isWildcard() const;
    fmt::format_context::iterator format(fmt::format_context&) const;

  private:
    std::string client;
    bool exactMatch;

    ClientSpec(const std::string&, bool);
};

class PortSpec {
  public:
    static PortSpec defaulted();
    static PortSpec partial(const std::string&);
    static PortSpec exact(const std::string&);
    static PortSpec numeric(int);
    static PortSpec type(unsigned int);
    static PortSpec wildcard();

    bool matchAsSender(const Address&) const;
    bool matchAsDest(const Address&) const;

    PortSpec(const PortSpec&) = default;
    PortSpec& operator=(const PortSpec&) = default;

    bool isDefaulted() const;
    bool isType() const;

    fmt::format_context::iterator format(fmt::format_context&) const;

  private:
    enum Kind {
      Defaulted,
      Partial,
      Exact,
      Numeric,
      Type,
      Wildcard
    };

    Kind kind;
    std::string port;
    bool exactMatch;
    int portNum;
    unsigned int typeFlag;

    PortSpec(Kind, const std::string&, bool, int, unsigned int);

    bool match(const Address&, bool primaryFlag) const;
};


class AddressSpec {
  public:
    AddressSpec(const ClientSpec&, const PortSpec&);

    static AddressSpec exact(const Address&);

    bool matchAsSender(const Address&) const;
    bool matchAsDest(const Address&) const;

    AddressSpec(const AddressSpec&) = default;
    AddressSpec& operator=(const AddressSpec&) = default;

    fmt::format_context::iterator format(fmt::format_context&) const;

    static AddressSpec parse(const std::string&);

  private:
    ClientSpec client;
    PortSpec port;
};


class ConnectionRule {
  public:
    ConnectionRule(const AddressSpec&, const AddressSpec&, bool);

    static ConnectionRule exact(const Address&, const Address&);
    static ConnectionRule exactBlock(const Address&, const Address&);

    bool isBlockingRule() const { return blocking; }

    bool senderMatch(const Address& a) const   { return sender.matchAsSender(a); }
    bool destMatch(const Address& a) const     { return dest.matchAsDest(a); }
    bool match(const Address& s, const Address& d) const
      { return sender.matchAsSender(s) && dest.matchAsDest(d); }

    ConnectionRule(const ConnectionRule&) = default;
    ConnectionRule& operator=(const ConnectionRule&) = default;

    fmt::format_context::iterator format(fmt::format_context&) const;

  private:
    AddressSpec sender;
    AddressSpec dest;
    bool blocking;
};

using ConnectionRules = std::vector<ConnectionRule>;

bool parseRules(std::istream& input, ConnectionRules& rules);
bool parseRules(std::string input, ConnectionRules& rules);


template <> struct fmt::formatter<ClientSpec> : formatter<string_view> {
  auto format(const ClientSpec& c, format_context& ctx) const
    { return c.format(ctx); }
};

template <> struct fmt::formatter<PortSpec> : formatter<string_view> {
  auto format(const PortSpec& p, format_context& ctx) const
    { return p.format(ctx); }
};

template <> struct fmt::formatter<AddressSpec> : formatter<string_view> {
  auto format(const AddressSpec& a, format_context& ctx) const
    { return a.format(ctx); }
};

template <> struct fmt::formatter<ConnectionRule> : formatter<string_view> {
  auto format(const ConnectionRule& r, format_context& ctx) const
    { return r.format(ctx); }
};

inline std::ostream& operator<<(std::ostream& s, const ClientSpec& c)
  { s << fmt::format("{}", c); return s; }

inline std::ostream& operator<<(std::ostream& s, const PortSpec& p)
  { s << fmt::format("{}", p); return s; }

inline std::ostream& operator<<(std::ostream& s, const AddressSpec& a)
  { s << fmt::format("{}", a); return s; }

inline std::ostream& operator<<(std::ostream& s, const ConnectionRule& c)
  { s << fmt::format("{}", c); return s; }
