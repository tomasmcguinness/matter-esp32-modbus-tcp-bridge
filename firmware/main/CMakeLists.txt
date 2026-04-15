idf_component_register(SRCS "main.c" "eth_connect.c" "modbus_tcp.c"
                    INCLUDE_DIRS "."
                    REQUIRES esp_eth esp_netif esp_event driver)
