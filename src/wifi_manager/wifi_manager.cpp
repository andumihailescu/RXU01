#include "wifi_manager/wifi_manager.h"

#include <algorithm>

#include "esp_event.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "nvs_flash.h"

namespace
{
    bool g_initialized = false;
    wifi_manager::Config g_config{};

    bool isValidChannel(uint8_t channel)
    {
        // Canalele Wi-Fi uzuale permise in regiunea europeana.
        return channel >= 1 && channel <= 13;
    }

    bool isValidUnicastMac(
        const uint8_t mac[wifi_manager::MAC_ADDRESS_SIZE])
    {
        const bool is_zero = std::all_of(
            mac,
            mac + wifi_manager::MAC_ADDRESS_SIZE,
            [](uint8_t byte) { return byte == 0; });

        const bool is_multicast = (mac[0] & 0x01U) != 0;

        return !is_zero && !is_multicast;
    }

    esp_err_t initNvs()
    {
        esp_err_t result = nvs_flash_init();

        if (result == ESP_ERR_NVS_NO_FREE_PAGES ||
            result == ESP_ERR_NVS_NEW_VERSION_FOUND)
        {
            result = nvs_flash_erase();

            if (result != ESP_OK)
            {
                return result;
            }

            result = nvs_flash_init();
        }

        return result;
    }

    void cleanupAfterFailedInit(bool wifi_started)
    {
        if (wifi_started)
        {
            esp_wifi_stop();
        }

        esp_wifi_deinit();
    }
}

namespace wifi_manager
{
    esp_err_t init(const Config &config)
    {
        if (g_initialized)
        {
            return ESP_OK;
        }

        if (!isValidChannel(config.channel) ||
            (config.use_custom_station_mac &&
             !isValidUnicastMac(config.station_mac)))
        {
            return ESP_ERR_INVALID_ARG;
        }

        esp_err_t result = initNvs();

        if (result != ESP_OK)
        {
            return result;
        }

        result = esp_netif_init();

        if (result != ESP_OK &&
            result != ESP_ERR_INVALID_STATE)
        {
            return result;
        }

        result = esp_event_loop_create_default();

        if (result != ESP_OK &&
            result != ESP_ERR_INVALID_STATE)
        {
            return result;
        }

        wifi_init_config_t wifi_config =
            WIFI_INIT_CONFIG_DEFAULT();

        result = esp_wifi_init(&wifi_config);

        if (result != ESP_OK)
        {
            return result;
        }

        bool wifi_started = false;

        result = esp_wifi_set_storage(config.storage);

        if (result != ESP_OK)
        {
            cleanupAfterFailedInit(wifi_started);
            return result;
        }

        result = esp_wifi_set_mode(WIFI_MODE_STA);

        if (result != ESP_OK)
        {
            cleanupAfterFailedInit(wifi_started);
            return result;
        }

        if (config.use_custom_station_mac)
        {
            result = esp_wifi_set_mac(
                WIFI_IF_STA,
                config.station_mac);

            if (result != ESP_OK)
            {
                cleanupAfterFailedInit(wifi_started);
                return result;
            }
        }

        result = esp_wifi_start();

        if (result != ESP_OK)
        {
            cleanupAfterFailedInit(wifi_started);
            return result;
        }

        wifi_started = true;

        result = esp_wifi_set_channel(
            config.channel,
            WIFI_SECOND_CHAN_NONE);

        if (result != ESP_OK)
        {
            cleanupAfterFailedInit(wifi_started);
            return result;
        }

        result = esp_wifi_set_ps(config.power_save);

        if (result != ESP_OK)
        {
            cleanupAfterFailedInit(wifi_started);
            return result;
        }

        g_config = config;
        g_initialized = true;

        return ESP_OK;
    }

    esp_err_t deinit()
    {
        if (!g_initialized)
        {
            return ESP_OK;
        }

        esp_err_t first_error = ESP_OK;

        const esp_err_t stop_result =
            esp_wifi_stop();

        if (stop_result != ESP_OK)
        {
            first_error = stop_result;
        }

        const esp_err_t deinit_result =
            esp_wifi_deinit();

        if (first_error == ESP_OK &&
            deinit_result != ESP_OK)
        {
            first_error = deinit_result;
        }

        g_initialized = false;
        g_config = {};

        return first_error;
    }

    bool is_initialized()
    {
        return g_initialized;
    }

    esp_err_t set_channel(uint8_t channel)
    {
        if (!g_initialized)
        {
            return ESP_ERR_INVALID_STATE;
        }

        if (!isValidChannel(channel))
        {
            return ESP_ERR_INVALID_ARG;
        }

        const esp_err_t result =
            esp_wifi_set_channel(
                channel,
                WIFI_SECOND_CHAN_NONE);

        if (result == ESP_OK)
        {
            g_config.channel = channel;
        }

        return result;
    }

    uint8_t get_channel()
    {
        return g_initialized
                   ? g_config.channel
                   : 0;
    }

    esp_err_t get_station_mac(uint8_t mac[6])
    {
        if (!g_initialized)
        {
            return ESP_ERR_INVALID_STATE;
        }

        if (mac == nullptr)
        {
            return ESP_ERR_INVALID_ARG;
        }

        return esp_wifi_get_mac(
            WIFI_IF_STA,
            mac);
    }
}
