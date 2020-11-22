#include "seq.h"


const Address Address::null;


void Seq::begin() {
  if (seq) return;

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

void Seq::end() {
  if (seq) {
    auto seq_ = seq;
    seq = nullptr;
    snd_seq_close(seq_);
  }
}


Address Seq::address(const snd_seq_addr_t& addr) {
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

  return
    Address(addr,
            snd_seq_client_info_get_name(client),
            snd_seq_port_info_get_name(port));
}

snd_seq_event_t* Seq::eventInput() {
  snd_seq_event_t *ev;
  auto q = snd_seq_event_input(seq, &ev);

  if (q == -EAGAIN)
    return nullptr;

  if (errCheck(q, "event input"))
    return nullptr;

  return ev;
}

void Seq::scanConnections(std::function<void(const snd_seq_connect_t&)> func) {
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
        func(conn);

        snd_seq_query_subscribe_set_index(query, ++index);
      }
    }
  }
}

void Seq::connect(const snd_seq_addr_t& sender, const snd_seq_addr_t& dest) {
  snd_seq_port_subscribe_t *subs;
  snd_seq_port_subscribe_alloca(&subs);
  snd_seq_port_subscribe_set_sender(subs, &sender);
  snd_seq_port_subscribe_set_dest(subs, &dest);

  // FIXME: these should be saved with the Connection & restored
  snd_seq_port_subscribe_set_queue(subs, 0);
  snd_seq_port_subscribe_set_exclusive(subs, 0);
  snd_seq_port_subscribe_set_time_update(subs, 0);
  snd_seq_port_subscribe_set_time_real(subs, 0);

  int serr;
  serr = snd_seq_subscribe_port(seq, subs);
  errCheck(serr, "subscribe");
}


bool Seq::errCheck(int serr, const char* op) {
  if (serr >= 0) return false;
  std::cerr << "ALSA Seq error " << std::dec << serr;
  if (op) std::cerr << " in " << op;
  std::cerr << std::endl;
  return true;
}

bool Seq::errFatal(int serr, const char* op) {
  bool r = errCheck(serr, op);
  if (r) end();
  return r;
}


void Address::output(std::ostream& s) const {
  if (valid)
    s << client << ":" << port << " [" << addr << "]";
  else
    s << "--:--";
}

void Seq::outputAddr(std::ostream& s, const snd_seq_addr_t& addr) {
  s << std::dec << static_cast<unsigned>(addr.client)
    << ":" << static_cast<unsigned>(addr.port);
}

void Seq::outputConnect(std::ostream& s, const snd_seq_connect_t& conn) {
  s << conn.sender << " --> " << conn.dest;
}
