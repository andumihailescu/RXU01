#include "application/application.h"

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "esp_err.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_timer.h"

#include "can_manager/can_manager.h"
#include "config/software_version.h"
#include "esp_now_can_gateway/esp_now_can_gateway.h"
#include "esp_now_driver/esp_now_driver.h"
#include "remote_protocol/remote_protocol.h"
#include "wifi_manager/wifi_manager.h"

namespace
{
    constexpr char TAG[] = "RXU01";
    constexpr std::size_t REQUEST_CACHE_SIZE = 16;
    constexpr int64_t REQUEST_CACHE_LIFETIME_US =
        5 * 1000 * 1000;

    CanManager g_can_manager{};
    uint8_t g_txu01_mac[esp_now_driver::MAC_ADDRESS_SIZE]{};

    struct CachedRequest
    {
        bool valid = false;
        uint8_t source_mac[esp_now_driver::MAC_ADDRESS_SIZE]{};
        remote_protocol::MessageType type =
            remote_protocol::MessageType::Command;
        uint8_t flags = remote_protocol::FlagNone;
        uint16_t sequence_number = 0;
        uint16_t message_id = 0;
        uint8_t payload_length = 0;
        uint8_t payload[remote_protocol::MAX_PAYLOAD_SIZE]{};
        remote_protocol::AcknowledgementStatus status =
            remote_protocol::AcknowledgementStatus::InvalidMessage;
        int64_t cached_at_us = 0;
    };

    CachedRequest g_request_cache[REQUEST_CACHE_SIZE]{};
    std::size_t g_next_cache_entry = 0;

    bool is_valid_unicast_mac(
        const uint8_t mac[esp_now_driver::MAC_ADDRESS_SIZE])
    {
        bool is_zero = true;

        for (std::size_t index = 0;
             index < esp_now_driver::MAC_ADDRESS_SIZE;
             ++index)
        {
            is_zero = is_zero && mac[index] == 0;
        }

        return !is_zero && (mac[0] & 0x01U) == 0;
    }

    esp_err_t validate_config(const application::Config &config)
    {
        if (config.esp_now_channel < 1 ||
            config.esp_now_channel > 13 ||
            config.receive_queue_depth == 0 ||
            config.send_result_queue_depth == 0 ||
            !is_valid_unicast_mac(config.txu01_mac) ||
            !can_command_router::is_valid_config(
                config.command_router))
        {
            return ESP_ERR_INVALID_ARG;
        }

        return ESP_OK;
    }

    esp_now_can_gateway::TransmitResult transmit_can_frame(
        const esp_now_can_gateway::CanFrame &source_frame,
        void *context)
    {
        auto *can_manager = static_cast<CanManager *>(context);

        if (can_manager == nullptr)
        {
            return esp_now_can_gateway::TransmitResult::Failed;
        }

        if (source_frame.data_length > CAN_MAX_DLEN)
        {
            return esp_now_can_gateway::TransmitResult::Failed;
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

        const CanManager::TransmitResult result =
            can_manager->send(destination_frame);

        if (result != CanManager::TransmitResult::Ok)
        {
            ESP_LOGE(
                TAG,
                "Eroare transmitere MCP2515: %d, CAN ID=0x%08lX",
                static_cast<int>(result),
                static_cast<unsigned long>(source_frame.identifier));

            switch (result)
            {
            case CanManager::TransmitResult::Busy:
                return esp_now_can_gateway::TransmitResult::Busy;
            case CanManager::TransmitResult::Timeout:
                return esp_now_can_gateway::TransmitResult::Timeout;
            default:
                return esp_now_can_gateway::TransmitResult::Failed;
            }
        }

        ESP_LOGI(
            TAG,
            "CAN TX ID=0x%08lX DLC=%u",
            static_cast<unsigned long>(source_frame.identifier),
            static_cast<unsigned>(source_frame.data_length));

        return esp_now_can_gateway::TransmitResult::Ok;
    }

    esp_err_t initialize_gateway(const application::Config &app_config)
    {
        esp_now_can_gateway::Config config{};
        config.transmit = transmit_can_frame;
        config.transmit_context = &g_can_manager;
        config.command_router = app_config.command_router;

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

    esp_err_t initialize_txu01_peer(
        const application::Config &app_config)
    {
        esp_now_driver::PeerConfig peer{};
        std::memcpy(
            peer.mac,
            app_config.txu01_mac,
            esp_now_driver::MAC_ADDRESS_SIZE);
        peer.channel = 0;
        peer.interface = WIFI_IF_STA;
        peer.encrypt = false;

        const esp_err_t result = esp_now_driver::add_peer(peer);

        if (result == ESP_OK)
        {
            std::memcpy(
                g_txu01_mac,
                app_config.txu01_mac,
                esp_now_driver::MAC_ADDRESS_SIZE);
        }

        return result;
    }

    remote_protocol::AcknowledgementStatus map_ack_status(
        esp_now_can_gateway::ProcessResult result)
    {
        using ProcessResult = esp_now_can_gateway::ProcessResult;
        using AckStatus = remote_protocol::AcknowledgementStatus;

        switch (result)
        {
        case ProcessResult::Ok:
            return AckStatus::CanTransmitted;
        case ProcessResult::InvalidMessage:
            return AckStatus::InvalidMessage;
        case ProcessResult::UnsupportedMessage:
            return AckStatus::UnsupportedMessage;
        case ProcessResult::UnknownCommand:
            return AckStatus::UnknownCommand;
        case ProcessResult::InvalidPayloadLength:
            return AckStatus::InvalidPayloadLength;
        case ProcessResult::InvalidPayloadValue:
            return AckStatus::InvalidPayloadValue;
        case ProcessResult::CanBusy:
            return AckStatus::CanBusy;
        case ProcessResult::CanTimeout:
            return AckStatus::CanTimeout;
        case ProcessResult::CanTransmitFailed:
        default:
            return AckStatus::CanTransmitFailed;
        }
    }

    CachedRequest *find_cached_request(
        const uint8_t source_mac[esp_now_driver::MAC_ADDRESS_SIZE],
        uint16_t sequence_number)
    {
        for (CachedRequest &entry : g_request_cache)
        {
            if (entry.valid &&
                esp_timer_get_time() - entry.cached_at_us >
                    REQUEST_CACHE_LIFETIME_US)
            {
                entry.valid = false;
            }

            if (entry.valid &&
                entry.sequence_number == sequence_number &&
                std::memcmp(
                    entry.source_mac,
                    source_mac,
                    esp_now_driver::MAC_ADDRESS_SIZE) == 0)
            {
                return &entry;
            }
        }

        return nullptr;
    }

    bool is_same_request(
        const CachedRequest &cached,
        const remote_protocol::Message &message)
    {
        return cached.type == message.type &&
               cached.flags == message.flags &&
               cached.message_id == message.message_id &&
               cached.payload_length == message.payload_length &&
               std::memcmp(
                   cached.payload,
                   message.payload,
                   message.payload_length) == 0;
    }

    void cache_request(
        const uint8_t source_mac[esp_now_driver::MAC_ADDRESS_SIZE],
        const remote_protocol::Message &message,
        remote_protocol::AcknowledgementStatus status)
    {
        CachedRequest &entry =
            g_request_cache[g_next_cache_entry];
        entry = {};
        entry.valid = true;
        std::memcpy(
            entry.source_mac,
            source_mac,
            esp_now_driver::MAC_ADDRESS_SIZE);
        entry.type = message.type;
        entry.flags = message.flags;
        entry.sequence_number = message.sequence_number;
        entry.message_id = message.message_id;
        entry.payload_length = message.payload_length;
        std::memcpy(
            entry.payload,
            message.payload,
            message.payload_length);
        entry.status = status;
        entry.cached_at_us = esp_timer_get_time();

        g_next_cache_entry =
            (g_next_cache_entry + 1) % REQUEST_CACHE_SIZE;
    }

    esp_err_t send_acknowledgement(
        const uint8_t destination_mac[
            esp_now_driver::MAC_ADDRESS_SIZE],
        const remote_protocol::Message &request,
        remote_protocol::AcknowledgementStatus status)
    {
        remote_protocol::Message acknowledgement{};
        acknowledgement.type =
            remote_protocol::MessageType::Acknowledgement;
        acknowledgement.flags =
            remote_protocol::FlagIsResponse;
        acknowledgement.sequence_number =
            request.sequence_number;
        acknowledgement.message_id = request.message_id;
        acknowledgement.payload[0] =
            static_cast<uint8_t>(status);
        acknowledgement.payload_length = 1;

        uint8_t packet[remote_protocol::MAX_PACKET_SIZE]{};
        std::size_t packet_length = 0;

        if (remote_protocol::encode(
                acknowledgement,
                packet,
                sizeof(packet),
                packet_length) !=
            remote_protocol::EncodeResult::Ok)
        {
            return ESP_FAIL;
        }

        return esp_now_driver::send(
            destination_mac,
            packet,
            packet_length);
    }

    void log_startup_info(const application::Config &config)
    {
        ESP_LOGI(TAG, "RXU01 pornit");
        ESP_LOGI(
            TAG,
            "Versiune software: %u.%u.%u + CW%02u + CY%02u",
            static_cast<unsigned>(SoftwareVersionConfig::Major),
            static_cast<unsigned>(SoftwareVersionConfig::Minor),
            static_cast<unsigned>(SoftwareVersionConfig::Patch),
            static_cast<unsigned>(SoftwareVersionConfig::IsoWeek),
            static_cast<unsigned>(SoftwareVersionConfig::IsoYearShort));
        ESP_LOGI(
            TAG,
            "Canal ESP-NOW: %u",
            static_cast<unsigned>(config.esp_now_channel));
        ESP_LOGI(TAG, "Astept mesaje ESP-NOW...");
    }

    void process_received_packet(
        const esp_now_driver::ReceivedPacket &packet)
    {
        if (std::memcmp(
                packet.source_mac,
                g_txu01_mac,
                esp_now_driver::MAC_ADDRESS_SIZE) != 0)
        {
            ESP_LOGW(
                TAG,
                "Pachet ignorat de la peer necunoscut " MACSTR,
                MAC2STR(packet.source_mac));
            return;
        }

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

        const bool acknowledgement_requested =
            (message.flags &
             remote_protocol::FlagAckRequested) != 0 &&
            (message.flags &
             remote_protocol::FlagIsResponse) == 0;

        if (acknowledgement_requested)
        {
            CachedRequest *cached = find_cached_request(
                packet.source_mac,
                message.sequence_number);

            if (cached != nullptr)
            {
                const remote_protocol::AcknowledgementStatus status =
                    is_same_request(*cached, message)
                        ? cached->status
                        : remote_protocol::AcknowledgementStatus::
                              InvalidMessage;

                ESP_LOGI(
                    TAG,
                    "Cerere duplicata seq=%u; retransmit ACK status=%u",
                    static_cast<unsigned>(message.sequence_number),
                    static_cast<unsigned>(status));

                if (send_acknowledgement(
                        packet.source_mac,
                        message,
                        status) != ESP_OK)
                {
                    ESP_LOGE(TAG, "Retrimiterea ACK-ului a esuat");
                }

                return;
            }
        }

        const esp_now_can_gateway::ProcessResult result =
            esp_now_can_gateway::process_message(message);

        if (result != esp_now_can_gateway::ProcessResult::Ok)
        {
            ESP_LOGE(
                TAG,
                "Comanda nu a putut fi rutata pe CAN: %s",
                esp_now_can_gateway::to_string(result));
        }

        if (acknowledgement_requested)
        {
            const remote_protocol::AcknowledgementStatus status =
                map_ack_status(result);

            cache_request(
                packet.source_mac,
                message,
                status);

            if (send_acknowledgement(
                    packet.source_mac,
                    message,
                    status) != ESP_OK)
            {
                ESP_LOGE(TAG, "Trimiterea ACK-ului final a esuat");
            }
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
        ESP_ERROR_CHECK(initialize_txu01_peer(config));

        log_startup_info(config);
        run_main_loop();
    }
}
