#include <algorithm>
#include <alsa/asoundlib.h>
#include <iostream>
#include <list>
#include <optional>


using namespace std;


/** TODO

  [] enumerate exisiting connections
  [] track connections via subscribe and unsubscribe events
  [] print connections by name
  [] ignore connections to....
    [] snd_seq_client_type_t  SND_SEQ_KERNEL  ?? (or is this used for HW ports?)
        ? SND_SEQ_PORT_TYPE_???
        ? SND_SEQ_PORT_CAP_NO_EXPORT
        ! client id ==  SND_SEQ_CLIENT_SYSTEM

 **/

namespace {
  struct Address {
    Address(const string& c, const string& p) : client(c), port(p) { }
    Address(const char* c, const char* p) : client(c), port(p) { }

    const string client;
    const string port;

    void invalidateAddr() { addrValid = false; }
    void setAddr(const snd_seq_addr_t& addr_) {
      addrValid = true;
      addr = addr_;
    }
    bool matches(const snd_seq_addr_t& addr_) {
      return addrValid
        && addr.client == addr_.client
        && addr.port == addr_.port;
    }
  private:
    bool addrValid = false;
    snd_seq_addr_t addr;
  };

  struct Connection {
    Address sender;
    Address dest;

    bool matches(const snd_seq_connect_t& conn) {
      return sender.matches(conn.sender) && dest.matches(conn.dest);
    }
  };
}

inline ostream& operator<<(ostream& s, const snd_seq_addr_t& addr) {
  s << dec << static_cast<unsigned>(addr.client)
    << ":" << static_cast<unsigned>(addr.port);
  return s;
}

inline ostream& operator<<(ostream& s, const snd_seq_connect_t& conn) {
  s << conn.sender << " --> " << conn.dest;
  return s;
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
#if 0
          case SND_SEQ_EVENT_CLIENT_START: {
            cout << "client start " << ev->data.addr << endl;
            break;
          }

          case SND_SEQ_EVENT_CLIENT_EXIT: {
            cout << "client exit " << ev->data.addr << endl;
            break;
          }

          case SND_SEQ_EVENT_CLIENT_CHANGE: {
            cout << "client change " << ev->data.addr << endl;
            break;
          }
#endif
          case SND_SEQ_EVENT_PORT_START: {
            cout << "port start " << ev->data.addr << endl;
            addPort(ev->data.addr);
            break;
          }

          case SND_SEQ_EVENT_PORT_EXIT: {
            cout << "port exit " << ev->data.addr << endl;
            delPort(ev->data.addr);
            break;
          }
#if 0
          case SND_SEQ_EVENT_PORT_CHANGE: {
            cout << "port change " << ev->data.addr << endl;
            break;
          }
#endif
          case SND_SEQ_EVENT_PORT_SUBSCRIBED: {
            cout << "port subscribed " << ev->data.connect << endl;
            addConnection(ev->data.connect);
            break;
          }

          case SND_SEQ_EVENT_PORT_UNSUBSCRIBED: {
            cout << "port unsubscribed " << ev->data.connect << endl;
            delConnection(ev->data.connect);
            break;
          }

          default: {
            cout << "some other event (" << dec << ev->type << ")" << endl;
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
    }

    void end() {
      if (seq) {
        auto seq_ = seq;
        seq = nullptr;
        snd_seq_close(seq_);
      }
    }


    optional<Address> getAddress(const snd_seq_addr_t& addr) {
      if (addr.client == SND_SEQ_CLIENT_SYSTEM) return nullopt;

      int serr;

      snd_seq_client_info_t *client;
      snd_seq_client_info_alloca(&client);
      serr = snd_seq_get_any_client_info(seq, addr.client, client);
      if (errCheck(serr, "get client info")) return nullopt;

      snd_seq_port_info_t *port;
      snd_seq_port_info_alloca(&port);
      serr = snd_seq_get_any_port_info(seq, addr.client, addr.port, &port);
      if (errCheck(serr, "get port info")) return nullopt;

      // TODO: reject SND_SEQ_PORT_CAP_NO_EXPORT here?

      Address a(snd_seq_client_info_get_name(client),
                snd_seq_port_info_get_name(port));
      a.setAddr(addr);
      return a;
    }



    typedef list<Connection> Connections;
    Connections connections;

    void addPort(const snd_seq_addr_t& addr) {
      // get port and client names
      // reject add if port it is not to be considered
      for (auto c : connections) {
        // for each of sender and dest
          // if names match this addr's names
          // and it's inactive
            // make this side active
            // if the other side is active
              // make the subscription
              // add to connections? (unless we get events about our own adds?
      }
    }

    void delPort(const snd_seq_addr_t& addr) {
      // get port and client anmes
      for (auto c : connections) {
        // for each of sender and dest
          // if names match this addr's names
            // make inactive
      }
    }


    void addConnection(const snd_seq_connect_t& conn) {
      auto i =
        find_if(connections.begin(), connections.end(),
          [&](auto c){ return c.matches(conn); });

      if (i != connections.end())
          return; // already have it

      // get port and client names
      // reject if either is not to be considered
      // build Connection
      // add to list
    }

    void delConnection(const snd_seq_connect_t& conn) {
      connections.remove_if(
        [&](auto c){ return c.matches(conn); });
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
