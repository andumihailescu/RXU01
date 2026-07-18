#include "vehicle_can_protocol/vehicle_can_protocol.h"

#include <cstddef>
#include <cstdint>

namespace
{
    bool validate_any_payload(const uint8_t *, uint8_t)
    {
        return true;
    }

    bool validate_turn_signal(
        const uint8_t *payload,
        uint8_t payload_length)
    {
        using vehicle_can_protocol::BinaryState;
        using vehicle_can_protocol::TurnSignal;

        if (payload_length != 2)
        {
            return false;
        }

        const uint8_t selector = payload[0];
        const uint8_t state = payload[1];

        const bool valid_selector =
            selector == static_cast<uint8_t>(TurnSignal::Left) ||
            selector == static_cast<uint8_t>(TurnSignal::Right) ||
            selector == static_cast<uint8_t>(TurnSignal::Warning);

        const bool valid_state =
            state == static_cast<uint8_t>(BinaryState::Off) ||
            state == static_cast<uint8_t>(BinaryState::On);

        return valid_selector && valid_state;
    }

    constexpr can_command_router::EcuId ecu_id(
        vehicle_can_protocol::EcuRole role)
    {
        return static_cast<can_command_router::EcuId>(role);
    }

    constexpr uint16_t remote_id(
        vehicle_can_protocol::RemoteCommandId id)
    {
        return static_cast<uint16_t>(id);
    }

    constexpr uint8_t lmcu_command_id(
        vehicle_can_protocol::Lmcu100CommandId id)
    {
        return static_cast<uint8_t>(id);
    }

    const can_command_router::EcuDefinition ECU_REGISTRY[] = {
        {
            ecu_id(vehicle_can_protocol::EcuRole::Lighting),
            vehicle_can_protocol::LMCU100_COMMAND_CAN_ID,
            false,
        },
    };

    const can_command_router::CommandRoute COMMAND_ROUTES[] = {
        {
            remote_id(
                vehicle_can_protocol::RemoteCommandId::
                    LightingGetSoftwareVersion),
            ecu_id(vehicle_can_protocol::EcuRole::Lighting),
            lmcu_command_id(
                vehicle_can_protocol::Lmcu100CommandId::
                    GetSoftwareVersion),
            0,
            validate_any_payload,
        },
        {
            remote_id(
                vehicle_can_protocol::RemoteCommandId::
                    LightingSetTaillightsBrightness),
            ecu_id(vehicle_can_protocol::EcuRole::Lighting),
            lmcu_command_id(
                vehicle_can_protocol::Lmcu100CommandId::
                    SetTaillightsBrightness),
            1,
            validate_any_payload,
        },
        {
            remote_id(
                vehicle_can_protocol::RemoteCommandId::
                    LightingActivateTurnSignal),
            ecu_id(vehicle_can_protocol::EcuRole::Lighting),
            lmcu_command_id(
                vehicle_can_protocol::Lmcu100CommandId::
                    ActivateTurnSignal),
            2,
            validate_turn_signal,
        },
    };
}

namespace vehicle_can_protocol
{
    can_command_router::Config make_command_router_config()
    {
        can_command_router::Config config{};
        config.ecu_registry = ECU_REGISTRY;
        config.ecu_count = sizeof(ECU_REGISTRY) /
                           sizeof(ECU_REGISTRY[0]);
        config.command_routes = COMMAND_ROUTES;
        config.command_route_count = sizeof(COMMAND_ROUTES) /
                                     sizeof(COMMAND_ROUTES[0]);
        return config;
    }
}
