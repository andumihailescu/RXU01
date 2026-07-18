#include "application/application.h"

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "esp_err.h"
#include "esp_log.h"
#include "esp_mac.h"

#include "can_manager/can_manager.h"
#include "esp_now_can_gateway/esp_now_can_gateway.h"
#include "esp_now_driver/esp_now_driver.h"
#include "remote_protocol/remote_protocol.h"
#include "wifi_manager/wifi_manager.h"

namespace
{
    constexpr char TAG[] = "RXU01";
    constexpr uint16_t TEST_MESSAGE_ID = 0x0100;

    CanManager g_can_manager{};

    esp_err_t validate_config(const application::Config &config)
    {
        if (config.esp_now_channel < 1 ||
            config.esp_now_channel > 13 ||
            config.receive_queue_depth == 0 ||
            config.send_result_queue_depth == 0 ||
            config.transport_start_identifier ==
                config.transport_data_identifier)
        {
            return ESP_ERR_INVALID_ARG;
        }

        return ESP_OK;
    }

    uint32_t read_uint32_little_endian(const uint8_t *data)
    {
        return static_cast<uint32_t>(data[0]) |
               (static_cast<uint32_t>(data[1]) << 8U) |
               (static_cast<uint32_t>(data[2]) << 16U) |
               (static_cast<uint32_t>(data[3]) << 24U);
    }

    esp_err_t transmit_can_frame(
        const esp_now_can_gateway::CanFrame &source_frame,
        void *context)
    {
        auto *can_manager = static_cast<CanManager *>(context);

        if (can_manager == nullptr)
        {
            return ESP_ERR_INVALID_ARG;
        }

        if (source_frame.data_length > CAN_MAX_DLEN)
        {
            return ESP_ERR_INVALID_SIZE;
        }

        can_frame destination_frame{};
        destination_frame.can_id = source_frame.identifier;

        if (source_frame.extended_identifier)
        {
            destination_frame.can_id |= CAN_EFF_FLAG;
        }

        destination_frame.can_dlc = source_frame.data_length;

        if (source_frame.data_length > 0)
        {
            std::memcpy(
                destination_frame.data,
                source_frame.data,
                source_frame.data_length);
        }

        const MCP2515::ERROR result =
            can_manager->send(destination_frame);

        if (result != MCP2515::ERROR_OK)
        {
            ESP_LOGE(
                TAG,
                "Eroare transmitere MCP2515: %d, CAN ID=0x%08lX",
                static_cast<int>(result),
                static_cast<unsigned long>(source_frame.identifier));

            return ESP_FAIL;
        }

        ESP_LOGI(
            TAG,
            "CAN TX ID=0x%08lX DLC=%u",
            static_cast<unsigned long>(source_frame.identifier),
            static_cast<unsigned>(source_frame.data_length));

        return ESP_OK;
    }

    esp_err_t initialize_gateway(const application::Config &app_config)
    {
        esp_now_can_gateway::Config config{};
        config.transmit = transmit_can_frame;
        config.transmit_context = &g_can_manager;
        config.transport_start_identifier =
            app_config.transport_start_identifier;
        config.transport_data_identifier =
            app_config.transport_data_identifier;
        config.transport_identifiers_are_extended =
            app_config.transport_identifiers_are_extended;
        config.direct_identifiers_are_extended =
            app_config.direct_identifiers_are_extended;

        return esp_now_can_gateway::init(config);
    }

    esp_err_t initialize_wifi(const application::Config &app_config)
    {
        wifi_manager::Config config{};
        config.channel = app_config.esp_now_channel;
        config.storage = WIFI_STORAGE_RAM;
        config.power_save = WIFI_PS_NONE;
        config.use_custom_station_mac =
            app_config.use_custom_station_mac;

        std::memcpy(
            config.station_mac,
            app_config.station_mac,
            wifi_manager::MAC_ADDRESS_SIZE);

        return wifi_manager::init(config);
    }

    esp_err_t initialize_esp_now(const application::Config &app_config)
    {
        esp_now_driver::Config config{};
        config.interface = WIFI_IF_STA;
        // Canalul 0 urmeaza canalul curent al interfetei Wi-Fi.
        config.default_peer_channel = 0;
        config.receive_queue_depth = app_config.receive_queue_depth;
        config.send_result_queue_depth = app_config.send_result_queue_depth;

        return esp_now_driver::init(config);
    }

    void log_startup_info(const application::Config &config)
    {
        ESP_LOGI(TAG, "RXU01 pornit");
        ESP_LOGI(
            TAG,
            "Canal ESP-NOW: %u",
            static_cast<unsigned>(config.esp_now_channel));
        ESP_LOGI(TAG, "Astept mesaje ESP-NOW...");
    }

    void process_received_packet(
        const esp_now_driver::ReceivedPacket &packet)
    {
        remote_protocol::Message message{};

        const remote_protocol::DecodeResult decode_result =
            remote_protocol::decode(
                packet.data,
                packet.data_length,
                message);

        if (decode_result != remote_protocol::DecodeResult::Ok)
        {
            ESP_LOGW(
                TAG,
                "Pachet invalid de la " MACSTR ": %s, lungime=%u",
                MAC2STR(packet.source_mac),
                remote_protocol::to_string(decode_result),
                static_cast<unsigned>(packet.data_length));

            return;
        }

        ESP_LOGI(
            TAG,
            "ESP-NOW RX de la " MACSTR
            ", RSSI=%d, tip=%u, ID=0x%04X, payload=%u bytes",
            MAC2STR(packet.source_mac),
            static_cast<int>(packet.rssi),
            static_cast<unsigned>(message.type),
            static_cast<unsigned>(message.message_id),
            static_cast<unsigned>(message.payload_length));

        if (message.message_id == TEST_MESSAGE_ID &&
            message.payload_length == 8)
        {
            ESP_LOGI(
                TAG,
                "Counter=%lu",
                static_cast<unsigned long>(
                    read_uint32_little_endian(message.payload)));
        }

        const esp_err_t result =
            esp_now_can_gateway::process_message(message);

        if (result != ESP_OK)
        {
            ESP_LOGE(
                TAG,
                "Mesajul nu a putut fi trimis pe CAN: %s",
                esp_err_to_name(result));
        }
    }

    [[noreturn]] void run_main_loop()
    {
        esp_now_driver::ReceivedPacket packet{};

        while (true)
        {
            if (!esp_now_driver::receive(
                    packet,
                    esp_now_driver::WAIT_FOREVER))
            {
                ESP_LOGW(TAG, "Eroare la asteptarea pachetului");
                continue;
            }

            process_received_packet(packet);
        }
    }
}

namespace application
{
    [[noreturn]] void run(const Config &config)
    {
        ESP_ERROR_CHECK(validate_config(config));
        ESP_ERROR_CHECK(g_can_manager.begin());
        ESP_ERROR_CHECK(initialize_gateway(config));
        ESP_ERROR_CHECK(initialize_wifi(config));
        ESP_ERROR_CHECK(initialize_esp_now(config));

        log_startup_info(config);
        run_main_loop();
    }
}
