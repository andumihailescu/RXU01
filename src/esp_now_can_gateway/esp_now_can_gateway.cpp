#include "esp_now_can_gateway/esp_now_can_gateway.h"

#include <algorithm>
#include <cstring>

namespace
{
    constexpr uint8_t TRANSPORT_START_MARKER =
        0x10;

    constexpr uint8_t TRANSPORT_DATA_MARKER =
        0x20;

    constexpr uint8_t
        TRANSPORT_FRAGMENT_INDEX_MASK =
            0x0F;

    esp_now_can_gateway::Config
        g_config{};

    esp_now_can_gateway::Statistics
        g_statistics{};

    bool g_initialized = false;

    uint8_t g_next_transfer_id = 1;

    bool isValidCanIdentifier(
        uint32_t identifier,
        bool extended)
    {
        return extended
                   ? identifier <= 0x1FFFFFFFU
                   : identifier <= 0x7FFU;
    }

    uint8_t getNextTransferId()
    {
        const uint8_t current =
            g_next_transfer_id;

        ++g_next_transfer_id;

        if (g_next_transfer_id == 0)
        {
            g_next_transfer_id = 1;
        }

        return current;
    }

    void writeUint16LittleEndian(
        uint8_t *destination,
        uint16_t value)
    {
        destination[0] =
            static_cast<uint8_t>(
                value & 0xFFU);

        destination[1] =
            static_cast<uint8_t>(
                (value >> 8U) & 0xFFU);
    }

    esp_err_t transmitFrame(
        const esp_now_can_gateway::CanFrame &frame)
    {
        const esp_err_t result =
            g_config.transmit(
                frame,
                g_config.transmit_context);

        if (result == ESP_OK)
        {
            ++g_statistics.frames_sent;
        }
        else
        {
            ++g_statistics.transmit_errors;
        }

        return result;
    }

    esp_err_t sendDirectMessage(
        const remote_protocol::Message &message)
    {
        const uint32_t identifier =
            static_cast<uint32_t>(
                message.message_id);

        if (!isValidCanIdentifier(
                identifier,
                g_config
                    .direct_identifiers_are_extended))
        {
            return ESP_ERR_INVALID_ARG;
        }

        esp_now_can_gateway::CanFrame frame{};

        frame.identifier = identifier;

        frame.extended_identifier =
            g_config
                .direct_identifiers_are_extended;

        frame.data_length =
            message.payload_length;

        if (message.payload_length > 0)
        {
            std::memcpy(
                frame.data,
                message.payload,
                message.payload_length);
        }

        const esp_err_t result =
            transmitFrame(frame);

        if (result == ESP_OK)
        {
            ++g_statistics.direct_messages;
        }

        return result;
    }

    esp_err_t sendFragmentedMessage(
        const remote_protocol::Message &message)
    {
        const std::size_t fragment_count =
            (message.payload_length +
             esp_now_can_gateway::
                 TRANSPORT_DATA_BYTES_PER_FRAME -
             1) /
            esp_now_can_gateway::
                TRANSPORT_DATA_BYTES_PER_FRAME;

        if (fragment_count == 0 ||
            fragment_count >
                (TRANSPORT_FRAGMENT_INDEX_MASK +
                 1U))
        {
            return ESP_ERR_INVALID_SIZE;
        }

        const uint8_t transfer_id =
            getNextTransferId();

        const uint16_t payload_crc =
            remote_protocol::calculate_crc16(
                message.payload,
                message.payload_length);

        /**
         * Start frame:
         *
         * byte 0   = 0x10
         * byte 1   = transfer ID
         * byte 2   = tipul mesajului
         * byte 3-4 = message ID
         * byte 5   = lungime totala
         * byte 6-7 = CRC16 al payloadului
         */
        esp_now_can_gateway::CanFrame
            start_frame{};

        start_frame.identifier =
            g_config
                .transport_start_identifier;

        start_frame.extended_identifier =
            g_config
                .transport_identifiers_are_extended;

        start_frame.data_length =
            esp_now_can_gateway::
                CLASSIC_CAN_MAX_DATA_LENGTH;

        start_frame.data[0] =
            TRANSPORT_START_MARKER;

        start_frame.data[1] =
            transfer_id;

        start_frame.data[2] =
            static_cast<uint8_t>(
                message.type);

        writeUint16LittleEndian(
            &start_frame.data[3],
            message.message_id);

        start_frame.data[5] =
            message.payload_length;

        writeUint16LittleEndian(
            &start_frame.data[6],
            payload_crc);

        esp_err_t result =
            transmitFrame(start_frame);

        if (result != ESP_OK)
        {
            return result;
        }

        std::size_t payload_offset = 0;

        for (std::size_t fragment_index = 0;
             fragment_index < fragment_count;
             ++fragment_index)
        {
            esp_now_can_gateway::CanFrame
                data_frame{};

            data_frame.identifier =
                g_config
                    .transport_data_identifier;

            data_frame.extended_identifier =
                g_config
                    .transport_identifiers_are_extended;

            data_frame.data_length =
                esp_now_can_gateway::
                    CLASSIC_CAN_MAX_DATA_LENGTH;

            data_frame.data[0] =
                static_cast<uint8_t>(
                    TRANSPORT_DATA_MARKER |
                    static_cast<uint8_t>(
                        fragment_index));

            data_frame.data[1] =
                transfer_id;

            const std::size_t remaining =
                message.payload_length -
                payload_offset;

            const std::size_t
                bytes_in_fragment =
                    std::min(
                        remaining,
                        esp_now_can_gateway::
                            TRANSPORT_DATA_BYTES_PER_FRAME);

            std::memcpy(
                &data_frame.data[2],
                &message.payload[payload_offset],
                bytes_in_fragment);

            result =
                transmitFrame(data_frame);

            if (result != ESP_OK)
            {
                return result;
            }

            payload_offset +=
                bytes_in_fragment;
        }

        ++g_statistics.fragmented_messages;

        return ESP_OK;
    }
}

namespace esp_now_can_gateway
{
    esp_err_t init(const Config &config)
    {
        if (g_initialized)
        {
            return ESP_OK;
        }

        if (config.transmit == nullptr)
        {
            return ESP_ERR_INVALID_ARG;
        }

        if (!isValidCanIdentifier(
                config
                    .transport_start_identifier,
                config
                    .transport_identifiers_are_extended) ||
            !isValidCanIdentifier(
                config
                    .transport_data_identifier,
                config
                    .transport_identifiers_are_extended))
        {
            return ESP_ERR_INVALID_ARG;
        }

        if (config.transport_start_identifier ==
            config.transport_data_identifier)
        {
            return ESP_ERR_INVALID_ARG;
        }

        g_config = config;
        g_next_transfer_id = 1;

        reset_statistics();

        g_initialized = true;

        return ESP_OK;
    }

    esp_err_t deinit()
    {
        g_initialized = false;
        g_config = {};
        g_next_transfer_id = 1;

        reset_statistics();

        return ESP_OK;
    }

    bool is_initialized()
    {
        return g_initialized;
    }

    esp_err_t process_message(
        const remote_protocol::Message &message)
    {
        if (!g_initialized)
        {
            return ESP_ERR_INVALID_STATE;
        }

        if (!remote_protocol::
                is_valid_message_type(
                    message.type) ||
            !remote_protocol::are_valid_flags(
                message.flags) ||
            message.payload_length >
                remote_protocol::
                    MAX_PAYLOAD_SIZE)
        {
            return ESP_ERR_INVALID_ARG;
        }

        if (message.payload_length <=
            CLASSIC_CAN_MAX_DATA_LENGTH)
        {
            return sendDirectMessage(
                message);
        }

        return sendFragmentedMessage(
            message);
    }

    Statistics get_statistics()
    {
        return g_statistics;
    }

    void reset_statistics()
    {
        g_statistics = {};
    }
}
