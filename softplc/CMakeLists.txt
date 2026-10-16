# softplc — stands in for the station PLC: open62541 1.4.21 server with the heartbeat watchdog
# (spec Sections 18, 19; I13). Leaves serverUrls at its default (docs/adr/0007).
add_executable(softplc src/softplc.c)
target_link_libraries(softplc PRIVATE open62541)
if(WIN32)
  target_link_libraries(softplc PRIVATE ws2_32 iphlpapi)
endif()
