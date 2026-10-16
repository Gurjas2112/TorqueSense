#include "sunverify/procedure/OpcUaAlerter.hpp"

#include <cstring>

#include "open62541.h"

namespace sunverify::procedure {

struct OpcUaAlerter::Impl {
  OpcUaConfig cfg;
  UA_Client* client = nullptr;
  UA_UInt16 ns = 2;
  bool up = false;
  AndonState last{};
  bool have_last = false;
  // node names built once at construction; the alert loop never formats strings
  std::string red, green, hold, cycle, violation, beat, release;

  UA_NodeId node(const std::string& name) const { return UA_NODEID_STRING(ns, const_cast<char*>(name.c_str())); }

  bool write(const std::string& name, void* v, const UA_DataType* t) {
    UA_Variant var;
    UA_Variant_setScalar(&var, v, t);
    const UA_StatusCode rc = UA_Client_writeValueAttribute(client, node(name), &var);
    if (rc == UA_STATUSCODE_BADCONNECTIONCLOSED || rc == UA_STATUSCODE_BADSERVERNOTCONNECTED ||
        rc == UA_STATUSCODE_BADCOMMUNICATIONERROR || rc == UA_STATUSCODE_BADTIMEOUT)
      up = false;
    return rc == UA_STATUSCODE_GOOD;
  }
};

OpcUaAlerter::OpcUaAlerter(OpcUaConfig cfg) : impl_(std::make_unique<Impl>()) {
  impl_->cfg = std::move(cfg);
  const std::string& p = impl_->cfg.prefix;
  impl_->red = p + ".Andon.Red";
  impl_->green = p + ".Andon.Green";
  impl_->hold = p + ".Conveyor.HoldReq";
  impl_->cycle = p + ".CycleState";
  impl_->violation = p + ".LastViolation";
  impl_->beat = p + ".Heartbeat";
  impl_->release = p + ".ReleaseRequest";
}

OpcUaAlerter::~OpcUaAlerter() {
  if (impl_->client) {
    UA_Client_disconnect(impl_->client);
    UA_Client_delete(impl_->client);
  }
}

bool OpcUaAlerter::connect() noexcept {
  Impl& m = *impl_;
  if (m.client) {
    UA_Client_disconnect(m.client);
    UA_Client_delete(m.client);
  }
  m.client = UA_Client_new();
  UA_ClientConfig* cc = UA_Client_getConfig(m.client);
  UA_ClientConfig_setDefault(cc);
  cc->timeout = static_cast<UA_UInt32>(m.cfg.timeout_ms);
  cc->logging->log = nullptr;   // quiet: the engine logs connection state itself
  m.up = UA_Client_connect(m.client, m.cfg.endpoint.c_str()) == UA_STATUSCODE_GOOD;
  if (m.up) {
    UA_String uri = UA_STRING(const_cast<char*>(m.cfg.namespace_uri.c_str()));
    UA_UInt16 idx = 0;
    if (UA_Client_NamespaceGetIndex(m.client, &uri, &idx) == UA_STATUSCODE_GOOD) m.ns = idx;
  }
  m.have_last = false;
  return m.up;
}

bool OpcUaAlerter::connected() const noexcept { return impl_->up; }

bool OpcUaAlerter::apply(const AndonState& s) noexcept {
  Impl& m = *impl_;
  if (!m.up) return false;
  bool ok = true;
  auto changed = [&](auto a, auto b) { return !m.have_last || a != b; };
  if (changed(s.red, m.last.red)) { UA_Boolean v = s.red; ok &= m.write(m.red, &v, &UA_TYPES[UA_TYPES_BOOLEAN]); }
  if (changed(s.green, m.last.green)) { UA_Boolean v = s.green; ok &= m.write(m.green, &v, &UA_TYPES[UA_TYPES_BOOLEAN]); }
  if (changed(s.hold_request, m.last.hold_request)) {
    UA_Boolean v = s.hold_request;
    ok &= m.write(m.hold, &v, &UA_TYPES[UA_TYPES_BOOLEAN]);
  }
  if (changed(s.cycle_state, m.last.cycle_state)) {
    UA_Int32 v = s.cycle_state;
    ok &= m.write(m.cycle, &v, &UA_TYPES[UA_TYPES_INT32]);
  }
  if (!m.have_last || std::strncmp(s.last_violation.data(), m.last.last_violation.data(), s.last_violation.size()) != 0) {
    UA_String v{std::strlen(s.last_violation.data()), reinterpret_cast<UA_Byte*>(const_cast<char*>(s.last_violation.data()))};
    ok &= m.write(m.violation, &v, &UA_TYPES[UA_TYPES_STRING]);
  }
  if (ok) {
    m.last = s;
    m.have_last = true;
  }
  return ok;
}

bool OpcUaAlerter::heartbeat(std::uint32_t n) noexcept {
  if (!impl_->up) return false;
  UA_UInt32 v = n;
  return impl_->write(impl_->beat, &v, &UA_TYPES[UA_TYPES_UINT32]);
}

bool OpcUaAlerter::poll(std::span<PlantRead> out) noexcept {
  Impl& m = *impl_;
  if (!m.up) return false;
  for (std::size_t i = 0; i < out.size(); ++i) {
    PlantRead& r = out[i];
    const std::string& name = i == 0 ? m.release : m.cfg.evidence_tags[i - 1];
    r.is_release = i == 0;
    r.tag = i == 0 ? -1 : static_cast<std::int32_t>(i - 1);
    UA_Variant v;
    UA_Variant_init(&v);
    const UA_StatusCode rc = UA_Client_readValueAttribute(m.client, m.node(name), &v);
    r.valid = rc == UA_STATUSCODE_GOOD && UA_Variant_isScalar(&v);
    if (r.valid) {
      if (v.type == &UA_TYPES[UA_TYPES_BOOLEAN]) r.number = *static_cast<UA_Boolean*>(v.data) ? 1.0 : 0.0;
      else if (v.type == &UA_TYPES[UA_TYPES_INT32]) r.number = *static_cast<UA_Int32*>(v.data);
      else if (v.type == &UA_TYPES[UA_TYPES_UINT32]) r.number = *static_cast<UA_UInt32*>(v.data);
      else if (v.type == &UA_TYPES[UA_TYPES_DOUBLE]) r.number = *static_cast<UA_Double*>(v.data);
      else r.valid = false;
    }
    UA_Variant_clear(&v);
    if (rc == UA_STATUSCODE_BADCONNECTIONCLOSED || rc == UA_STATUSCODE_BADSERVERNOTCONNECTED) {
      m.up = false;
      return false;
    }
  }
  return true;
}

}  // namespace sunverify::procedure
