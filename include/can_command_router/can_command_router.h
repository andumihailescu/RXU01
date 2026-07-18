#pragma once

#include <cstddef>
#include <cstdint>

#include "remote_protocol/remote_protocol.h"

namespace can_command_router
{
    constexpr std::size_t CLASSIC_CAN_MAX_DATA_LENGTH = 8;
    constexpr std::size_t MAX_COMMAND_PAYLOAD_LENGTH =
        CLASSIC_CAN_MAX_DATA_LENGTH - 1;

    using EcuId = uint8_t;
    constexpr EcuId INVALID_ECU_ID = 0;

    struct CanFrame
    {
        uint32_t identifier = 0;
        bool extended_identifier = false;
        uint8_t data_length = 0;
        uint8_t data[CLASSIC_CAN_MAX_DATA_LENGTH]{};
    };

    /**
     * Un endpoint CAN de comanda pentru un ECU/rol logic din masina.
     */
    struct EcuDefinition
    {
        EcuId ecu_id = INVALID_ECU_ID;
        uint32_t command_can_identifier = 0;
        bool extended_identifier = false;
    };

    using PayloadValidator =
        bool (*)(const uint8_t *payload, uint8_t payload_length);

    /**
     * Leaga o comanda logica ESP-NOW de ECU-ul destinatar si de
     * command_id-ul inteles de acel ECU.
     */
    struct CommandRoute
    {
        uint16_t remote_message_id = 0;
        EcuId destination_ecu = INVALID_ECU_ID;
        uint8_t ecu_command_id = 0;
        uint8_t expected_payload_length = 0;
        PayloadValidator validate_payload = nullptr;
    };

    /**
     * View nemodificabil peste cele doua tabele. Tabelele nu sunt copiate;
     * memoria lor trebuie sa ramana valida cat timp routerul este folosit.
     */
    struct Config
    {
        const EcuDefinition *ecu_registry = nullptr;
        std::size_t ecu_count = 0;
        const CommandRoute *command_routes = nullptr;
        std::size_t command_route_count = 0;
    };

    enum class RouteResult
    {
        Ok,
        InvalidConfiguration,
        InvalidMessageType,
        UnknownCommand,
        InvalidPayloadLength,
        InvalidPayloadValue
    };

    bool is_valid_config(const Config &config);

    /**
     * Traduce o comanda logica TXU01 intr-un singur cadru CAN clasic.
     * Primul byte CAN este command_id-ul ECU-ului destinatar, urmat de
     * argumentele comenzii.
     */
    RouteResult build_frame(
        const remote_protocol::Message &message,
        const Config &config,
        CanFrame &frame);

    const char *to_string(RouteResult result);
}
