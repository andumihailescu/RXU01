#pragma once

#include <cstdint>

namespace application
{
    struct Config
    {
        uint8_t esp_now_channel = 1;
        uint8_t receive_queue_depth = 8;
        uint8_t send_result_queue_depth = 8;

        bool use_custom_station_mac = true;
        uint8_t station_mac[6] = {
            0x02, 0x52, 0x58, 0x55, 0x00, 0x01};

        uint32_t transport_start_identifier = 0x6E0;
        uint32_t transport_data_identifier = 0x6E1;
        bool transport_identifiers_are_extended = false;
        bool direct_identifiers_are_extended = false;
    };

    /**
     * Initializeaza serviciile RXU01 si ruleaza bucla principala.
     * Functia nu revine in timpul functionarii normale.
     */
    [[noreturn]] void run(const Config &config = {});
}
