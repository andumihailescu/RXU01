idf_component_register(
    SRCS
        "main.cpp"
        "application/application.cpp"
        "wifi_manager/wifi_manager.cpp"
        "esp_now_driver/esp_now_driver.cpp"
        "remote_protocol/remote_protocol.cpp"
        "esp_now_can_gateway/esp_now_can_gateway.cpp"
        "mcp2515/mcp2515.cpp"
        "can_manager/can_manager.cpp"

    INCLUDE_DIRS
        "../include"

    REQUIRES
        esp_event
        esp_netif
        esp_wifi
        nvs_flash
        freertos
        esp_common
        esp_driver_gpio
        esp_driver_spi
)
