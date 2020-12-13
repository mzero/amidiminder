#pragma once

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

    void output(std::ostream&) const;

  private:
    std::string client;
    bool exactMatch;

    ClientSpec(const std::string&, bool);
};

class PortSpec {
  public:
    static PortSpec exact(const std::string&);
    static PortSpec partial(const std::string&);
    static PortSpec numeric(int);
    static PortSpec type(unsigned int);
    static PortSpec wildcard();

    bool match (const Address&) const;

    PortSpec(const PortSpec&) = default;
    PortSpec& operator=(const PortSpec&) = default;

    void output(std::ostream&) const;

  private:
    std::string port;
    bool exactMatch;
    int portNum;
    unsigned int typeFlag;

    PortSpec(const std::string&, bool, int, unsigned int);
};


class AddressSpec {
  public:
    AddressSpec(const ClientSpec&, const PortSpec&);

    static AddressSpec exact(const Address&);

    bool match(const Address&) const;

    AddressSpec(const AddressSpec&) = default;
    AddressSpec& operator=(const AddressSpec&) = default;

    void output(std::ostream&) const;

  private:
    ClientSpec client;
    PortSpec port;
};


class ConnectionRule {
  public:
    ConnectionRule(const AddressSpec&, const AddressSpec&, bool);

    static ConnectionRule exact(const Address&, const Address&);

    bool isBlockingRule() const { return blocking; }

    bool senderMatch(const Address& a) const   { return sender.match(a); }
    bool destMatch(const Address& a) const     { return dest.match(a); }
    bool match(const Address& s, const Address& d) const
      { return sender.match(s) && dest.match(d); }

    ConnectionRule(const ConnectionRule&) = default;
    ConnectionRule& operator=(const ConnectionRule&) = default;

    void output(std::ostream&) const;

  private:
    AddressSpec sender;
    AddressSpec dest;
    bool blocking;
};

using ConnectionRules = std::vector<ConnectionRule>;

bool parseRulesFile(std::istream& input, ConnectionRules& rules);


inline std::ostream& operator<<(std::ostream& s, const ClientSpec& c)
  { c.output(s); return s; }

inline std::ostream& operator<<(std::ostream& s, const PortSpec& p)
  { p.output(s); return s; }

inline std::ostream& operator<<(std::ostream& s, const AddressSpec& a)
  { a.output(s); return s; }

inline std::ostream& operator<<(std::ostream& s, const ConnectionRule& c)
  { c.output(s); return s; }
