#pragma once

#include <cstdint>

#include "esp_err.h"
#include "esp_now.h"

class EspNowManager
{
public:
    struct Message
    {
        uint8_t data[8];
    };

    using ReceiveCallback = void (*)(
        const uint8_t *source_mac,
        const Message &message);

    esp_err_t begin(uint8_t channel = 1);
    void setReceiveCallback(ReceiveCallback callback);

private:
    static void onDataReceived(
        const esp_now_recv_info_t *info,
        const uint8_t *data,
        int data_len);

    static EspNowManager *instance_;
    ReceiveCallback receive_callback_ = nullptr;
};

static_assert(
    sizeof(EspNowManager::Message) == 8,
    "ESP-NOW message must be exactly 8 bytes");