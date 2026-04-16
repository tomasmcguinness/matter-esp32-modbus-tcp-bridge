idf_component_register(SRCS "main.cpp" "eth_connect.cpp" "modbus_tcp.c"
                       INCLUDE_DIRS "."
                       REQUIRES esp_eth esp_netif esp_event driver)
