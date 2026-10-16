/* softplc: stands in for the station PLC. Publishes the Andon and conveyor tags and owns
 * the fail-visible rule (I13): if the engine's heartbeat counter stops changing for 2 s,
 * the station goes AMBER and InspectionOnline goes false. The PLC decides this, not the
 * engine, because a dead engine cannot report that it is dead. */
#include "open62541.h"
#include <signal.h>
#include <stdlib.h>
#include <string.h>

#define STALE_MS 2000
static volatile UA_Boolean running = true;
static void stop(int s) { (void)s; running = false; }

static UA_UInt16 ns;
static UA_UInt32 last_beat = 0;
static UA_DateTime last_change = 0;        /* 0 = never seen: inspection not ready */

static void add_var(UA_Server *s, const char *id, const UA_DataType *t, void *init) {
    UA_VariableAttributes a = UA_VariableAttributes_default;
    UA_Variant_setScalar(&a.value, init, t);
    a.dataType = t->typeId;
    a.accessLevel = UA_ACCESSLEVELMASK_READ | UA_ACCESSLEVELMASK_WRITE;
    a.displayName = UA_LOCALIZEDTEXT("en-US", (char *)id);
    UA_Server_addVariableNode(s, UA_NODEID_STRING(ns, (char *)id),
        UA_NODEID_NUMERIC(0, UA_NS0ID_OBJECTSFOLDER), UA_NODEID_NUMERIC(0, UA_NS0ID_ORGANIZES),
        UA_QUALIFIEDNAME(ns, (char *)id), UA_NODEID_NUMERIC(0, UA_NS0ID_BASEDATAVARIABLETYPE),
        a, NULL, NULL);
}

static void set_bool(UA_Server *s, const char *id, UA_Boolean v) {
    UA_Variant x; UA_Variant_setScalar(&x, &v, &UA_TYPES[UA_TYPES_BOOLEAN]);
    UA_Server_writeValue(s, UA_NODEID_STRING(ns, (char *)id), x);
}

/* Every 250 ms: has the heartbeat moved? */
static void watchdog(UA_Server *s, void *data) {
    (void)data;
    UA_Variant v; UA_Variant_init(&v);
    if (UA_Server_readValue(s, UA_NODEID_STRING(ns, "Station.W14.Heartbeat"), &v) != UA_STATUSCODE_GOOD)
        return;
    UA_UInt32 beat = *(UA_UInt32 *)v.data;
    UA_Variant_clear(&v);
    UA_DateTime now = UA_DateTime_nowMonotonic();
    if (beat != last_beat) { last_beat = beat; last_change = now; }
    UA_Boolean online = last_change != 0 && (now - last_change) < STALE_MS * UA_DATETIME_MSEC;
    set_bool(s, "Station.W14.InspectionOnline", online);
    set_bool(s, "Station.W14.Andon.Amber", !online);
}

int main(int argc, char **argv) {
    signal(SIGINT, stop); signal(SIGTERM, stop);
    UA_UInt16 port = argc > 1 ? (UA_UInt16)atoi(argv[1]) : 4840;
    UA_Server *server = UA_Server_new();
    UA_ServerConfig *cfg = UA_Server_getConfig(server);
    UA_ServerConfig_setMinimal(cfg, port, NULL);   /* listen on all interfaces; see notes */
    ns = UA_Server_addNamespace(server, "urn:torquesense:station");

    UA_Boolean f = false; UA_UInt32 zero = 0;
    const char *bools[] = {"Station.W14.Andon.Red", "Station.W14.Andon.Amber", "Station.W14.Andon.Green",
                           "Station.W14.Conveyor.HoldReq", "Station.W14.InspectionOnline"};
    for (size_t i = 0; i < 5; ++i) add_var(server, bools[i], &UA_TYPES[UA_TYPES_BOOLEAN], &f);
    add_var(server, "Station.W14.Heartbeat", &UA_TYPES[UA_TYPES_UINT32], &zero);
    set_bool(server, "Station.W14.Andon.Amber", true);   /* not ready until the first beat */

    UA_Server_addRepeatedCallback(server, watchdog, NULL, 250, NULL);
    UA_StatusCode rc = UA_Server_runUntilInterrupt(server);
    UA_Server_delete(server);
    return rc == UA_STATUSCODE_GOOD ? 0 : 1;
}
