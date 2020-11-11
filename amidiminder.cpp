#include <algorithm>
#include <alsa/asoundlib.h>
#include <iostream>
#include <list>


using namespace std;


/** TODO

  [] remember subscription options

  [] verbosity controls
  [] connection list on ^t
  [] persistent file of connections, read on startup
  [] auto connection file
    [] allow and disallow like amidiauto?
    [] patterns? regex?
    [] support for matching port types?
    [] support for subscription options (like aconnect?)

 **/

namespace {

  inline ostream& operator<<(ostream& s, const snd_seq_addr_t& addr) {
    s << dec << static_cast<unsigned>(addr.client)
      << ":" << static_cast<unsigned>(addr.port);
    return s;
  }

  inline ostream& operator<<(ostream& s, const snd_seq_connect_t& conn) {
    s << conn.sender << " --> " << conn.dest;
    return s;
  }


  struct Address {
    Address() { }
    Address(const string& c, const string& p) : client(c), port(p) { }
    Address(const char* c, const char* p) : client(c), port(p) { }

    const string client;
    const string port;

    operator bool() const { return !client.empty(); }

    bool isAddrValid() const { return addrValid; }
    const snd_seq_addr_t& getAddr() const { return addr; }

    void invalidateAddr() { addrValid = false; }
    void setAddr(const snd_seq_addr_t& addr_) {
      addrValid = true;
      addr = addr_;
    }

    bool matches(const snd_seq_addr_t& addr_) const {
      return addrValid
        && addr.client == addr_.client
        && addr.port == addr_.port;
    }
    bool matches(const Address& a) const {
      return client == a.client && port == a.port;
    }

    void outputOn(ostream& s) const {
      if (!*this) {
        s << "--:--";
        return;
      }
      s << client << ":" << port;
      if (addrValid)
        s << " [" << addr << "]";
      else
        s << " [--]";
    }

  private:
    bool addrValid = false;
    snd_seq_addr_t addr;
  };

  struct Connection {
    Address sender;
    Address dest;

    Connection(const Address& s, const Address& d) : sender(s), dest(d) { }

    bool matches(const snd_seq_connect_t& conn) const {
      return sender.matches(conn.sender) && dest.matches(conn.dest);
    }
  };


  inline ostream& operator<<(ostream& s, const Address& a) {
    a.outputOn(s);
    return s;
  }

  inline ostream& operator<<(ostream& s, const Connection& c) {
    s << c.sender << " ==>> " << c.dest;
    return s;
  }


}



class MidiMinder {
  public:
    MidiMinder() { begin(); }
    ~MidiMinder() { end(); }

    void run() {
      while (seq) {
        snd_seq_event_t *ev;
        auto q = snd_seq_event_input(seq, &ev);

        if (q == -EAGAIN) {
          sleep(1);
          continue;
        }

        if (errCheck(q, "event input"))
          continue;

        switch (ev->type) {
          case SND_SEQ_EVENT_CLIENT_START: {
            // cout << "client start " << ev->data.addr << endl;
            break;
          }

          case SND_SEQ_EVENT_CLIENT_EXIT: {
            // cout << "client exit " << ev->data.addr << endl;
            break;
          }

          case SND_SEQ_EVENT_CLIENT_CHANGE: {
            // cout << "client change " << ev->data.addr << endl;
            break;
          }

          case SND_SEQ_EVENT_PORT_START: {
            // cout << "port start " << ev->data.addr << endl;
            addPort(ev->data.addr);
            break;
          }

          case SND_SEQ_EVENT_PORT_EXIT: {
            // cout << "port exit " << ev->data.addr << endl;
            delPort(ev->data.addr);
            break;
          }

          case SND_SEQ_EVENT_PORT_CHANGE: {
            // cout << "port change " << ev->data.addr << endl;
            // FIXME: treat this as a add/remove?
            break;
          }

          case SND_SEQ_EVENT_PORT_SUBSCRIBED: {
            // cout << "port subscribed " << ev->data.connect << endl;
            addConnection(ev->data.connect);
            break;
          }

          case SND_SEQ_EVENT_PORT_UNSUBSCRIBED: {
            // cout << "port unsubscribed " << ev->data.connect << endl;
            delConnection(ev->data.connect);
            break;
          }

          default: {
            cout << "some other event ("
               << dec << static_cast<int>(ev->type) << ")" << endl;
          }
        }
      }
    }

  private:
    snd_seq_t *seq = nullptr;
    int evtPort;

    void begin() {
      int serr;

      serr = snd_seq_open(&seq, "default", SND_SEQ_OPEN_DUPLEX, 0);
      if (errFatal(serr, "open sequencer")) return;

      serr = snd_seq_set_client_name(seq, "amidiminder");
      if (errFatal(serr, "name sequencer")) return;

      evtPort = snd_seq_create_simple_port(seq, "panopticon",
        SND_SEQ_PORT_CAP_WRITE | SND_SEQ_PORT_CAP_NO_EXPORT,
        SND_SEQ_PORT_TYPE_APPLICATION);
      if (errFatal(evtPort, "create event port")) return;

	    serr = snd_seq_connect_from(seq, evtPort,
	      SND_SEQ_CLIENT_SYSTEM, SND_SEQ_PORT_SYSTEM_ANNOUNCE);
      if (errFatal(serr, "connect to system annouce port")) return;

      scanExistingConnections();
    }

    void end() {
      if (seq) {
        auto seq_ = seq;
        seq = nullptr;
        snd_seq_close(seq_);
      }
    }


    Address getAddress(const snd_seq_addr_t& addr) {
      if (addr.client == SND_SEQ_CLIENT_SYSTEM) return {};

      int serr;

      snd_seq_client_info_t *client;
      snd_seq_client_info_alloca(&client);
      serr = snd_seq_get_any_client_info(seq, addr.client, client);
      if (errCheck(serr, "get client info")) return {};

      snd_seq_port_info_t *port;
      snd_seq_port_info_alloca(&port);
      serr = snd_seq_get_any_port_info(seq, addr.client, addr.port, port);
      if (errCheck(serr, "get port info")) return {};

      // TODO: reject SND_SEQ_PORT_CAP_NO_EXPORT here?

      Address a(snd_seq_client_info_get_name(client),
                snd_seq_port_info_get_name(port));
      a.setAddr(addr);
      return a;
    }



    typedef list<Connection> Connections;
    Connections connections;

    void addPort(const snd_seq_addr_t& addr) {
      Address a = getAddress(addr);
      if (!a) return;

      for (auto&& c : connections) {
        bool activated = false;

        if (c.sender.matches(a)) {
          c.sender.setAddr(addr);
          activated = true;
        }

        if (c.dest.matches(a)) {
          c.dest.setAddr(addr);
          activated = true;
        }

        if (activated && c.sender.isAddrValid() && c.dest.isAddrValid()) {
          cout << "connection re-activated: " << c << endl;

          snd_seq_port_subscribe_t *subs;
          snd_seq_port_subscribe_alloca(&subs);
          snd_seq_port_subscribe_set_sender(subs, &c.sender.getAddr());
          snd_seq_port_subscribe_set_dest(subs, &c.dest.getAddr());

          // FIXME: these should be saved with the Connection & restored
          snd_seq_port_subscribe_set_queue(subs, 0);
          snd_seq_port_subscribe_set_exclusive(subs, 0);
          snd_seq_port_subscribe_set_time_update(subs, 0);
          snd_seq_port_subscribe_set_time_real(subs, 0);

          int serr;
          serr = snd_seq_subscribe_port(seq, subs);
          errCheck(serr, "subscribe");
        }
      }
    }

    void delPort(const snd_seq_addr_t& addr) {
      for (auto&& c : connections) {
        bool deactivated = false;

        if (c.sender.matches(addr)) {
          c.sender.invalidateAddr();
          deactivated = true;
        }

        if (c.dest.matches(addr)) {
          c.dest.invalidateAddr();
          deactivated = true;
        }

        if (deactivated)
          cout << "connection deactivated: " << c << endl;
      }
    }


    void addConnection(const snd_seq_connect_t& conn) {
      auto i =
        find_if(connections.begin(), connections.end(),
          [&](auto c){ return c.matches(conn); });

      if (i != connections.end())
          return; // already have it

      Address sender = getAddress(conn.sender);
      Address dest = getAddress(conn.dest);

      if (sender && dest) {
        Connection c(sender, dest);
        connections.push_back(c);
        cout << "adding connection " << c << endl;
      }
    }

    void delConnection(const snd_seq_connect_t& conn) {
      for (const auto& c : connections)
        if (c.matches(conn))
          cout << "removing connection " << c << endl;

      connections.remove_if(
        [&](auto c){ return c.matches(conn); });
    }


    void scanExistingConnections() {
      snd_seq_client_info_t *client;
      snd_seq_client_info_alloca(&client);

      snd_seq_port_info_t *port;
      snd_seq_port_info_alloca(&port);

      snd_seq_query_subscribe_t *query;
      snd_seq_query_subscribe_alloca(&query);

      snd_seq_port_subscribe_t *subs;
      snd_seq_port_subscribe_alloca(&subs);


      snd_seq_client_info_set_client(client, -1);
      while (snd_seq_query_next_client(seq, client) >= 0) {

        auto clientId = snd_seq_client_info_get_client(client);

        snd_seq_port_info_set_client(port, clientId);
        snd_seq_port_info_set_port(port, -1);
        while (snd_seq_query_next_port(seq, port) >= 0) {

          auto p0 = snd_seq_port_info_get_addr(port);

          int index;
          snd_seq_query_subscribe_set_root(query, p0);
          snd_seq_query_subscribe_set_type(query, SND_SEQ_QUERY_SUBS_READ);
          snd_seq_query_subscribe_set_index(query, index = 0);
          while (snd_seq_query_port_subscribers(seq, query) >= 0) {
            auto p1 = snd_seq_query_subscribe_get_addr(query);

            snd_seq_connect_t conn = { *p0, *p1 };
            addConnection(conn);

            snd_seq_query_subscribe_set_index(query, ++index);
          }
        }
      }
    }

    bool errCheck(int serr, const char* op) {
      if (serr >= 0) return false;
      std::cerr << "ALSA Seq error " << std::dec << serr;
      if (op) std::cerr << " in " << op;
      std::cerr << std::endl;
      return true;
    }

    bool errFatal(int serr, const char* op) {
      bool r = errCheck(serr, op);
      if (r) end();
      return r;
    }

};



int main() {
  MidiMinder mm;
  mm.run();
  return 0;
}
