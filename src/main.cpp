#include <cstddef>
#include <cstdint>
#include <cstring>

#include "esp_err.h"
#include "esp_log.h"
#include "esp_mac.h"

#include "wifi_manager/wifi_manager.h"
#include "esp_now_driver/esp_now_driver.h"
#include "remote_protocol/remote_protocol.h"
#include "esp_now_can_gateway/esp_now_can_gateway.h"

#include "can_manager/can_manager.h"

namespace
{
    constexpr char TAG[] = "RXU01";

    constexpr uint8_t ESPNOW_CHANNEL = 1;
    constexpr uint16_t TEST_MESSAGE_ID = 0x0100;

    CanManager g_can_manager{};

    uint32_t readUint32LittleEndian(
        const uint8_t *data)
    {
        return static_cast<uint32_t>(data[0]) |
               (static_cast<uint32_t>(data[1]) << 8U) |
               (static_cast<uint32_t>(data[2]) << 16U) |
               (static_cast<uint32_t>(data[3]) << 24U);
    }

    /*
     * Adaptor intre esp_now_can_gateway si driverul MCP2515.
     */
    esp_err_t transmitCanFrame(
        const esp_now_can_gateway::CanFrame &source_frame,
        void *context)
    {
        auto *can_manager =
            static_cast<CanManager *>(context);

        if (can_manager == nullptr)
        {
            return ESP_ERR_INVALID_ARG;
        }

        if (source_frame.data_length > 8)
        {
            return ESP_ERR_INVALID_SIZE;
        }

        can_frame destination_frame{};

        /*
         * Momentan folosim identificatori CAN standard pe 11 biti.
         */
        if (source_frame.extended_identifier)
        {
            ESP_LOGE(
                TAG,
                "Driver adaptor: identificatorul CAN extins "
                "nu este implementat momentan");

            return ESP_ERR_NOT_SUPPORTED;
        }

        if (source_frame.identifier > 0x7FF)
        {
            ESP_LOGE(
                TAG,
                "CAN ID invalid pentru format standard: 0x%08lX",
                static_cast<unsigned long>(
                    source_frame.identifier));

            return ESP_ERR_INVALID_ARG;
        }

        destination_frame.can_id =
            source_frame.identifier;

        destination_frame.can_dlc =
            source_frame.data_length;

        if (source_frame.data_length > 0)
        {
            std::memcpy(
                destination_frame.data,
                source_frame.data,
                source_frame.data_length);
        }

        const MCP2515::ERROR result =
            can_manager->send(
                destination_frame);

        if (result != MCP2515::ERROR_OK)
        {
            ESP_LOGE(
                TAG,
                "Eroare transmitere MCP2515: %d, "
                "CAN ID=0x%03lX",
                static_cast<int>(result),
                static_cast<unsigned long>(
                    source_frame.identifier));

            return ESP_FAIL;
        }

        ESP_LOGI(
            TAG,
            "CAN TX ID=0x%03lX DLC=%u DATA="
            "%02X %02X %02X %02X "
            "%02X %02X %02X %02X",
            static_cast<unsigned long>(
                destination_frame.can_id),
            static_cast<unsigned>(
                destination_frame.can_dlc),
            destination_frame.data[0],
            destination_frame.data[1],
            destination_frame.data[2],
            destination_frame.data[3],
            destination_frame.data[4],
            destination_frame.data[5],
            destination_frame.data[6],
            destination_frame.data[7]);

        return ESP_OK;
    }

    void processReceivedPacket(
        const esp_now_driver::ReceivedPacket &packet)
    {
        remote_protocol::Message message{};

        const remote_protocol::DecodeResult decode_result =
            remote_protocol::decode(
                packet.data,
                packet.data_length,
                message);

        if (decode_result !=
            remote_protocol::DecodeResult::Ok)
        {
            ESP_LOGW(
                TAG,
                "Pachet invalid de la " MACSTR
                ": %s, lungime=%u",
                MAC2STR(packet.source_mac),
                remote_protocol::to_string(
                    decode_result),
                static_cast<unsigned>(
                    packet.data_length));

            return;
        }

        ESP_LOGI(
            TAG,
            "ESP-NOW RX de la " MACSTR
            ", RSSI=%d, destination=0x%02X, "
            "ID=0x%04X, payload=%u bytes",
            MAC2STR(packet.source_mac),
            static_cast<int>(packet.rssi),
            static_cast<unsigned>(
                message.destination),
            static_cast<unsigned>(
                message.message_id),
            static_cast<unsigned>(
                message.payload_length));

        if (message.message_id == TEST_MESSAGE_ID &&
            message.payload_length == 8)
        {
            const uint32_t counter =
                readUint32LittleEndian(
                    message.payload);

            ESP_LOGI(
                TAG,
                "Counter=%lu, date: "
                "%02X %02X %02X %02X "
                "%02X %02X %02X %02X",
                static_cast<unsigned long>(counter),
                message.payload[0],
                message.payload[1],
                message.payload[2],
                message.payload[3],
                message.payload[4],
                message.payload[5],
                message.payload[6],
                message.payload[7]);
        }

        /*
         * Pentru test trimitem orice mesaj valid pe CAN,
         * inclusiv mesajele marcate Destination::Rxu01.
         *
         * Ulterior vom face rutarea dupa destination.
         */
        const esp_err_t gateway_result =
            esp_now_can_gateway::process_message(
                message);

        if (gateway_result != ESP_OK)
        {
            ESP_LOGE(
                TAG,
                "Mesajul nu a putut fi trimis pe CAN: %s",
                esp_err_to_name(
                    gateway_result));
        }
    }
}

extern "C" void app_main(void)
{
    /*
     * 1. Initializare MCP2515.
     */
    const esp_err_t can_result =
        g_can_manager.begin();

    if (can_result != ESP_OK)
    {
        ESP_LOGE(
            TAG,
            "Initializarea CAN a esuat: %s",
            esp_err_to_name(can_result));

        return;
    }

    /*
     * 2. Initializare gateway ESP-NOW -> CAN.
     */
    esp_now_can_gateway::Config gateway_config{};

    gateway_config.transmit =
        transmitCanFrame;

    gateway_config.transmit_context =
        &g_can_manager;

    gateway_config.direct_identifiers_are_extended =
        false;

    gateway_config.transport_identifiers_are_extended =
        false;

    gateway_config.transport_start_identifier =
        0x6E0;

    gateway_config.transport_data_identifier =
        0x6E1;

    ESP_ERROR_CHECK(
        esp_now_can_gateway::init(
            gateway_config));

    /*
     * 3. Initializare Wi-Fi.
     */
    wifi_manager::Config wifi_config{};

    wifi_config.channel =
        ESPNOW_CHANNEL;

    wifi_config.storage =
        WIFI_STORAGE_RAM;

    wifi_config.power_save =
        WIFI_PS_NONE;

    ESP_ERROR_CHECK(
        wifi_manager::init(
            wifi_config));

    /*
     * 4. Initializare ESP-NOW.
     */
    esp_now_driver::Config espnow_config{};

    espnow_config.interface =
        WIFI_IF_STA;

    espnow_config.default_peer_channel =
        ESPNOW_CHANNEL;

    espnow_config.receive_queue_depth = 8;
    espnow_config.send_result_queue_depth = 4;

    ESP_ERROR_CHECK(
        esp_now_driver::init(
            espnow_config));

    /*
     * 5. Afisare informatii RXU01.
     */
    uint8_t local_mac[esp_now_driver::MAC_ADDRESS_SIZE]{};

    ESP_ERROR_CHECK(
        esp_now_driver::get_local_mac(
            local_mac));

    ESP_LOGI(
        TAG,
        "RXU01 pornit");

    ESP_LOGI(
        TAG,
        "MCP2515 initializat");

    ESP_LOGI(
        TAG,
        "Canal ESP-NOW: %u",
        static_cast<unsigned>(
            ESPNOW_CHANNEL));

    ESP_LOGI(
        TAG,
        "MAC local STA: " MACSTR,
        MAC2STR(local_mac));

    ESP_LOGI(
        TAG,
        "Astept mesaje ESP-NOW...");

    /*
     * 6. Receptie ESP-NOW.
     */
    esp_now_driver::ReceivedPacket packet{};

    while (true)
    {
        const bool received =
            esp_now_driver::receive(
                packet,
                esp_now_driver::WAIT_FOREVER);

        if (!received)
        {
            ESP_LOGW(
                TAG,
                "Eroare la asteptarea pachetului");

            continue;
        }

        processReceivedPacket(packet);
    }
}