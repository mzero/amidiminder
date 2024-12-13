#include "rule.h"

#include <regex>
#include <string>
#include <string_view>
#include <vector>
#include <stdexcept>

#include "msg.h"


// TODO: support case insensitive matches for non-exact


namespace {
  template <typename Iter> inline
  Iter string_to(Iter i, std::string_view s) {
    return std::copy(s.begin(), s.end(), i);
  }
}

/*

== rules

# ...
    -- comment

_endpoint_ _connect_ _endpoint_

_connect_ ::=
    "<->" | "->" | "<-"     -- connections
    "<-x->" | "-x->" | "<-x-" -- blocking connections, cancel prior rule matches
       -- note: one or more dashes are supported in all forms

_endpoint_ ::=
    _client_                -- default to first port (of correct direction)
    _client_ ":" _port_     -- port on a given client
    "." ( "hw" | "app" )    -- match ports with given property (client wildcard)

_client_ ::=
    _words_                 -- substring find
    '"' _words_ '"'         -- exact match
    "*"                     -- all clients (!)

_port_ ::=
    _words_                 -- substring find
    '"' _words_ '"'         -- exact match
    '='_number_             -- port n
    "*"                     -- all ports

*/


ClientSpec::ClientSpec(Kind k, const std::string& c, int i)
  : kind(k), client(c), clientNum(i) { }

ClientSpec ClientSpec::partial(const std::string& s)
  { return ClientSpec(Partial, s, 0); }

ClientSpec ClientSpec::exact(const std::string& s)
  { return ClientSpec(Exact, s, 0); }

ClientSpec ClientSpec::numeric(int i)
  { return ClientSpec(Numeric, "", i); }

ClientSpec ClientSpec::wildcard()
  { return ClientSpec(Wildcard, "", 0); }


bool ClientSpec::match(const Address& a) const {
  switch (kind) {
    case Partial:   return a.client.find(client) != std::string::npos;
    case Exact:     return a.client == client;
    case Numeric:   return a.addr.client == clientNum;
    case Wildcard:  return true;
  }
  return false; // should never happen
}

bool ClientSpec::isWildcard() const
  { return kind == Wildcard; }

fmt::format_context::iterator
ClientSpec::format(fmt::format_context& ctx) const {
  switch (kind) {
    case Partial:   return string_to(ctx.out(), client);
    case Exact:     return fmt::format_to(ctx.out(), "\"{}\"", client);
    case Numeric:   return fmt::format_to(ctx.out(), "{:d}", clientNum);
    case Wildcard:  return string_to(ctx.out(), "*");
  }
  return ctx.out();
}


PortSpec::PortSpec(Kind k, const std::string& p, bool e, int n, unsigned int t)
  : kind(k), port(p), exactMatch(e), portNum(n), typeFlag(t)
  { }

PortSpec PortSpec::defaulted()
  { return PortSpec(Defaulted, "", false, -1, 0); }

PortSpec PortSpec::partial(const std::string& p)
  { return PortSpec(Partial, p, false, -1, 0); }

PortSpec PortSpec::exact(const std::string& p)
  { return PortSpec(Exact, p, true, -1, 0); }

PortSpec PortSpec::numeric(int n)
  { return PortSpec(Numeric, "", false, n, 0); }

PortSpec PortSpec::type(unsigned int t)
  { return PortSpec(Type, "", false, -1, t); }

PortSpec PortSpec::wildcard()
  { return PortSpec(Wildcard, "", false, -1, 0); }

bool PortSpec::isDefaulted() const
  { return kind == Defaulted; }

bool PortSpec::isType() const
  { return kind == Type; }

bool PortSpec::isWildcard() const
  { return kind == Wildcard; }

bool PortSpec::matchAsSender(const Address& a) const {
  return a.canBeSender() && match(a, a.primarySender);
}

bool PortSpec::matchAsDest(const Address& a) const {
  return a.canBeDest() && match(a, a.primaryDest);
}

bool PortSpec::match(const Address& a, bool primaryFlag) const {
  switch (kind) {
    case Defaulted:   return primaryFlag;
    case Partial:     return a.port.find(port) != std::string::npos
                              || a.portLong == port; // just in case...
    case Exact:       return a.port == port || a.portLong == port;
    case Numeric:     return a.addr.port == portNum;
    case Type:        return a.types & typeFlag;
    case Wildcard:    return true;
  }
  return false; // should never happen
}

fmt::format_context::iterator
PortSpec::format(fmt::format_context& ctx) const {
  switch (kind) {
    case Defaulted:   break;
    case Partial:     return string_to(ctx.out(), port);
    case Exact:       return fmt::format_to(ctx.out(), "\"{}\"", port);
    case Numeric:     return fmt::format_to(ctx.out(), "{}", portNum);
    case Type:
      switch (typeFlag) {
        case SND_SEQ_PORT_TYPE_HARDWARE:    return string_to(ctx.out(), ".hw");
        case SND_SEQ_PORT_TYPE_APPLICATION: return string_to(ctx.out(), ".app");
        default:
          return fmt::format_to(ctx.out(), "{:x}", typeFlag);
      }
    case Wildcard:    return string_to(ctx.out(), "*");
  }
  return ctx.out();
}


AddressSpec::AddressSpec(const ClientSpec& c, const PortSpec& p)
  : client(c), port(p)
  { }

AddressSpec AddressSpec::exact(const Address& a)
  { return AddressSpec(ClientSpec::exact(a.client), PortSpec::exact(a.port)); }
  // TODO: Decide if this should use PortSpec::numeric(a.addr.port) instead.

bool AddressSpec::matchAsSender(const Address& a) const
  { return client.match(a) && port.matchAsSender(a); }

bool AddressSpec::matchAsDest(const Address& a) const
  { return client.match(a) && port.matchAsDest(a); }

bool AddressSpec::isWildcard() const
  { return client.isWildcard() || port.isWildcard(); }

fmt::format_context::iterator
AddressSpec::format(fmt::format_context& ctx) const {
  if (client.isWildcard() && port.isType())   return fmt::format_to(ctx.out(), "{}", port);
  else if (port.isDefaulted())                return fmt::format_to(ctx.out(), "{}", client);
  else                                        return fmt::format_to(ctx.out(), "{}:{}", client, port);
}


ConnectionRule::ConnectionRule(
    const AddressSpec& s, const AddressSpec& d, bool b)
  : sender(s), dest(d), blocking(b)
  { }

ConnectionRule ConnectionRule::exact(const Address& s, const Address& d)
  { return ConnectionRule(AddressSpec::exact(s), AddressSpec::exact(d), false); }

ConnectionRule ConnectionRule::exactBlock(const Address& s, const Address& d)
  { return ConnectionRule(AddressSpec::exact(s), AddressSpec::exact(d), true); }

fmt::format_context::iterator
ConnectionRule::format(fmt::format_context& ctx) const {
  return fmt::format_to(ctx.out(), "{} {} {}",
    sender, (blocking ? "-x->" : "-->"), dest);
}



namespace {

  class ParseError : public std::runtime_error {
    public:
      template <typename... T>
      ParseError(const char* format, const T&... args)
        : std::runtime_error(fmt::format(format, args...))
        { }
  };

  ClientSpec parseClientSpec(const std::string& s) {
    std::smatch m;

    static const std::regex clientRE(
      "(\\*)|\"([^\"]+)\"|\'([^\']+)\'|([^*\"'=.].*)");
    if (!std::regex_match(s, m, clientRE))
      throw ParseError("malformed client '{}'", s);

    if (m.str(1).size())  return ClientSpec::wildcard();
    if (m.str(2).size())  return ClientSpec::exact(m.str(2));
    if (m.str(3).size())  return ClientSpec::exact(m.str(3));
    if (m.str(4).size())  return ClientSpec::partial(m.str(4));

    throw ParseError("parseClientSpec match failure with '{}'", s);
      // shouldn't ever happen!
  }

  PortSpec parsePortSpec(const std::string& s) {
    std::smatch m;

    static const std::regex portRE(
      "(\\*)|\"([^\"]+)\"|\'([^\']+)\'|=(\\d+)|([^*\"'=.].*)");
    if (!std::regex_match(s, m, portRE))
      throw ParseError("malformed port '{}'", s);

    if (m.str(1).size())  return PortSpec::wildcard();
    if (m.str(2).size())  return PortSpec::exact(m.str(2));
    if (m.str(3).size())  return PortSpec::exact(m.str(3));
    if (m.str(4).size())  return PortSpec::numeric(std::stoi(m.str(4)));
    if (m.str(5).size())  return PortSpec::partial(m.str(5));

    throw ParseError("parsePortSpec match failure with '{}'", s);
      // shouldn't ever happen!
  }

  AddressSpec parseAddressSpec(const std::string& s, bool allowIDs = false) {
    std::smatch m;

    static const std::regex idsRE("(\\d+):(\\d+)");
    if (std::regex_match(s, m, idsRE)) {
      if (!allowIDs)
        throw ParseError("client-id:port-id matches not allowed here");
      auto c = ClientSpec::numeric(std::stoi(m.str(1)));
      auto p = PortSpec::numeric(std::stoi(m.str(2)));
      return AddressSpec(c, p);
    }

    static const std::regex portTypeRE("\\.\\w+");
    if (std::regex_match(s, m, portTypeRE)) {
      unsigned int type = 0;
      if      (s == ".hw")    type = SND_SEQ_PORT_TYPE_HARDWARE;
      else if (s == ".app")   type = SND_SEQ_PORT_TYPE_APPLICATION;
      else
        throw ParseError("invalid port type '{}'", s);

      return AddressSpec(ClientSpec::wildcard(), PortSpec::type(type));
    }

    static const std::regex addressRE(
          "([^\"':][^:]*|\"[^\"]+\"|\'[^\']+\')"
        "(:([^\"':][^:]*|\"[^\"]+\"|\'[^\']+\'))?");
    if (!std::regex_match(s, m, addressRE))
      throw ParseError("malformed address '{}'", s);

    ClientSpec cs = parseClientSpec(m.str(1));
    PortSpec ps =
      m.str(2).empty()
        ? (cs.isWildcard()? PortSpec::wildcard() : PortSpec::defaulted())
        : parsePortSpec(m.str(3));

    return AddressSpec(cs, ps);
  }

  ConnectionRules parseConnectionRule(const std::string& s) {
    std::smatch m;

    static const std::regex ruleRE("(.*?)\\s+(-+(?:x-+)?>|<-+(?:x-+)?>?)\\s+(.*)");
    if (!std::regex_match(s, m, ruleRE))
      throw ParseError("malformed rule '{}'", s);

    AddressSpec left = parseAddressSpec(m.str(1));
    AddressSpec right = parseAddressSpec(m.str(3));

    ConnectionRules rules;
    std::string type = m.str(2);
    if (type.empty())
      throw ParseError("parseConnectionRule match failure with '{}'", s);
        // should never happen, because ruleRE ensures at least one character

    bool blocking = type.find('x') != std::string::npos;
    if (type.back() == '>')   rules.push_back(ConnectionRule(left, right, blocking));
    if (type.front() == '<')  rules.push_back(ConnectionRule(right, left, blocking));

    return rules;
  }

  ConnectionRules parseLine(const std::string& line) {
    std::smatch m;

    std::string ruleUntrimmed = line;
    bool expect_failure = false;

    static const std::regex decomment("([^#]*)#(.*)");
    if (std::regex_match(line, m, decomment)) {
      ruleUntrimmed = m.str(1);
      expect_failure = m.str(2).find("FAIL") != std::string::npos;
    }

    std::string rule = ruleUntrimmed;

    static const std::regex trimWhitespace("\\s*(.*?)\\s*");
    if (std::regex_match(ruleUntrimmed, m, trimWhitespace))
      rule = m.str(1);

    ConnectionRules r;
    if (rule.empty()) return r;

    try {
      r = parseConnectionRule(rule);
    }
    catch (const ParseError& p) {
      if (expect_failure) return r;
      throw;
    }

    if (expect_failure)
      throw ParseError("was not expected to parse");
    return r;
  }

}

AddressSpec AddressSpec::parse(const std::string& s, bool allowIDs) {
  return parseAddressSpec(s, allowIDs);
}

bool parseRules(std::istream& input, ConnectionRules& rules) {
  int lineNo = 1;
  bool good = true;
  for (std::string line; std::getline(input, line); ++lineNo) {
    try {
      auto newRules = parseLine(line);
      rules.insert(rules.end(), newRules.begin(), newRules.end());
    }
    catch (const ParseError& p) {
      Msg::error("Parse error on line {}: {}", lineNo, p.what());
      good = false;
    }
  }
  return good;
}

bool parseRules(std::string input, ConnectionRules& rules) {
  std::istringstream stream(input);
  return parseRules(stream, rules);
}

