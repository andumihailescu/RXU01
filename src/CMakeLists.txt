idf_component_register(
    SRC_DIRS
        "."
        "wifi_manager"
        "esp_now_driver"
        "remote_protocol"
        "esp_now_can_gateway"
        "rxu01_router"
        "mcp2515"
        "can_manager"

    INCLUDE_DIRS
        "."

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