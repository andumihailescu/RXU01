#pragma once

#include <cstdint>

#include "vehicle_can_protocol/vehicle_can_protocol.h"

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

        uint8_t txu01_mac[6] = {
            0x02, 0x52, 0x58, 0x55, 0x00, 0x02};

        can_command_router::Config command_router =
            vehicle_can_protocol::make_command_router_config();
    };

    /**
     * Initializeaza serviciile RXU01 si ruleaza bucla principala.
     * Functia nu revine in timpul functionarii normale.
     */
    [[noreturn]] void run(const Config &config = {});
}
