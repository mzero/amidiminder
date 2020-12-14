#include "rule.h"

#include <regex>
#include <sstream>
#include <string>
#include <vector>

// TODO: support case insensitive matches for non-exact

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
    _client_                -- defaults to all ports
    _client_ ":" _port_     -- port on a given client
    "." ( "hw" | "app" )    -- match ports with given property (client wildcard)

_client_ ::=
    _words_                 -- case independent find
    '"' _words_ '"'         -- exact match
    "*"                     -- all clients (!)

_port_ ::=
    _words_                 -- case independent find
    '"' _words_ '"'         -- exact match
    _number_                -- port n
    "*"                     -- all ports

*/


ClientSpec::ClientSpec(const std::string& c, bool e)
  : client(c), exactMatch(e) { }

ClientSpec ClientSpec::exact(const std::string& s)
  { return ClientSpec(s, true); }

ClientSpec ClientSpec::partial(const std::string& s)
  { return ClientSpec(s, false); }

ClientSpec ClientSpec::wildcard()
  { return ClientSpec("", false); }

bool ClientSpec::match(const Address& a) const {
  if (exactMatch)   return a.client == client;
  else              return a.client.find(client) != std::string::npos;
}

void ClientSpec::output(std::ostream& s) const {
  if      (exactMatch)      s << '"' << client << '"';
  else if (client.empty())  s << '*';
  else                      s << client;
}


PortSpec::PortSpec(const std::string& p, bool e, int n, unsigned int t)
  : port(p), exactMatch(e), portNum(n), typeFlag(t)
  { }

PortSpec PortSpec::exact(const std::string& p)
  { return PortSpec(p, true, -1, 0); }

PortSpec PortSpec::partial(const std::string& p)
  { return PortSpec(p, false, -1, 0); }

PortSpec PortSpec::numeric(int n)
  { return PortSpec("", false, n, 0); }

PortSpec PortSpec::type(unsigned int t)
  { return PortSpec("", false, -1, t); }

PortSpec PortSpec::wildcard()
  { return PortSpec("", false, -1, 0); }

bool PortSpec::match(const Address& a) const {
  if      (portNum >= 0)  return a.addr.port == portNum;
  else if (exactMatch)    return a.port == port;
  else if (typeFlag)      return a.types & typeFlag;
  else                    return a.port.find(port) != std::string::npos;
}

namespace {
  void outputTypeFlag(std::ostream& s, unsigned int tf) {
    switch (tf) {
      case SND_SEQ_PORT_TYPE_HARDWARE:    s << ".hw";   break;
      case SND_SEQ_PORT_TYPE_APPLICATION: s << ".app";  break;
      default:                            s << '.' << std::hex << tf;
    }
  }
}

void PortSpec::output(std::ostream& s) const {
  if      (portNum >= 0)    s << portNum;
  else if (exactMatch)      s << '"' << port << '"';
  else if (typeFlag)        outputTypeFlag(s, typeFlag);
  else if (port.empty())    s << '*';
  else                      s << port;
}


AddressSpec::AddressSpec(const ClientSpec& c, const PortSpec& p)
  : client(c), port(p)
  { }

AddressSpec AddressSpec::exact(const Address& a)
  { return AddressSpec(ClientSpec::exact(a.client), PortSpec::exact(a.port)); }
  // TODO: Decide if this should use PortSpec::numeric(a.addr.port) instead.

bool AddressSpec::match(const Address& a) const
  { return client.match(a) && port.match(a); }

void AddressSpec::output(std::ostream& s) const
  { s << client << ':' << port; }


ConnectionRule::ConnectionRule(
    const AddressSpec& s, const AddressSpec& d, bool b)
  : sender(s), dest(d), blocking(b)
  { }

ConnectionRule ConnectionRule::exact(const Address& s, const Address& d)
  { return ConnectionRule(AddressSpec::exact(s), AddressSpec::exact(d), false); }

void ConnectionRule::output(std::ostream& s) const
  { s << sender << (blocking ? " -x-> " : " --> ") << dest; }



namespace {

  using Error = std::ostringstream;

  std::string errormsg(std::ostream& o) {
    auto e = dynamic_cast<Error*>(&o);
    return e ? e->str() : "parse error";
  }

  class Parse : public std::string {
    public:
      Parse(std::ostream& o) : std::string(errormsg(o)) { }
  };

  ClientSpec parseClientSpec(const std::string& s) {
    std::smatch m;

    static const std::regex clientRE(
      "(\\*)|\"([^\"]+)\"|([^*\"].*)");
    if (!std::regex_match(s, m, clientRE))
      throw Parse(Error() << "malformed client '" << s << "'");

    if (m.str(1).size())  return ClientSpec::wildcard();
    if (m.str(2).size())  return ClientSpec::exact(m.str(2));
    if (m.str(3).size())  return ClientSpec::partial(m.str(3));

    throw Parse(Error() << "parseClientSpec match failure with '" << s << "'");
      // shouldn't ever happen!
  }

  PortSpec parsePortSpec(const std::string& s) {
    std::smatch m;

    static const std::regex portRE(
      "(\\*)|\"([^\"]+)\"|(\\d+)|([^*\"].*)");
    if (!std::regex_match(s, m, portRE))
      throw Parse(Error() << "malformed port '" << s << "'");

    if (m.str(1).size())  return PortSpec::wildcard();
    if (m.str(2).size())  return PortSpec::exact(m.str(2));
    if (m.str(3).size())  return PortSpec::numeric(std::stoi(m.str(3)));
    if (m.str(4).size())  return PortSpec::partial(m.str(4));

    throw Parse(Error() << "parsePortSpec match failure with '" << s << "'");
      // shouldn't ever happen!
  }

  AddressSpec parseAddressSpec(const std::string& s) {
    std::smatch m;

    static const std::regex portTypeRE("\\.\\w+");
    if (std::regex_match(s, m, portTypeRE)) {
      unsigned int type = 0;
      if      (s == ".hw")    type = SND_SEQ_PORT_TYPE_HARDWARE;
      else if (s == ".app")   type = SND_SEQ_PORT_TYPE_APPLICATION;
      else
        throw Parse(Error() << "invalid port type '" << s << "'");

      return AddressSpec(ClientSpec::wildcard(), PortSpec::type(type));
    }

    static const std::regex addressRE("([^:.]*)(:([^:.]*))?");
    if (!std::regex_match(s, m, addressRE))
      throw Parse(Error() << "malformed address '" << s << "'");

    ClientSpec cs = parseClientSpec(m.str(1));
    PortSpec ps =
      m.str(2).empty() ? PortSpec::wildcard() : parsePortSpec(m.str(3));

    return AddressSpec(cs, ps);
  }

  ConnectionRules parseConnectionRule(const std::string& s) {
    std::smatch m;

    static const std::regex ruleRE("(.*?)\\s+(-+(?:x-+)?>|<-+(?:x-+)?>?)\\s+(.*)");
    if (!std::regex_match(s, m, ruleRE))
      throw Parse(Error() << "malformed rule '" << s << "'");

    AddressSpec left = parseAddressSpec(m.str(1));
    AddressSpec right = parseAddressSpec(m.str(3));

    ConnectionRules rules;
    std::string type = m.str(2);
    if (type.empty())
      throw Parse(Error() << "parseConnectionRule match failure with '" << s << "'");
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
    catch (Parse& p) {
      if (expect_failure) return r;
      throw;
    }

    if (expect_failure)
      throw Parse(Error() << "was not expected to parse");
    return r;
  }

}


bool parseRulesFile(std::istream& input, ConnectionRules& rules) {
  int lineNo = 1;
  bool good = true;
  for (std::string line; std::getline(input, line); ++lineNo) {
    try {
      auto newRules = parseLine(line);
      rules.insert(rules.end(), newRules.begin(), newRules.end());
    }
    catch (Parse& p) {
      std::cerr << "configuration line " << lineNo << " error: " << p << "\n";
      good = false;
    }
  }
  return good;
}


