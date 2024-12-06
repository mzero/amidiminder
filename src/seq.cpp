#include "seq.h"

#include <sstream>
#include <string_view>
#include <vector>

#include "msg.h"


const Address Address::null;

Address::Address(const snd_seq_addr_t& a, unsigned int f, unsigned int t,
    const std::string& c, const std::string& p)
  : valid(true), addr(a), caps(f), types(t), client(c), port(p), portLong(p),
    primarySender(false), primaryDest(false)
{
  static const std::string whitespace = " _";
  std::string_view trimmed(portLong);

  while (true) {
    if (trimmed.size() > 0
        && whitespace.find(trimmed[0]) != whitespace.npos) {
      trimmed.remove_prefix(1);
      continue;
    }
    if (trimmed.size() > client.size()
                          // not >= as we want there to be something left
        && trimmed.substr(0, client.size()) == client) {
      trimmed.remove_prefix(client.size());
      continue;
    }
    break;
  }
  while (true) {
    if (trimmed.size() > 0
        && whitespace.find(trimmed[trimmed.size()-1]) != whitespace.npos) {
      trimmed.remove_suffix(1);
      continue;
    }
    break;
  }

  if (trimmed.size() > 0)
    port = trimmed;
}

void Seq::begin() {
  if (seq) return;

  int serr;

  serr = snd_seq_open(&seq, "default", SND_SEQ_OPEN_DUPLEX, 0);
  if (errFatal(serr, "open sequencer")) return;

  int ret = snd_seq_client_id(seq);
  if (errFatal(ret, "client id")) return;
  seqClient = ret;

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


std::string Seq::clientName(client_id_t c) {
  if (c == SND_SEQ_CLIENT_SYSTEM) return "";

  int serr;

  snd_seq_client_info_t *client;
  snd_seq_client_info_alloca(&client);
  serr = snd_seq_get_any_client_info(seq, c, client);
  if (serr == -ENOENT) return {}; // client has already exited!
  if (errCheck(serr, "get client info")) return "";

  return snd_seq_client_info_get_name(client);
}


Address Seq::address(const snd_seq_addr_t& addr) {
  if (addr.client == SND_SEQ_CLIENT_SYSTEM) return {};

  int serr;

  snd_seq_client_info_t *client;
  snd_seq_client_info_alloca(&client);
  serr = snd_seq_get_any_client_info(seq, addr.client, client);
  if (serr == -ENOENT) return {}; // client has already exited!
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

void Seq::scanClients(std::function<void(const client_id_t)> func) {
  snd_seq_client_info_t *client;
  snd_seq_client_info_alloca(&client);

  snd_seq_client_info_set_client(client, -1);
  while (snd_seq_query_next_client(seq, client) >= 0) {
    auto clientId = snd_seq_client_info_get_client(client);
    if (clientId == SND_SEQ_CLIENT_SYSTEM) continue;

    func(clientId);
  }
}


void Seq::scanPorts(std::function<void(const snd_seq_addr_t&)> func) {
  snd_seq_client_info_t *client;
  snd_seq_client_info_alloca(&client);

  snd_seq_port_info_t *port;
  snd_seq_port_info_alloca(&port);

  snd_seq_client_info_set_client(client, -1);
  while (snd_seq_query_next_client(seq, client) >= 0) {
    auto clientId = snd_seq_client_info_get_client(client);
    if (clientId == SND_SEQ_CLIENT_SYSTEM) continue;

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
  Msg::error("ALSA Seq error {} in {}", serr, op);
  return true;
}

bool Seq::errFatal(int serr, const char* op) {
  bool r = errCheck(serr, op);
  if (r) end();
  return r;
}



fmt::format_context::iterator
Address::format(fmt::format_context& ctx) const {
  if (valid)
    return fmt::format_to(ctx.out(), "{}:{} [{}]{}", client, port, addr,
      ((primarySender || primaryDest) ? "+" : ""));
  else
    return fmt::format_to(ctx.out(), "--:--");
}

fmt::format_context::iterator
fmt::formatter<snd_seq_addr_t>::format(
    const snd_seq_addr_t& a, format_context& ctx) const {
  return format_to(ctx.out(), "{:d}:{:d}", a.client, a.port);
}

fmt::format_context::iterator
fmt::formatter<snd_seq_connect_t>::format(
    const snd_seq_connect_t& c, format_context& ctx) const {
  return format_to(ctx.out(), "{} --> {}", c.sender, c.dest);
}

fmt::format_context::iterator
fmt::formatter<snd_seq_event_t>::format(
    const snd_seq_event_t& ev, format_context& ctx) const {
  switch (ev.type) {
	case SND_SEQ_EVENT_CLIENT_START:
    return format_to(ctx.out(), "SND_SEQ_EVENT_CLIENT_START {}", ev.data.addr);

	case SND_SEQ_EVENT_CLIENT_EXIT:
    return format_to(ctx.out(), "SND_SEQ_EVENT_CLIENT_EXIT {}", ev.data.addr);

	case SND_SEQ_EVENT_CLIENT_CHANGE:
    return format_to(ctx.out(), "SND_SEQ_EVENT_CLIENT_CHANGE {}", ev.data.addr);

	case SND_SEQ_EVENT_PORT_START:
    return format_to(ctx.out(), "SND_SEQ_EVENT_PORT_START {}", ev.data.addr);

	case SND_SEQ_EVENT_PORT_EXIT:
    return format_to(ctx.out(), "SND_SEQ_EVENT_PORT_EXIT {}", ev.data.addr);

	case SND_SEQ_EVENT_PORT_CHANGE:
    return format_to(ctx.out(), "SND_SEQ_EVENT_PORT_CHANGE {}", ev.data.addr);

	case SND_SEQ_EVENT_PORT_SUBSCRIBED:
    return format_to(ctx.out(), "SND_SEQ_EVENT_PORT_SUBSCRIBED {}", ev.data.connect);

	case SND_SEQ_EVENT_PORT_UNSUBSCRIBED:
    return format_to(ctx.out(), "SND_SEQ_EVENT_PORT_UNSUBSCRIBED {}", ev.data.connect);

  default:
    return format_to(ctx.out(), "SND_SEQ_EVENT type {:d} ", ev.type);
  }
}

std::string Address::capsString() const {
  std::vector<const char*> capStrs;
  if (caps & SND_SEQ_PORT_CAP_READ)           capStrs.push_back("read");
  if (caps & SND_SEQ_PORT_CAP_WRITE)          capStrs.push_back("write");
  if (caps & SND_SEQ_PORT_CAP_SYNC_READ)      capStrs.push_back("sync read");
  if (caps & SND_SEQ_PORT_CAP_SYNC_WRITE)     capStrs.push_back("sync write");
  if (caps & SND_SEQ_PORT_CAP_DUPLEX)         capStrs.push_back("duplex");
  if (caps & SND_SEQ_PORT_CAP_SUBS_READ)      capStrs.push_back("subs read");
  if (caps & SND_SEQ_PORT_CAP_SUBS_WRITE)     capStrs.push_back("subs write");
  if (caps & SND_SEQ_PORT_CAP_NO_EXPORT)      capStrs.push_back("no export");

  std::ostringstream out;
  int count = 0;
  for (auto& s : capStrs)
    out << (count++ ? ", " : "") << s;
  return out.str();
}

std::string Address::typeString() const {
  std::vector<const char*> typeStrs;
  if (types & SND_SEQ_PORT_TYPE_SPECIFIC)     typeStrs.push_back("specific");
  if (types & SND_SEQ_PORT_TYPE_MIDI_GENERIC) typeStrs.push_back("midi generic");
  if (types & SND_SEQ_PORT_TYPE_MIDI_GM)      typeStrs.push_back("midi gm");
  if (types & SND_SEQ_PORT_TYPE_MIDI_GS)      typeStrs.push_back("midi gs");
  if (types & SND_SEQ_PORT_TYPE_MIDI_XG)      typeStrs.push_back("midi xg");
  if (types & SND_SEQ_PORT_TYPE_MIDI_MT32)    typeStrs.push_back("midi mt32");
  if (types & SND_SEQ_PORT_TYPE_MIDI_GM2)     typeStrs.push_back("midi gm2");
  if (types & SND_SEQ_PORT_TYPE_SYNTH)        typeStrs.push_back("synth");
  if (types & SND_SEQ_PORT_TYPE_DIRECT_SAMPLE)typeStrs.push_back("direct sample");
  if (types & SND_SEQ_PORT_TYPE_SAMPLE)       typeStrs.push_back("sample");
  if (types & SND_SEQ_PORT_TYPE_HARDWARE)     typeStrs.push_back("hardware");
  if (types & SND_SEQ_PORT_TYPE_SOFTWARE)     typeStrs.push_back("software");
  if (types & SND_SEQ_PORT_TYPE_SYNTHESIZER)  typeStrs.push_back("synthesizer");
  if (types & SND_SEQ_PORT_TYPE_PORT)         typeStrs.push_back("port");
  if (types & SND_SEQ_PORT_TYPE_APPLICATION)  typeStrs.push_back("application");

  std::ostringstream out;
  int count = 0;
  for (auto& s : typeStrs)
    out << (count++ ? ", " : "") << s;
  return out.str();
}

std::string Seq::clientDetails(client_id_t c) {
  int serr;

  snd_seq_client_info_t *client;
  snd_seq_client_info_alloca(&client);
  serr = snd_seq_get_any_client_info(seq, c, client);
  if (errCheck(serr, "get client info")) return "???";

  std::ostringstream out;

  auto cType = snd_seq_client_info_get_type(client);
  std::string cTypeStr;
  switch (cType) {
    case SND_SEQ_KERNEL_CLIENT: {
      auto card = snd_seq_client_info_get_card(client);
      out << "kernel(card=" << card << ")";
      break;
    }
    case SND_SEQ_USER_CLIENT: {
      auto pid = snd_seq_client_info_get_pid(client);
      out << "user(pid=" << pid << ")";
      break;
    }
    default:
      out << "unknown type";
  }

  // There are other info fields, but they don't see particularly useful
  // for printing out in a listing

  return out.str();
}

