#pragma once

#include <cstdint>

#include "esp_err.h"
#include "esp_wifi_types.h"

namespace wifi_manager
{
    struct Config
    {
        uint8_t channel = 1;
        wifi_storage_t storage = WIFI_STORAGE_RAM;
        wifi_ps_type_t power_save = WIFI_PS_NONE;
    };

    esp_err_t init(const Config &config);

    esp_err_t deinit();

    bool is_initialized();

    esp_err_t set_channel(uint8_t channel);

    uint8_t get_channel();

    esp_err_t get_station_mac(uint8_t mac[6]);
}
