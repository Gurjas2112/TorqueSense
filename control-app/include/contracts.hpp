//  To parse this JSON data, first install
//
//      json.hpp  https://github.com/nlohmann/json
//
//  Then include this file, and then do
//
//     StationState data = nlohmann::json::parse(jsonString);
//     Anomaly data = nlohmann::json::parse(jsonString);
//     Adjudication data = nlohmann::json::parse(jsonString);
//     StationHealth data = nlohmann::json::parse(jsonString);
//     HostHealth data = nlohmann::json::parse(jsonString);

#pragma once

#include <optional>
#include <nlohmann/json.hpp>

#ifndef NLOHMANN_OPT_HELPER
#define NLOHMANN_OPT_HELPER
namespace nlohmann {
    template <typename T>
    struct adl_serializer<std::shared_ptr<T>> {
        static void to_json(json & j, const std::shared_ptr<T> & opt) {
            if (!opt) j = nullptr; else j = *opt;
        }

        static std::shared_ptr<T> from_json(const json & j) {
            if (j.is_null()) return std::shared_ptr<T>(); else return std::make_shared<T>(j.get<T>());
        }
    };
    template <typename T>
    struct adl_serializer<std::optional<T>> {
        static void to_json(json & j, const std::optional<T> & opt) {
            if (!opt) j = nullptr; else j = *opt;
        }

        static std::optional<T> from_json(const json & j) {
            if (j.is_null()) return std::optional<T>(); else return std::make_optional<T>(j.get<T>());
        }
    };
}
#endif

namespace sunverify {
namespace contracts {
    using nlohmann::json;

    #ifndef NLOHMANN_UNTYPED_sunverify_contracts_HELPER
    #define NLOHMANN_UNTYPED_sunverify_contracts_HELPER
    inline json get_untyped(const json & j, const char * property) {
        if (j.find(property) != j.end()) {
            return j.at(property).get<json>();
        }
        return json();
    }

    inline json get_untyped(const json & j, std::string property) {
        return get_untyped(j, property.data());
    }
    #endif

    #ifndef NLOHMANN_OPTIONAL_sunverify_contracts_HELPER
    #define NLOHMANN_OPTIONAL_sunverify_contracts_HELPER
    template <typename T>
    inline std::shared_ptr<T> get_heap_optional(const json & j, const char * property) {
        auto it = j.find(property);
        if (it != j.end() && !it->is_null()) {
            return j.at(property).get<std::shared_ptr<T>>();
        }
        return std::shared_ptr<T>();
    }

    template <typename T>
    inline std::shared_ptr<T> get_heap_optional(const json & j, std::string property) {
        return get_heap_optional<T>(j, property.data());
    }
    template <typename T>
    inline std::optional<T> get_stack_optional(const json & j, const char * property) {
        auto it = j.find(property);
        if (it != j.end() && !it->is_null()) {
            return j.at(property).get<std::optional<T>>();
        }
        return std::optional<T>();
    }

    template <typename T>
    inline std::optional<T> get_stack_optional(const json & j, std::string property) {
        return get_stack_optional<T>(j, property.data());
    }
    #endif

    enum class Andon : int { AMBER, GREEN, RED };

    struct InspectionPointState {
        std::optional<bool> ambiguous;
        double conf;
        std::string family;
        std::string id;
        std::string state;
        /**
         * image column of the CAD-projected point, full-resolution pixels
         */
        double u;
        double v;
        /**
         * position in the assembly frame, for the part map
         */
        std::optional<double> x_mm;
        std::optional<double> y_mm;
    };

    struct Latency {
        double engine_ms_p50;
        double engine_ms_p99;
        double evidence_ms_p99;
        std::optional<double> last_engine_ms;
        std::optional<double> last_evidence_ms;
    };

    enum class StepKind : int { COMPLETE, SEQUENCE };

    enum class StepStatus : int { ACTIVE, COMPLETE, VIOLATED, WAITING };

    struct StepState {
        std::optional<int64_t> allotted_ms;
        int64_t done;
        std::optional<int64_t> elapsed_ms;
        std::string id;
        StepKind kind;
        std::string state;
        StepStatus status;
        std::optional<std::vector<std::string>> targets;
        int64_t total;
    };

    enum class VerdictKind : int { EVIDENCE_MISSING, INCOMPLETE_AT_RELEASE, PRECEDENCE, REGRESSION, RELEASED, SEQUENCE, STEP_TIMEOUT };

    struct Verdict {
        /**
         * the order still to do, for the corrective arc
         */
        std::optional<std::vector<std::string>> corrective;
        std::optional<std::string> expected_ip;
        VerdictKind kind;
        std::optional<std::string> narrative;
        std::optional<std::string> observed_ip;
        std::string step;
    };

    /**
     * Engine -> dashboard and HMI, about 15 Hz over the WebSocket on :8080. Built on the io
     * thread from the hot path's fixed-size StateVector; JSON never touches a pinned thread
     * (spec Section 30).
     */
    struct StationState {
        std::optional<bool> advisory_only;
        std::optional<Andon> andon;
        std::optional<std::string> cad_revision;
        std::optional<int64_t> cycle_elapsed_ms;
        std::string cycle_id;
        /**
         * why the engine runs on a fallback provider, or null
         */
        std::optional<std::string> degraded;
        int64_t frame_seq;
        std::vector<InspectionPointState> ips;
        Latency latency;
        std::optional<std::string> model_version;
        /**
         * the inspection points the operator should act on next
         */
        std::vector<std::string> next;
        std::string profile;
        std::optional<std::string> profile_version;
        /**
         * execution provider in use: tensorrt | cuda | replay
         */
        std::optional<std::string> provider;
        /**
         * e.g. W-14
         */
        std::string station_id;
        /**
         * id of the first incomplete step, or an empty string when every step is complete
         */
        std::string step;
        std::optional<std::vector<StepState>> steps;
        std::optional<double> takt_s;
        /**
         * engine wall clock, ms since the Unix epoch
         */
        std::optional<int64_t> ts_ms;
        std::optional<Verdict> verdict;
    };

    enum class ViolationType : int { EVIDENCE_MISSING, INCOMPLETE_AT_RELEASE, PRECEDENCE, REGRESSION, SEQUENCE, STEP_TIMEOUT };

    /**
     * Engine -> API, once per violation (spec Section 22). The API stores it in the outbox and
     * forwards it to public.anomalies; the columns here are that table's.
     */
    struct Anomaly {
        std::optional<bool> adjudicated;
        std::string cad_revision;
        std::optional<std::string> clip_key;
        std::string cycle_id;
        /**
         * violation-specific facts from the interpreter (missing, before, signal …)
         */
        std::optional<std::map<std::string, nlohmann::json>> detail;
        double engine_ms;
        /**
         * minted at the source: replays are idempotent
         */
        std::string event_id;
        double evidence_ms;
        std::optional<std::string> expected_ip;
        std::string model_version;
        std::string narrative;
        std::optional<std::string> observed_ip;
        std::string profile;
        std::string profile_version;
        /**
         * clip ring sequence at the violation; the sidecar cuts the clip around it
         */
        std::optional<int64_t> ring_end_seq;
        std::string station_id;
        std::string step_id;
        std::string ts;
        ViolationType violation_type;
    };

    struct Crop {
        int64_t h;
        int64_t w;
        int64_t x;
        int64_t y;
    };

    struct AdjudicationRequest {
        /**
         * the state the detector thinks it sees, with confidence in the ambiguous band
         */
        std::string candidate_state;
        std::optional<double> conf;
        Crop crop;
        std::string family;
        std::string ip;
        /**
         * what to ground, from the family library; defaults to the family's phrase for
         * candidate_state
         */
        std::optional<std::string> phrase;
        std::string request_id;
        /**
         * SharedClipRing sequence of the frame
         */
        int64_t ring_seq;
        std::string station_id;
    };

    struct AdjudicationResponse {
        std::optional<std::vector<std::vector<double>>> boxes;
        std::optional<bool> cached;
        /**
         * true if the grounded phrase sits on the inspection point
         */
        bool confirmed;
        std::optional<std::string> error;
        double latency_ms;
        std::optional<std::string> model;
        std::string request_id;
        std::optional<std::string> revision;
    };

    /**
     * Engine io thread <-> sidecar (:8001), POST /v1/adjudicate. The engine names a frame in
     * the SharedClipRing and an inspection point's crop; the sidecar grounds a phrase in it
     * with Florence-2. Never on the alert path (I3, I11): an answer can only resolve an
     * ambiguous state commit.
     */
    struct Adjudication {
        AdjudicationRequest request;
        std::optional<AdjudicationResponse> response;
    };

    /**
     * Engine io thread -> API, one bucket per station per bucket_s (300 s, 5 in demo mode).
     * Columns of public.station_health (spec Sections 16 and 19).
     */
    struct StationHealth {
        std::optional<int64_t> adjudications;
        double ambiguous_rate;
        std::optional<double> anchor_residual_px;
        int64_t bucket_s;
        std::string bucket_start;
        std::string cad_revision;
        /**
         * NULL for file, webcam and phone sources
         */
        std::optional<double> camera_temp_c;
        double dropped_frame_rate;
        std::optional<double> evidence_p99_ms;
        double fps;
        std::optional<double> ip_conf_mean;
        std::string model_version;
        /**
         * industrial GigE only (Appendix D.1)
         */
        std::optional<int64_t> packets_resent;
        std::string station_id;
    };

    enum class EngineStatus : int { DOWN, UP };

    struct Engine {
        std::optional<std::string> cad_revision;
        std::optional<double> evidence_p99_ms;
        std::optional<double> fps;
        std::optional<std::string> model_version;
        std::optional<std::string> reason;
        EngineStatus status;
        std::optional<double> uptime_s;
    };

    struct Gpu {
        double mem_used_mib;
        double sm_clock_max_mhz;
        double sm_clock_mhz;
        double sm_clock_ratio;
        double temp_c;
        double util_pct;
    };

    /**
     * Fleet agent -> API (POST /v1/health/host), every 10 s. The record fleet-agent/main.go
     * sends; the API flattens it into public.host_health.
     */
    struct HostHealth {
        int64_t disk_free_bytes;
        Engine engine;
        std::optional<Gpu> gpu;
        std::string node;
        std::string station_id;
        std::string ts;
    };
}
}

namespace sunverify {
namespace contracts {
    void from_json(const json & j, InspectionPointState & x);
    void to_json(json & j, const InspectionPointState & x);

    void from_json(const json & j, Latency & x);
    void to_json(json & j, const Latency & x);

    void from_json(const json & j, StepState & x);
    void to_json(json & j, const StepState & x);

    void from_json(const json & j, Verdict & x);
    void to_json(json & j, const Verdict & x);

    void from_json(const json & j, StationState & x);
    void to_json(json & j, const StationState & x);

    void from_json(const json & j, Anomaly & x);
    void to_json(json & j, const Anomaly & x);

    void from_json(const json & j, Crop & x);
    void to_json(json & j, const Crop & x);

    void from_json(const json & j, AdjudicationRequest & x);
    void to_json(json & j, const AdjudicationRequest & x);

    void from_json(const json & j, AdjudicationResponse & x);
    void to_json(json & j, const AdjudicationResponse & x);

    void from_json(const json & j, Adjudication & x);
    void to_json(json & j, const Adjudication & x);

    void from_json(const json & j, StationHealth & x);
    void to_json(json & j, const StationHealth & x);

    void from_json(const json & j, Engine & x);
    void to_json(json & j, const Engine & x);

    void from_json(const json & j, Gpu & x);
    void to_json(json & j, const Gpu & x);

    void from_json(const json & j, HostHealth & x);
    void to_json(json & j, const HostHealth & x);

    void from_json(const json & j, Andon & x);
    void to_json(json & j, const Andon & x);

    void from_json(const json & j, StepKind & x);
    void to_json(json & j, const StepKind & x);

    void from_json(const json & j, StepStatus & x);
    void to_json(json & j, const StepStatus & x);

    void from_json(const json & j, VerdictKind & x);
    void to_json(json & j, const VerdictKind & x);

    void from_json(const json & j, ViolationType & x);
    void to_json(json & j, const ViolationType & x);

    void from_json(const json & j, EngineStatus & x);
    void to_json(json & j, const EngineStatus & x);

    inline void from_json(const json & j, InspectionPointState& x) {
        x.ambiguous = get_stack_optional<bool>(j, "ambiguous");
        x.conf = j.at("conf").get<double>();
        x.family = j.at("family").get<std::string>();
        x.id = j.at("id").get<std::string>();
        x.state = j.at("state").get<std::string>();
        x.u = j.at("u").get<double>();
        x.v = j.at("v").get<double>();
        x.x_mm = get_stack_optional<double>(j, "x_mm");
        x.y_mm = get_stack_optional<double>(j, "y_mm");
    }

    inline void to_json(json & j, const InspectionPointState & x) {
        j = json::object();
        j["ambiguous"] = x.ambiguous;
        j["conf"] = x.conf;
        j["family"] = x.family;
        j["id"] = x.id;
        j["state"] = x.state;
        j["u"] = x.u;
        j["v"] = x.v;
        j["x_mm"] = x.x_mm;
        j["y_mm"] = x.y_mm;
    }

    inline void from_json(const json & j, Latency& x) {
        x.engine_ms_p50 = j.at("engine_ms_p50").get<double>();
        x.engine_ms_p99 = j.at("engine_ms_p99").get<double>();
        x.evidence_ms_p99 = j.at("evidence_ms_p99").get<double>();
        x.last_engine_ms = get_stack_optional<double>(j, "last_engine_ms");
        x.last_evidence_ms = get_stack_optional<double>(j, "last_evidence_ms");
    }

    inline void to_json(json & j, const Latency & x) {
        j = json::object();
        j["engine_ms_p50"] = x.engine_ms_p50;
        j["engine_ms_p99"] = x.engine_ms_p99;
        j["evidence_ms_p99"] = x.evidence_ms_p99;
        j["last_engine_ms"] = x.last_engine_ms;
        j["last_evidence_ms"] = x.last_evidence_ms;
    }

    inline void from_json(const json & j, StepState& x) {
        x.allotted_ms = get_stack_optional<int64_t>(j, "allotted_ms");
        x.done = j.at("done").get<int64_t>();
        x.elapsed_ms = get_stack_optional<int64_t>(j, "elapsed_ms");
        x.id = j.at("id").get<std::string>();
        x.kind = j.at("kind").get<StepKind>();
        x.state = j.at("state").get<std::string>();
        x.status = j.at("status").get<StepStatus>();
        x.targets = get_stack_optional<std::vector<std::string>>(j, "targets");
        x.total = j.at("total").get<int64_t>();
    }

    inline void to_json(json & j, const StepState & x) {
        j = json::object();
        j["allotted_ms"] = x.allotted_ms;
        j["done"] = x.done;
        j["elapsed_ms"] = x.elapsed_ms;
        j["id"] = x.id;
        j["kind"] = x.kind;
        j["state"] = x.state;
        j["status"] = x.status;
        j["targets"] = x.targets;
        j["total"] = x.total;
    }

    inline void from_json(const json & j, Verdict& x) {
        x.corrective = get_stack_optional<std::vector<std::string>>(j, "corrective");
        x.expected_ip = get_stack_optional<std::string>(j, "expected_ip");
        x.kind = j.at("kind").get<VerdictKind>();
        x.narrative = get_stack_optional<std::string>(j, "narrative");
        x.observed_ip = get_stack_optional<std::string>(j, "observed_ip");
        x.step = j.at("step").get<std::string>();
    }

    inline void to_json(json & j, const Verdict & x) {
        j = json::object();
        j["corrective"] = x.corrective;
        j["expected_ip"] = x.expected_ip;
        j["kind"] = x.kind;
        j["narrative"] = x.narrative;
        j["observed_ip"] = x.observed_ip;
        j["step"] = x.step;
    }

    inline void from_json(const json & j, StationState& x) {
        x.advisory_only = get_stack_optional<bool>(j, "advisory_only");
        x.andon = get_stack_optional<Andon>(j, "andon");
        x.cad_revision = get_stack_optional<std::string>(j, "cad_revision");
        x.cycle_elapsed_ms = get_stack_optional<int64_t>(j, "cycle_elapsed_ms");
        x.cycle_id = j.at("cycle_id").get<std::string>();
        x.degraded = get_stack_optional<std::string>(j, "degraded");
        x.frame_seq = j.at("frame_seq").get<int64_t>();
        x.ips = j.at("ips").get<std::vector<InspectionPointState>>();
        x.latency = j.at("latency").get<Latency>();
        x.model_version = get_stack_optional<std::string>(j, "model_version");
        x.next = j.at("next").get<std::vector<std::string>>();
        x.profile = j.at("profile").get<std::string>();
        x.profile_version = get_stack_optional<std::string>(j, "profile_version");
        x.provider = get_stack_optional<std::string>(j, "provider");
        x.station_id = j.at("station_id").get<std::string>();
        x.step = j.at("step").get<std::string>();
        x.steps = get_stack_optional<std::vector<StepState>>(j, "steps");
        x.takt_s = get_stack_optional<double>(j, "takt_s");
        x.ts_ms = get_stack_optional<int64_t>(j, "ts_ms");
        x.verdict = get_stack_optional<Verdict>(j, "verdict");
    }

    inline void to_json(json & j, const StationState & x) {
        j = json::object();
        j["advisory_only"] = x.advisory_only;
        j["andon"] = x.andon;
        j["cad_revision"] = x.cad_revision;
        j["cycle_elapsed_ms"] = x.cycle_elapsed_ms;
        j["cycle_id"] = x.cycle_id;
        j["degraded"] = x.degraded;
        j["frame_seq"] = x.frame_seq;
        j["ips"] = x.ips;
        j["latency"] = x.latency;
        j["model_version"] = x.model_version;
        j["next"] = x.next;
        j["profile"] = x.profile;
        j["profile_version"] = x.profile_version;
        j["provider"] = x.provider;
        j["station_id"] = x.station_id;
        j["step"] = x.step;
        j["steps"] = x.steps;
        j["takt_s"] = x.takt_s;
        j["ts_ms"] = x.ts_ms;
        j["verdict"] = x.verdict;
    }

    inline void from_json(const json & j, Anomaly& x) {
        x.adjudicated = get_stack_optional<bool>(j, "adjudicated");
        x.cad_revision = j.at("cad_revision").get<std::string>();
        x.clip_key = get_stack_optional<std::string>(j, "clip_key");
        x.cycle_id = j.at("cycle_id").get<std::string>();
        x.detail = get_stack_optional<std::map<std::string, nlohmann::json>>(j, "detail");
        x.engine_ms = j.at("engine_ms").get<double>();
        x.event_id = j.at("event_id").get<std::string>();
        x.evidence_ms = j.at("evidence_ms").get<double>();
        x.expected_ip = get_stack_optional<std::string>(j, "expected_ip");
        x.model_version = j.at("model_version").get<std::string>();
        x.narrative = j.at("narrative").get<std::string>();
        x.observed_ip = get_stack_optional<std::string>(j, "observed_ip");
        x.profile = j.at("profile").get<std::string>();
        x.profile_version = j.at("profile_version").get<std::string>();
        x.ring_end_seq = get_stack_optional<int64_t>(j, "ring_end_seq");
        x.station_id = j.at("station_id").get<std::string>();
        x.step_id = j.at("step_id").get<std::string>();
        x.ts = j.at("ts").get<std::string>();
        x.violation_type = j.at("violation_type").get<ViolationType>();
    }

    inline void to_json(json & j, const Anomaly & x) {
        j = json::object();
        j["adjudicated"] = x.adjudicated;
        j["cad_revision"] = x.cad_revision;
        j["clip_key"] = x.clip_key;
        j["cycle_id"] = x.cycle_id;
        j["detail"] = x.detail;
        j["engine_ms"] = x.engine_ms;
        j["event_id"] = x.event_id;
        j["evidence_ms"] = x.evidence_ms;
        j["expected_ip"] = x.expected_ip;
        j["model_version"] = x.model_version;
        j["narrative"] = x.narrative;
        j["observed_ip"] = x.observed_ip;
        j["profile"] = x.profile;
        j["profile_version"] = x.profile_version;
        j["ring_end_seq"] = x.ring_end_seq;
        j["station_id"] = x.station_id;
        j["step_id"] = x.step_id;
        j["ts"] = x.ts;
        j["violation_type"] = x.violation_type;
    }

    inline void from_json(const json & j, Crop& x) {
        x.h = j.at("h").get<int64_t>();
        x.w = j.at("w").get<int64_t>();
        x.x = j.at("x").get<int64_t>();
        x.y = j.at("y").get<int64_t>();
    }

    inline void to_json(json & j, const Crop & x) {
        j = json::object();
        j["h"] = x.h;
        j["w"] = x.w;
        j["x"] = x.x;
        j["y"] = x.y;
    }

    inline void from_json(const json & j, AdjudicationRequest& x) {
        x.candidate_state = j.at("candidate_state").get<std::string>();
        x.conf = get_stack_optional<double>(j, "conf");
        x.crop = j.at("crop").get<Crop>();
        x.family = j.at("family").get<std::string>();
        x.ip = j.at("ip").get<std::string>();
        x.phrase = get_stack_optional<std::string>(j, "phrase");
        x.request_id = j.at("request_id").get<std::string>();
        x.ring_seq = j.at("ring_seq").get<int64_t>();
        x.station_id = j.at("station_id").get<std::string>();
    }

    inline void to_json(json & j, const AdjudicationRequest & x) {
        j = json::object();
        j["candidate_state"] = x.candidate_state;
        j["conf"] = x.conf;
        j["crop"] = x.crop;
        j["family"] = x.family;
        j["ip"] = x.ip;
        j["phrase"] = x.phrase;
        j["request_id"] = x.request_id;
        j["ring_seq"] = x.ring_seq;
        j["station_id"] = x.station_id;
    }

    inline void from_json(const json & j, AdjudicationResponse& x) {
        x.boxes = get_stack_optional<std::vector<std::vector<double>>>(j, "boxes");
        x.cached = get_stack_optional<bool>(j, "cached");
        x.confirmed = j.at("confirmed").get<bool>();
        x.error = get_stack_optional<std::string>(j, "error");
        x.latency_ms = j.at("latency_ms").get<double>();
        x.model = get_stack_optional<std::string>(j, "model");
        x.request_id = j.at("request_id").get<std::string>();
        x.revision = get_stack_optional<std::string>(j, "revision");
    }

    inline void to_json(json & j, const AdjudicationResponse & x) {
        j = json::object();
        j["boxes"] = x.boxes;
        j["cached"] = x.cached;
        j["confirmed"] = x.confirmed;
        j["error"] = x.error;
        j["latency_ms"] = x.latency_ms;
        j["model"] = x.model;
        j["request_id"] = x.request_id;
        j["revision"] = x.revision;
    }

    inline void from_json(const json & j, Adjudication& x) {
        x.request = j.at("request").get<AdjudicationRequest>();
        x.response = get_stack_optional<AdjudicationResponse>(j, "response");
    }

    inline void to_json(json & j, const Adjudication & x) {
        j = json::object();
        j["request"] = x.request;
        j["response"] = x.response;
    }

    inline void from_json(const json & j, StationHealth& x) {
        x.adjudications = get_stack_optional<int64_t>(j, "adjudications");
        x.ambiguous_rate = j.at("ambiguous_rate").get<double>();
        x.anchor_residual_px = get_stack_optional<double>(j, "anchor_residual_px");
        x.bucket_s = j.at("bucket_s").get<int64_t>();
        x.bucket_start = j.at("bucket_start").get<std::string>();
        x.cad_revision = j.at("cad_revision").get<std::string>();
        x.camera_temp_c = get_stack_optional<double>(j, "camera_temp_c");
        x.dropped_frame_rate = j.at("dropped_frame_rate").get<double>();
        x.evidence_p99_ms = get_stack_optional<double>(j, "evidence_p99_ms");
        x.fps = j.at("fps").get<double>();
        x.ip_conf_mean = get_stack_optional<double>(j, "ip_conf_mean");
        x.model_version = j.at("model_version").get<std::string>();
        x.packets_resent = get_stack_optional<int64_t>(j, "packets_resent");
        x.station_id = j.at("station_id").get<std::string>();
    }

    inline void to_json(json & j, const StationHealth & x) {
        j = json::object();
        j["adjudications"] = x.adjudications;
        j["ambiguous_rate"] = x.ambiguous_rate;
        j["anchor_residual_px"] = x.anchor_residual_px;
        j["bucket_s"] = x.bucket_s;
        j["bucket_start"] = x.bucket_start;
        j["cad_revision"] = x.cad_revision;
        j["camera_temp_c"] = x.camera_temp_c;
        j["dropped_frame_rate"] = x.dropped_frame_rate;
        j["evidence_p99_ms"] = x.evidence_p99_ms;
        j["fps"] = x.fps;
        j["ip_conf_mean"] = x.ip_conf_mean;
        j["model_version"] = x.model_version;
        j["packets_resent"] = x.packets_resent;
        j["station_id"] = x.station_id;
    }

    inline void from_json(const json & j, Engine& x) {
        x.cad_revision = get_stack_optional<std::string>(j, "cad_revision");
        x.evidence_p99_ms = get_stack_optional<double>(j, "evidence_p99_ms");
        x.fps = get_stack_optional<double>(j, "fps");
        x.model_version = get_stack_optional<std::string>(j, "model_version");
        x.reason = get_stack_optional<std::string>(j, "reason");
        x.status = j.at("status").get<EngineStatus>();
        x.uptime_s = get_stack_optional<double>(j, "uptime_s");
    }

    inline void to_json(json & j, const Engine & x) {
        j = json::object();
        j["cad_revision"] = x.cad_revision;
        j["evidence_p99_ms"] = x.evidence_p99_ms;
        j["fps"] = x.fps;
        j["model_version"] = x.model_version;
        j["reason"] = x.reason;
        j["status"] = x.status;
        j["uptime_s"] = x.uptime_s;
    }

    inline void from_json(const json & j, Gpu& x) {
        x.mem_used_mib = j.at("mem_used_mib").get<double>();
        x.sm_clock_max_mhz = j.at("sm_clock_max_mhz").get<double>();
        x.sm_clock_mhz = j.at("sm_clock_mhz").get<double>();
        x.sm_clock_ratio = j.at("sm_clock_ratio").get<double>();
        x.temp_c = j.at("temp_c").get<double>();
        x.util_pct = j.at("util_pct").get<double>();
    }

    inline void to_json(json & j, const Gpu & x) {
        j = json::object();
        j["mem_used_mib"] = x.mem_used_mib;
        j["sm_clock_max_mhz"] = x.sm_clock_max_mhz;
        j["sm_clock_mhz"] = x.sm_clock_mhz;
        j["sm_clock_ratio"] = x.sm_clock_ratio;
        j["temp_c"] = x.temp_c;
        j["util_pct"] = x.util_pct;
    }

    inline void from_json(const json & j, HostHealth& x) {
        x.disk_free_bytes = j.at("disk_free_bytes").get<int64_t>();
        x.engine = j.at("engine").get<Engine>();
        x.gpu = get_stack_optional<Gpu>(j, "gpu");
        x.node = j.at("node").get<std::string>();
        x.station_id = j.at("station_id").get<std::string>();
        x.ts = j.at("ts").get<std::string>();
    }

    inline void to_json(json & j, const HostHealth & x) {
        j = json::object();
        j["disk_free_bytes"] = x.disk_free_bytes;
        j["engine"] = x.engine;
        j["gpu"] = x.gpu;
        j["node"] = x.node;
        j["station_id"] = x.station_id;
        j["ts"] = x.ts;
    }

    inline void from_json(const json & j, Andon & x) {
        if (j == "AMBER") x = Andon::AMBER;
        else if (j == "GREEN") x = Andon::GREEN;
        else if (j == "RED") x = Andon::RED;
        else { throw std::runtime_error("Cannot deserialize to enumeration \"Andon\""); }
    }

    inline void to_json(json & j, const Andon & x) {
        switch (x) {
            case Andon::AMBER: j = "AMBER"; break;
            case Andon::GREEN: j = "GREEN"; break;
            case Andon::RED: j = "RED"; break;
            default: throw std::runtime_error("Unexpected value in enumeration \"Andon\": " + std::to_string(static_cast<int>(x)));
        }
    }

    inline void from_json(const json & j, StepKind & x) {
        if (j == "complete") x = StepKind::COMPLETE;
        else if (j == "sequence") x = StepKind::SEQUENCE;
        else { throw std::runtime_error("Cannot deserialize to enumeration \"StepKind\""); }
    }

    inline void to_json(json & j, const StepKind & x) {
        switch (x) {
            case StepKind::COMPLETE: j = "complete"; break;
            case StepKind::SEQUENCE: j = "sequence"; break;
            default: throw std::runtime_error("Unexpected value in enumeration \"StepKind\": " + std::to_string(static_cast<int>(x)));
        }
    }

    inline void from_json(const json & j, StepStatus & x) {
        if (j == "active") x = StepStatus::ACTIVE;
        else if (j == "complete") x = StepStatus::COMPLETE;
        else if (j == "violated") x = StepStatus::VIOLATED;
        else if (j == "waiting") x = StepStatus::WAITING;
        else { throw std::runtime_error("Cannot deserialize to enumeration \"StepStatus\""); }
    }

    inline void to_json(json & j, const StepStatus & x) {
        switch (x) {
            case StepStatus::ACTIVE: j = "active"; break;
            case StepStatus::COMPLETE: j = "complete"; break;
            case StepStatus::VIOLATED: j = "violated"; break;
            case StepStatus::WAITING: j = "waiting"; break;
            default: throw std::runtime_error("Unexpected value in enumeration \"StepStatus\": " + std::to_string(static_cast<int>(x)));
        }
    }

    inline void from_json(const json & j, VerdictKind & x) {
        if (j == "EVIDENCE_MISSING") x = VerdictKind::EVIDENCE_MISSING;
        else if (j == "INCOMPLETE_AT_RELEASE") x = VerdictKind::INCOMPLETE_AT_RELEASE;
        else if (j == "PRECEDENCE") x = VerdictKind::PRECEDENCE;
        else if (j == "REGRESSION") x = VerdictKind::REGRESSION;
        else if (j == "RELEASED") x = VerdictKind::RELEASED;
        else if (j == "SEQUENCE") x = VerdictKind::SEQUENCE;
        else if (j == "STEP_TIMEOUT") x = VerdictKind::STEP_TIMEOUT;
        else { throw std::runtime_error("Cannot deserialize to enumeration \"VerdictKind\""); }
    }

    inline void to_json(json & j, const VerdictKind & x) {
        switch (x) {
            case VerdictKind::EVIDENCE_MISSING: j = "EVIDENCE_MISSING"; break;
            case VerdictKind::INCOMPLETE_AT_RELEASE: j = "INCOMPLETE_AT_RELEASE"; break;
            case VerdictKind::PRECEDENCE: j = "PRECEDENCE"; break;
            case VerdictKind::REGRESSION: j = "REGRESSION"; break;
            case VerdictKind::RELEASED: j = "RELEASED"; break;
            case VerdictKind::SEQUENCE: j = "SEQUENCE"; break;
            case VerdictKind::STEP_TIMEOUT: j = "STEP_TIMEOUT"; break;
            default: throw std::runtime_error("Unexpected value in enumeration \"VerdictKind\": " + std::to_string(static_cast<int>(x)));
        }
    }

    inline void from_json(const json & j, ViolationType & x) {
        if (j == "EVIDENCE_MISSING") x = ViolationType::EVIDENCE_MISSING;
        else if (j == "INCOMPLETE_AT_RELEASE") x = ViolationType::INCOMPLETE_AT_RELEASE;
        else if (j == "PRECEDENCE") x = ViolationType::PRECEDENCE;
        else if (j == "REGRESSION") x = ViolationType::REGRESSION;
        else if (j == "SEQUENCE") x = ViolationType::SEQUENCE;
        else if (j == "STEP_TIMEOUT") x = ViolationType::STEP_TIMEOUT;
        else { throw std::runtime_error("Cannot deserialize to enumeration \"ViolationType\""); }
    }

    inline void to_json(json & j, const ViolationType & x) {
        switch (x) {
            case ViolationType::EVIDENCE_MISSING: j = "EVIDENCE_MISSING"; break;
            case ViolationType::INCOMPLETE_AT_RELEASE: j = "INCOMPLETE_AT_RELEASE"; break;
            case ViolationType::PRECEDENCE: j = "PRECEDENCE"; break;
            case ViolationType::REGRESSION: j = "REGRESSION"; break;
            case ViolationType::SEQUENCE: j = "SEQUENCE"; break;
            case ViolationType::STEP_TIMEOUT: j = "STEP_TIMEOUT"; break;
            default: throw std::runtime_error("Unexpected value in enumeration \"ViolationType\": " + std::to_string(static_cast<int>(x)));
        }
    }

    inline void from_json(const json & j, EngineStatus & x) {
        if (j == "down") x = EngineStatus::DOWN;
        else if (j == "up") x = EngineStatus::UP;
        else { throw std::runtime_error("Cannot deserialize to enumeration \"EngineStatus\""); }
    }

    inline void to_json(json & j, const EngineStatus & x) {
        switch (x) {
            case EngineStatus::DOWN: j = "down"; break;
            case EngineStatus::UP: j = "up"; break;
            default: throw std::runtime_error("Unexpected value in enumeration \"EngineStatus\": " + std::to_string(static_cast<int>(x)));
        }
    }
}
}
