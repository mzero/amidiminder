#include "seq.h"

#include <vector>


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
  if (errFatal(serr, "connect to system announce port")) return;

}

void Seq::end() {
  if (seq) {
    auto seq_ = seq;
    seq = nullptr;
    snd_seq_close(seq_);
  }
}


std::string Seq::clientName(const snd_seq_addr_t& addr) {
  if (addr.client == SND_SEQ_CLIENT_SYSTEM) return "";

  int serr;

  snd_seq_client_info_t *client;
  snd_seq_client_info_alloca(&client);
  serr = snd_seq_get_any_client_info(seq, addr.client, client);
  if (errCheck(serr, "get client info")) return "";

  return snd_seq_client_info_get_name(client);
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

  auto caps = snd_seq_port_info_get_capability(port);
  if (caps & SND_SEQ_PORT_CAP_NO_EXPORT) return {};
  if (!(caps & (SND_SEQ_PORT_CAP_SUBS_READ | SND_SEQ_PORT_CAP_SUBS_WRITE)))
    return {};

  auto types = snd_seq_port_info_get_type(port);

  return
    Address(addr, caps, types,
            snd_seq_client_info_get_name(client),
            snd_seq_port_info_get_name(port));
}

void Seq::scanFDs(std::function<void(int)> fn) {
  int npfd = snd_seq_poll_descriptors_count(seq, POLLIN);
  auto pfds = (struct pollfd *)alloca(npfd * sizeof(struct pollfd));
  npfd = snd_seq_poll_descriptors(seq, pfds, npfd, POLLIN);
  for (int i = 0; i < npfd; ++i)
    fn(pfds[i].fd);
}

snd_seq_event_t* Seq::eventInput() {
  if (snd_seq_event_input_pending(seq, 1) == 0)
    return nullptr;

  snd_seq_event_t *ev;
  auto q = snd_seq_event_input(seq, &ev);

  if (q == -EAGAIN)
    return nullptr;

  if (errCheck(q, "event input"))
    return nullptr;

  return ev;
}

void Seq::scanPorts(std::function<void(const snd_seq_addr_t&)> func) {
  snd_seq_client_info_t *client;
  snd_seq_client_info_alloca(&client);

  snd_seq_port_info_t *port;
  snd_seq_port_info_alloca(&port);

  snd_seq_client_info_set_client(client, -1);
  while (snd_seq_query_next_client(seq, client) >= 0) {
    auto clientId = snd_seq_client_info_get_client(client);

    // Note: The ALSA docs imply that the ports will be scanned
    // in numeric order. A review of the kernel code found that
    // it explicitly does so. The rest of the code relies on
    // this property, so if it ever changes, this code would need
    // to gather the snd_seq_addr_t values and sort them before
    // passing them to the call back func.

    snd_seq_port_info_set_client(port, clientId);
    snd_seq_port_info_set_port(port, -1);
    while (snd_seq_query_next_port(seq, port) >= 0) {

      snd_seq_addr_t addr = *snd_seq_port_info_get_addr(port);
      func(addr);
    }
  }
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
  if (serr == -EBUSY) return;  // connection is already made
  errCheck(serr, "subscribe");
}

void Seq::disconnect(const snd_seq_connect_t& conn) {
  snd_seq_port_subscribe_t *subs;
  snd_seq_port_subscribe_alloca(&subs);
  snd_seq_port_subscribe_set_sender(subs, &conn.sender);
  snd_seq_port_subscribe_set_dest(subs, &conn.dest);

  int serr;
  serr = snd_seq_unsubscribe_port(seq, subs);
  if (serr == -ENOENT) return;  // connection not found
  errCheck(serr, "unsubscribe");
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
    s << client << ":" << port
      << " [" << addr << "]"
      << ((primarySender || primaryDest) ? "+" : "");
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

void Seq::outputEvent(std::ostream& s, const snd_seq_event_t& ev) {
  switch (ev.type) {
	case SND_SEQ_EVENT_CLIENT_START:
    s << "SND_SEQ_EVENT_CLIENT_START ";
    outputAddr(s, ev.data.addr);
    break;

	case SND_SEQ_EVENT_CLIENT_EXIT:
    s << "SND_SEQ_EVENT_CLIENT_EXIT ";
    outputAddr(s, ev.data.addr);
    break;

	case SND_SEQ_EVENT_CLIENT_CHANGE:
    s << "SND_SEQ_EVENT_CLIENT_CHANGE ";
    outputAddr(s, ev.data.addr);
    break;

	case SND_SEQ_EVENT_PORT_START:
    s << "SND_SEQ_EVENT_PORT_START ";
    outputAddr(s, ev.data.addr);
    break;

	case SND_SEQ_EVENT_PORT_EXIT:
    s << "SND_SEQ_EVENT_PORT_EXIT ";
    outputAddr(s, ev.data.addr);
    break;

	case SND_SEQ_EVENT_PORT_CHANGE:
    s << "SND_SEQ_EVENT_PORT_CHANGE ";
    outputAddr(s, ev.data.addr);
    break;

	case SND_SEQ_EVENT_PORT_SUBSCRIBED:
    s << "SND_SEQ_EVENT_PORT_SUBSCRIBED ";
    outputConnect(s, ev.data.connect);
    break;

	case SND_SEQ_EVENT_PORT_UNSUBSCRIBED:
    s << "SND_SEQ_EVENT_PORT_UNSUBSCRIBED ";
    outputConnect(s, ev.data.connect);
    break;

  default:
    s << "SND_SEQ_EVENT type " << ev.type;
  }
}



void Seq::outputAddrDetails(std::ostream& out, const snd_seq_addr_t& addr) {
  int serr;

  snd_seq_client_info_t *client;
  snd_seq_client_info_alloca(&client);
  serr = snd_seq_get_any_client_info(seq, addr.client, client);
  if (errCheck(serr, "get client info")) return;

  snd_seq_client_type_t cType = snd_seq_client_info_get_type(client);
  std::string cTypeStr;
  switch (cType) {
    case SND_SEQ_KERNEL_CLIENT:   cTypeStr = "kernel";    break;
    case SND_SEQ_USER_CLIENT:     cTypeStr = "user";      break;
    default:                      cTypeStr = "???";
  }
  std::string cName = snd_seq_client_info_get_name(client);

  snd_seq_port_info_t *port;
  snd_seq_port_info_alloca(&port);
  serr = snd_seq_get_any_port_info(seq, addr.client, addr.port, port);
  if (errCheck(serr, "get port info")) return;

  std::string pName = snd_seq_port_info_get_name(port);

  auto pCap = snd_seq_port_info_get_capability(port);
  std::vector<const char*> pCapStrs;
  if (pCap & SND_SEQ_PORT_CAP_READ)           pCapStrs.push_back("read");
  if (pCap & SND_SEQ_PORT_CAP_WRITE)          pCapStrs.push_back("write");
  if (pCap & SND_SEQ_PORT_CAP_SYNC_READ)      pCapStrs.push_back("sync read");
  if (pCap & SND_SEQ_PORT_CAP_SYNC_WRITE)     pCapStrs.push_back("sync write");
  if (pCap & SND_SEQ_PORT_CAP_DUPLEX)         pCapStrs.push_back("duplex");
  if (pCap & SND_SEQ_PORT_CAP_SUBS_READ)      pCapStrs.push_back("subs read");
  if (pCap & SND_SEQ_PORT_CAP_SUBS_WRITE)     pCapStrs.push_back("subs write");
  if (pCap & SND_SEQ_PORT_CAP_NO_EXPORT)      pCapStrs.push_back("no export");

  auto pType = snd_seq_port_info_get_type(port);
  std::vector<const char*> pTypeStrs;
  if (pType & SND_SEQ_PORT_TYPE_SPECIFIC)     pTypeStrs.push_back("specific");
  if (pType & SND_SEQ_PORT_TYPE_MIDI_GENERIC) pTypeStrs.push_back("midi generic");
  if (pType & SND_SEQ_PORT_TYPE_MIDI_GM)      pTypeStrs.push_back("midi gm");
  if (pType & SND_SEQ_PORT_TYPE_MIDI_GS)      pTypeStrs.push_back("midi gs");
  if (pType & SND_SEQ_PORT_TYPE_MIDI_XG)      pTypeStrs.push_back("midi xg");
  if (pType & SND_SEQ_PORT_TYPE_MIDI_MT32)    pTypeStrs.push_back("midi mt32");
  if (pType & SND_SEQ_PORT_TYPE_MIDI_GM2)     pTypeStrs.push_back("midi gm2");
  if (pType & SND_SEQ_PORT_TYPE_SYNTH)        pTypeStrs.push_back("synth");
  if (pType & SND_SEQ_PORT_TYPE_DIRECT_SAMPLE)pTypeStrs.push_back("direct sample");
  if (pType & SND_SEQ_PORT_TYPE_SAMPLE)       pTypeStrs.push_back("sample");
  if (pType & SND_SEQ_PORT_TYPE_HARDWARE)     pTypeStrs.push_back("hardware");
  if (pType & SND_SEQ_PORT_TYPE_SOFTWARE)     pTypeStrs.push_back("software");
  if (pType & SND_SEQ_PORT_TYPE_SYNTHESIZER)  pTypeStrs.push_back("synthesizer");
  if (pType & SND_SEQ_PORT_TYPE_PORT)         pTypeStrs.push_back("port");
  if (pType & SND_SEQ_PORT_TYPE_APPLICATION)  pTypeStrs.push_back("application");

  out << "[" << std::dec << int(addr.client) << ":" << int(addr.port) << "] "
    << cName << ":" << pName << std::endl;
  out << "    client type: " << cTypeStr << std::endl;

  out << "    port caps:   ";
  int count = 0;
  for (auto& s : pCapStrs)
    out << (count++ ? ", " : "") << s;
  out << std::endl;

  out << "    port types:  ";
  count = 0;
  for (auto& s : pTypeStrs)
    out << (count++ ? ", " : "") << s;
  out << std::endl;
}

