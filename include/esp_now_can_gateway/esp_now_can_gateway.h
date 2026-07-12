#pragma once

#include <cstddef>
#include <cstdint>

#include "esp_err.h"

#include "remote_protocol/remote_protocol.h"

namespace esp_now_can_gateway
{
    constexpr std::size_t
        CLASSIC_CAN_MAX_DATA_LENGTH = 8;

    /**
     * Fiecare cadru de fragment transporta
     * 6 bytes utili:
     *
     * byte 0 = marker | fragment index
     * byte 1 = transfer ID
     * byte 2..7 = date
     */
    constexpr std::size_t
        TRANSPORT_DATA_BYTES_PER_FRAME = 6;

    struct CanFrame
    {
        uint32_t identifier = 0;

        bool extended_identifier =
            false;

        uint8_t data_length = 0;

        uint8_t data[CLASSIC_CAN_MAX_DATA_LENGTH]{};
    };

    /**
     * Main-ul va adapta acest callback la
     * metoda reala din driverul MCP2515.
     */
    using TransmitCallback =
        esp_err_t (*)(
            const CanFrame &frame,
            void *context);

    struct Config
    {
        TransmitCallback transmit =
            nullptr;

        void *transmit_context =
            nullptr;

        uint32_t transport_start_identifier =
            0x6E0;

        uint32_t transport_data_identifier =
            0x6E1;

        bool transport_identifiers_are_extended =
            false;

        /**
         * Pentru mesajele de maximum 8 bytes,
         * message_id devine direct CAN ID.
         */
        bool direct_identifiers_are_extended =
            false;
    };

    struct Statistics
    {
        uint32_t direct_messages = 0;
        uint32_t fragmented_messages = 0;
        uint32_t frames_sent = 0;
        uint32_t transmit_errors = 0;
    };

    /**
     * Nu este thread-safe.
     * Apeleaza componenta dintr-un singur task.
     */
    esp_err_t init(const Config &config);

    esp_err_t deinit();

    bool is_initialized();

    esp_err_t process_message(
        const remote_protocol::Message &message);

    Statistics get_statistics();

    void reset_statistics();
}
