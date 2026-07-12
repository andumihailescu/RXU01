#pragma once

#include <cstddef>
#include <cstdint>

#include "esp_err.h"

#include "remote_protocol/remote_protocol.h"

namespace rxu01_router
{
    /**
     * Handler pentru mesaje destinate chiar lui RXU01.
     *
     * Exemplu:
     * - configurare RXU01;
     * - heartbeat;
     * - cerere software version;
     * - testul cu counter.
     */
    using LocalMessageCallback =
        esp_err_t (*)(
            const remote_protocol::Message &message,
            void *context);

    struct Config
    {
        LocalMessageCallback local_handler =
            nullptr;

        void *local_context =
            nullptr;

        bool deliver_broadcast_locally =
            true;

        bool forward_broadcast_to_can =
            true;
    };

    struct Statistics
    {
        uint32_t valid_packets = 0;
        uint32_t invalid_packets = 0;

        uint32_t local_messages = 0;
        uint32_t can_messages = 0;

        uint32_t routing_errors = 0;
    };

    /**
     * Gateway-ul CAN trebuie initializat inainte
     * daca vor fi rutate mesaje catre CAN.
     */
    esp_err_t init(const Config &config);

    esp_err_t deinit();

    bool is_initialized();

    esp_err_t process_packet(
        const uint8_t *packet,
        std::size_t packet_length);

    esp_err_t process_message(
        const remote_protocol::Message &message);

    Statistics get_statistics();

    void reset_statistics();
}
