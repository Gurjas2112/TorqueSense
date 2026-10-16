#include "LineSimulator.hpp"

#include <algorithm>
#include <chrono>

#include "Affinity.hpp"
#include "open62541.h"

namespace sunverify::app {
namespace {

bool write_bool(UA_Client* c, UA_UInt16 ns, const std::string& name, bool value) {
  UA_Boolean v = value;
  UA_Variant var;
  UA_Variant_setScalar(&var, &v, &UA_TYPES[UA_TYPES_BOOLEAN]);
  return UA_Client_writeValueAttribute(c, UA_NODEID_STRING(ns, const_cast<char*>(name.c_str())), &var) ==
         UA_STATUSCODE_GOOD;
}

}  // namespace

LineSimulator::LineSimulator(std::string endpoint, std::string release_tag)
    : endpoint_(std::move(endpoint)), release_tag_(std::move(release_tag)) {}

LineSimulator::~LineSimulator() { stop(); }

void LineSimulator::start() {
  running_ = true;
  thread_ = std::thread([this] { run(); });
}

void LineSimulator::stop() {
  if (!running_.exchange(false)) return;
  cv_.notify_all();
  if (thread_.joinable()) thread_.join();
}

void LineSimulator::begin_cycle(std::int64_t t0_ns, std::vector<LineEvent> events) {
  std::sort(events.begin(), events.end(), [](const LineEvent& a, const LineEvent& b) { return a.t_ms < b.t_ms; });
  {
    std::lock_guard lk(m_);
    schedule_ = std::move(events);
    t0_ns_ = t0_ns;
    fresh_ = true;
  }
  cv_.notify_all();
}

void LineSimulator::run() {
  name_current_thread("line-sim");
  UA_Client* client = nullptr;
  UA_UInt16 ns = 2;
  auto connect = [&] {
    if (client) {
      UA_Client_disconnect(client);
      UA_Client_delete(client);
    }
    client = UA_Client_new();
    UA_ClientConfig* cc = UA_Client_getConfig(client);
    UA_ClientConfig_setDefault(cc);
    cc->logging->log = nullptr;
    cc->timeout = 500;
    if (UA_Client_connect(client, endpoint_.c_str()) != UA_STATUSCODE_GOOD) return false;
    UA_String uri = UA_STRING(const_cast<char*>("urn:torquesense:station"));
    UA_Client_NamespaceGetIndex(client, &uri, &ns);
    return true;
  };
  bool up = connect();
  std::vector<LineEvent> plan;
  std::int64_t t0 = 0;
  std::size_t next = 0;
  while (running_) {
    {
      std::unique_lock lk(m_);
      if (fresh_) {
        plan = schedule_;
        t0 = t0_ns_;
        next = 0;
        fresh_ = false;
        lk.unlock();
        if (!up) up = connect();
        // a new part arrives: clear the previous cycle's release request and plant signals
        if (up) {
          write_bool(client, ns, release_tag_, false);
          for (const auto& e : plan) write_bool(client, ns, e.tag, false);
        }
      }
    }
    while (next < plan.size() && now_ns() >= t0 + plan[next].t_ms * 1'000'000) {
      if (!up) up = connect();
      if (up && !write_bool(client, ns, plan[next].tag, plan[next].value)) up = false;
      ++next;
    }
    std::unique_lock lk(m_);
    cv_.wait_for(lk, std::chrono::milliseconds(5), [&] { return fresh_ || !running_; });
  }
  if (client) {
    UA_Client_disconnect(client);
    UA_Client_delete(client);
  }
}

}  // namespace sunverify::app
