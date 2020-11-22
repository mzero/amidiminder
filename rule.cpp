#include "rule.h"

#include <string>

// TODO: support case insensitive matches for non-exact

/*

== rules

# ...
    -- comment

[_client_]
    -- following rules only apply when _client_ is present, and _client is implicit

_endpoint_ _connect_ _endpoint_

_connect_ ::=
    "<->" | "->" | "<-"     -- connections
    "<x>" | "x>" | "<x"     -- "anti-connections", keep other rules from matching

_endpoint_ ::=
    _client_                -- defaults to port 0
    _client_ ":" _port_     -- port on a given client
    ""                      -- defaults to port 0 of implicit client
    ":" _port_              -- port of implicit client
    "hardware" | "software" -- magic match types

_client_ ::=
    _words_                 -- case independent find
    '"' _words_ '"'         -- exact match
    "*"                     -- all clients (!)

_port_ ::=
    _words_                 -- case independent find
    '"' _words_ '"'         -- exact match
    _number_                -- port n
    "*"                     -- all ports



[bicycle]
:controllers <- nanoKEY2
:controllers <- nanoKONTROL
:controllers <- Launchpad Pro MK3:0
:controllers <- Launchpad Pro MK3:2
:synths -> Circuit:0
:synths -> Pisound

== amidiauto rules
[allow]
[disallow]
.... <-> ....
.... -> ....
.... <- ....
 where ... is either text of partial client name or *

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


PortSpec::PortSpec(const std::string& p, bool e, int n)
  : port(p), exactMatch(e), portNum(n)
  { }

PortSpec PortSpec::exact(const std::string& p)
  { return PortSpec(p, true, -1); }

PortSpec PortSpec::partial(const std::string& p)
  { return PortSpec(p, false, -1); }

PortSpec PortSpec::numeric(int n)
  { return PortSpec("", false, n); }

PortSpec PortSpec::wildcard()
  { return PortSpec("", false, -1); }

bool PortSpec::match(const Address& a) const {
  if      (portNum >= 0)  return a.addr.port == portNum;
  else if (exactMatch)    return a.port == port;
  else                    return a.port.find(port) != std::string::npos;
}

void PortSpec::output(std::ostream& s) const {
  if      (portNum >= 0)    s << portNum;
  else if (exactMatch)      s << '"' << port << '"';
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


ConnectionRule::ConnectionRule(const AddressSpec& s, const AddressSpec& d)
  : source(s), dest(d)
  { }

ConnectionRule ConnectionRule::exact(const Address& s, const Address& d)
  { return ConnectionRule(AddressSpec::exact(s), AddressSpec::exact(d)); }

void ConnectionRule::output(std::ostream& s) const
  { s << source << " --> " << dest; }
