#include <cstdint>
#include <cstring>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "esp_err.h"
#include "esp_log.h"

#include "can_manager/can_manager.h"
#include "esp_now/esp_now_manager.h"

#define JTAG_ATTACH_DELAY_MS 10000

namespace
{
    constexpr char TAG[] = "Main";
    constexpr uint32_t CAN_MESSAGE_ID = 0x500;

    EspNowManager esp_now_manager;
    CanManager can_manager;

    QueueHandle_t esp_now_message_queue = nullptr;

    void onEspNowMessage(
        const uint8_t *source_mac,
        const EspNowManager::Message &message)
    {
        (void)source_mac;

        if (esp_now_message_queue == nullptr)
        {
            return;
        }

        if (xQueueSend(esp_now_message_queue, &message, 0) != pdTRUE)
        {
            ESP_LOGW(TAG, "ESP-NOW queue full; packet dropped");
        }
    }

    bool forwardMessageToCan(const EspNowManager::Message &message)
    {
        can_frame frame = {};

        frame.can_id = CAN_MESSAGE_ID;
        frame.can_dlc = sizeof(message.data);

        std::memcpy(
            frame.data,
            message.data,
            sizeof(message.data));

        const MCP2515::ERROR result = can_manager.send(frame);

        if (result != MCP2515::ERROR_OK)
        {
            ESP_LOGE(
                TAG,
                "CAN transmission failed for ID 0x%03lX: %d",
                static_cast<unsigned long>(frame.can_id),
                result);

            return false;
        }

        ESP_LOGI(
            TAG,
            "Forwarded ESP-NOW message to CAN: "
            "%02X %02X %02X %02X %02X %02X %02X %02X",
            message.data[0],
            message.data[1],
            message.data[2],
            message.data[3],
            message.data[4],
            message.data[5],
            message.data[6],
            message.data[7]);

        return true;
    }
}

extern "C" void app_main(void)
{
    // Oferă timp pentru atașarea debuggerului JTAG după reset.
    vTaskDelay(pdMS_TO_TICKS(JTAG_ATTACH_DELAY_MS));

    static_assert(
        sizeof(EspNowManager::Message) == 8,
        "ESP-NOW Message must be exactly 8 bytes");

    esp_now_message_queue = xQueueCreate(
        8,
        sizeof(EspNowManager::Message));

    if (esp_now_message_queue == nullptr)
    {
        ESP_LOGE(TAG, "Failed to create ESP-NOW queue");
        return;
    }

    esp_now_manager.setReceiveCallback(onEspNowMessage);

    ESP_ERROR_CHECK(
        esp_now_manager.begin(1));

    const esp_err_t can_status = can_manager.begin();

    if (can_status != ESP_OK)
    {
        ESP_LOGE(
            TAG,
            "CAN initialization failed: %s",
            esp_err_to_name(can_status));

        return;
    }

    while (true)
    {
        EspNowManager::Message message = {};

        if (xQueueReceive(
                esp_now_message_queue,
                &message,
                portMAX_DELAY) != pdTRUE)
        {
            continue;
        }

        forwardMessageToCan(message);
    }
}