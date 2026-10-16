/* softplc: stands in for the station PLC (spec Sections 18, 19). Publishes each station's Andon,
 * conveyor and cycle tags, and owns the fail-visible rule (I13): if a station's heartbeat counter
 * stops changing for 2 s, that station goes AMBER and InspectionOnline goes false. The PLC decides
 * this, not the engine, because a dead engine cannot report that it is dead.
 *
 *   softplc [port] [--station W14]... [--bool <full tag>]... [--string <full tag>]...
 *
 * Defaults: port 4840, one station W14. Each --station adds an independent watchdog (four-station
 * mode). --bool and --string publish extra plant tags a profile's evidence names, e.g.
 * --bool Station.W14.Nutrunner.ResultOK. Extended from softplc_reference.c, which was tested against
 * open62541 1.4.21; the server configuration is unchanged: UA_ServerConfig_setMinimal with no
 * serverUrls, so it binds every interface and echoes each client's own URL (docs/adr/0007). */
#include "open62541.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define STALE_MS 2000
#define MAX_STATIONS 8
#define MAX_TAGS 64
#define NAME_LEN 160

static UA_UInt16 ns;

typedef struct {
    char id[32];                    /* e.g. W14 */
    UA_UInt32 last_beat;
    UA_DateTime last_change;        /* 0 = never seen: inspection not ready */
} Station;

static Station stations[MAX_STATIONS];
static size_t n_stations = 0;

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

static void tag_name(char *out, const Station *st, const char *leaf) {
    snprintf(out, NAME_LEN, "Station.%s.%s", st->id, leaf);
}

/* Every 250 ms, per station: has the heartbeat moved? */
static void watchdog(UA_Server *s, void *data) {
    Station *st = (Station *)data;
    char hb[NAME_LEN], online_tag[NAME_LEN], amber[NAME_LEN];
    tag_name(hb, st, "Heartbeat");
    tag_name(online_tag, st, "InspectionOnline");
    tag_name(amber, st, "Andon.Amber");
    UA_Variant v; UA_Variant_init(&v);
    if (UA_Server_readValue(s, UA_NODEID_STRING(ns, hb), &v) != UA_STATUSCODE_GOOD)
        return;
    UA_UInt32 beat = *(UA_UInt32 *)v.data;
    UA_Variant_clear(&v);
    UA_DateTime now = UA_DateTime_nowMonotonic();
    if (beat != st->last_beat) { st->last_beat = beat; st->last_change = now; }
    UA_Boolean online = st->last_change != 0 && (now - st->last_change) < STALE_MS * UA_DATETIME_MSEC;
    set_bool(s, online_tag, online);
    set_bool(s, amber, !online);
}

static void add_station(UA_Server *server, Station *st) {
    static const char *bools[] = {"Andon.Red", "Andon.Amber", "Andon.Green", "Conveyor.HoldReq",
                                  "InspectionOnline", "ReleaseRequest"};
    UA_Boolean f = false; UA_UInt32 zero = 0; UA_Int32 izero = 0;
    UA_String empty = UA_STRING_NULL;
    char name[NAME_LEN];
    for (size_t i = 0; i < sizeof bools / sizeof bools[0]; ++i) {
        tag_name(name, st, bools[i]);
        add_var(server, name, &UA_TYPES[UA_TYPES_BOOLEAN], &f);
    }
    tag_name(name, st, "Heartbeat");
    add_var(server, name, &UA_TYPES[UA_TYPES_UINT32], &zero);
    tag_name(name, st, "CycleState");
    add_var(server, name, &UA_TYPES[UA_TYPES_INT32], &izero);
    tag_name(name, st, "LastViolation");
    add_var(server, name, &UA_TYPES[UA_TYPES_STRING], &empty);
    tag_name(name, st, "VariantCode");
    add_var(server, name, &UA_TYPES[UA_TYPES_STRING], &empty);
    tag_name(name, st, "Andon.Amber");
    set_bool(server, name, true);                          /* not ready until the first beat */
    UA_Server_addRepeatedCallback(server, watchdog, st, 250, NULL);
}

int main(int argc, char **argv) {
    UA_UInt16 port = 4840;
    const char *bool_tags[MAX_TAGS], *string_tags[MAX_TAGS];
    size_t n_bool = 0, n_string = 0;
    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--station") && i + 1 < argc) {
            if (n_stations < MAX_STATIONS) {
                memset(&stations[n_stations], 0, sizeof(Station));
                snprintf(stations[n_stations].id, sizeof stations[n_stations].id, "%s", argv[++i]);
                ++n_stations;
            } else { ++i; }
        } else if (!strcmp(argv[i], "--bool") && i + 1 < argc) {
            if (n_bool < MAX_TAGS) bool_tags[n_bool++] = argv[++i]; else ++i;
        } else if (!strcmp(argv[i], "--string") && i + 1 < argc) {
            if (n_string < MAX_TAGS) string_tags[n_string++] = argv[++i]; else ++i;
        } else if (argv[i][0] != '-') {
            port = (UA_UInt16)atoi(argv[i]);
        } else {
            fprintf(stderr, "usage: softplc [port] [--station W14]... [--bool TAG]... [--string TAG]...\n");
            return 2;
        }
    }
    if (n_stations == 0) { snprintf(stations[0].id, sizeof stations[0].id, "W14"); n_stations = 1; }

    UA_Server *server = UA_Server_new();
    UA_ServerConfig *cfg = UA_Server_getConfig(server);
    UA_ServerConfig_setMinimal(cfg, port, NULL);   /* listen on all interfaces; never set serverUrls */
    ns = UA_Server_addNamespace(server, "urn:torquesense:station");

    for (size_t i = 0; i < n_stations; ++i) add_station(server, &stations[i]);
    UA_Boolean f = false; UA_String empty = UA_STRING_NULL;
    for (size_t i = 0; i < n_bool; ++i) add_var(server, bool_tags[i], &UA_TYPES[UA_TYPES_BOOLEAN], &f);
    for (size_t i = 0; i < n_string; ++i) add_var(server, string_tags[i], &UA_TYPES[UA_TYPES_STRING], &empty);

    UA_StatusCode rc = UA_Server_runUntilInterrupt(server);   /* handles SIGINT and SIGTERM itself */
    UA_Server_delete(server);
    return rc == UA_STATUSCODE_GOOD ? 0 : 1;
}
