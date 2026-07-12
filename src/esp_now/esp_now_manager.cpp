#include "esp_now/esp_now_manager.h"

#include <cstring>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "nvs_flash.h"

namespace
{
    constexpr char TAG[] = "EspNowManager";

    esp_err_t initNvs()
    {
        esp_err_t err = nvs_flash_init();

        if (err == ESP_ERR_NVS_NO_FREE_PAGES ||
            err == ESP_ERR_NVS_NEW_VERSION_FOUND)
        {
            err = nvs_flash_erase();
            if (err != ESP_OK)
            {
                return err;
            }

            err = nvs_flash_init();
        }

        return err;
    }
} // namespace

EspNowManager *EspNowManager::instance_ = nullptr;

esp_err_t EspNowManager::begin(uint8_t channel)
{
    esp_err_t err = initNvs();
    if (err != ESP_OK)
    {
        return err;
    }

    if ((err = esp_netif_init()) != ESP_OK)
    {
        return err;
    }

    if ((err = esp_event_loop_create_default()) != ESP_OK)
    {
        return err;
    }

    wifi_init_config_t wifi_config = WIFI_INIT_CONFIG_DEFAULT();

    if ((err = esp_wifi_init(&wifi_config)) != ESP_OK ||
        (err = esp_wifi_set_storage(WIFI_STORAGE_RAM)) != ESP_OK ||
        (err = esp_wifi_set_mode(WIFI_MODE_STA)) != ESP_OK ||
        (err = esp_wifi_start()) != ESP_OK ||
        (err = esp_wifi_set_channel(
             channel,
             WIFI_SECOND_CHAN_NONE)) != ESP_OK ||
        (err = esp_wifi_set_ps(WIFI_PS_NONE)) != ESP_OK)
    {
        return err;
    }

    instance_ = this;

    if ((err = esp_now_init()) != ESP_OK ||
        (err = esp_now_register_recv_cb(onDataReceived)) != ESP_OK)
    {
        instance_ = nullptr;
        return err;
    }

    uint8_t local_mac[ESP_NOW_ETH_ALEN] = {};

    if ((err = esp_wifi_get_mac(WIFI_IF_STA, local_mac)) != ESP_OK)
    {
        return err;
    }

    ESP_LOGI(
        TAG,
        "Receiver started on channel %u, STA MAC: " MACSTR,
        channel,
        MAC2STR(local_mac));

    return ESP_OK;
}

void EspNowManager::setReceiveCallback(ReceiveCallback callback)
{
    receive_callback_ = callback;
}

void EspNowManager::onDataReceived(
    const esp_now_recv_info_t *info,
    const uint8_t *data,
    int data_len)
{
    if (instance_ == nullptr ||
        info == nullptr ||
        info->src_addr == nullptr ||
        data == nullptr)
    {
        ESP_LOGW(TAG, "Invalid ESP-NOW packet");
        return;
    }

    if (data_len != static_cast<int>(sizeof(Message)))
    {
        ESP_LOGW(
            TAG,
            "Invalid message length from " MACSTR
            ": received=%d, expected=%u",
            MAC2STR(info->src_addr),
            data_len,
            static_cast<unsigned>(sizeof(Message)));

        return;
    }

    Message message{};

    std::memcpy(
        message.data,
        data,
        sizeof(message.data));

    if (instance_->receive_callback_ != nullptr)
    {
        instance_->receive_callback_(
            info->src_addr,
            message);

        return;
    }

    ESP_LOGI(
        TAG,
        "Received from " MACSTR
        ": %02X %02X %02X %02X %02X %02X %02X %02X",
        MAC2STR(info->src_addr),
        message.data[0],
        message.data[1],
        message.data[2],
        message.data[3],
        message.data[4],
        message.data[5],
        message.data[6],
        message.data[7]);
}