idf_component_register(SRCS "main.cpp" "matter_ethernet_driver.cpp" "modbus_tcp.c" "web_server.cpp"
                       INCLUDE_DIRS "."
                       REQUIRES esp_eth esp_netif esp_event esp_http_server driver mdns espressif__esp_matter
                       EMBED_FILES "${CMAKE_SOURCE_DIR}/html_app/index.html")

idf_component_get_property(matter_lib espressif__esp_matter COMPONENT_LIB)
set_source_files_properties(
    "${CMAKE_SOURCE_DIR}/managed_components/espressif__esp_matter/connectedhomeip/connectedhomeip/src/platform/ESP32/NetworkCommissioningDriver_Ethernet.cpp"
    TARGET_DIRECTORY ${matter_lib}
    PROPERTIES HEADER_FILE_ONLY TRUE
)

# esp-matter's CMakeLists excludes ESP32DnssdImpl.cpp when Wi-Fi is disabled, but
# DnssdImpl.cpp (included when CONFIG_USE_MINIMAL_MDNS=n) depends on its symbols.
# Add it back so platform mDNS links on Ethernet-only builds.
target_sources(${matter_lib} PRIVATE
    "${CMAKE_SOURCE_DIR}/managed_components/espressif__esp_matter/connectedhomeip/connectedhomeip/src/platform/ESP32/ESP32DnssdImpl.cpp"
)
