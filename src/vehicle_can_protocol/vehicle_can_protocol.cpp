#include "vehicle_can_protocol/vehicle_can_protocol.h"

#include <cstddef>
#include <cstdint>

namespace
{
    bool validate_any_payload(const uint8_t *, uint8_t)
    {
        return true;
    }

    bool is_binary_state(uint8_t state)
    {
        using vehicle_can_protocol::BinaryState;

        return state == static_cast<uint8_t>(BinaryState::Off) ||
               state == static_cast<uint8_t>(BinaryState::On);
    }

    bool validate_indicator_state(
        const uint8_t *payload,
        uint8_t payload_length)
    {
        using vehicle_can_protocol::BinaryState;
        using vehicle_can_protocol::IndicatorStateField;

        if (payload == nullptr ||
            payload_length !=
                vehicle_can_protocol::INDICATOR_STATE_PAYLOAD_LENGTH)
        {
            return false;
        }

        const auto field = [payload](IndicatorStateField index) {
            return payload[static_cast<std::size_t>(index)];
        };
        const uint8_t warning = field(IndicatorStateField::Warning);
        const uint8_t left = field(IndicatorStateField::Left);
        const uint8_t right = field(IndicatorStateField::Right);

        if (!is_binary_state(warning) ||
            !is_binary_state(left) ||
            !is_binary_state(right))
        {
            return false;
        }

        return !(left == static_cast<uint8_t>(BinaryState::On) &&
                 right == static_cast<uint8_t>(BinaryState::On));
    }

    bool validate_exterior_light_state(
        const uint8_t *payload,
        uint8_t payload_length)
    {
        using vehicle_can_protocol::ExteriorLightMode;
        using vehicle_can_protocol::ExteriorLightStateField;

        if (payload == nullptr ||
            payload_length !=
                vehicle_can_protocol::
                    EXTERIOR_LIGHT_STATE_PAYLOAD_LENGTH)
        {
            return false;
        }

        const auto field = [payload](ExteriorLightStateField index) {
            return payload[static_cast<std::size_t>(index)];
        };

        if (field(ExteriorLightStateField::Mode) >
            static_cast<uint8_t>(ExteriorLightMode::LowBeam))
        {
            return false;
        }

        return is_binary_state(
                   field(ExteriorLightStateField::FrontProjectors)) &&
               is_binary_state(
                   field(ExteriorLightStateField::FogLights)) &&
               is_binary_state(
                   field(ExteriorLightStateField::HighBeam));
    }

    bool validate_binary_state(
        const uint8_t *payload,
        uint8_t payload_length)
    {
        return payload != nullptr &&
               payload_length == 1 &&
               is_binary_state(payload[0]);
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
                    LightingSetIndicatorState),
            ecu_id(vehicle_can_protocol::EcuRole::Lighting),
            lmcu_command_id(
                vehicle_can_protocol::Lmcu100CommandId::
                    SetIndicatorState),
            vehicle_can_protocol::INDICATOR_STATE_PAYLOAD_LENGTH,
            validate_indicator_state,
        },
        {
            remote_id(
                vehicle_can_protocol::RemoteCommandId::
                    LightingSetExteriorLightsState),
            ecu_id(vehicle_can_protocol::EcuRole::Lighting),
            lmcu_command_id(
                vehicle_can_protocol::Lmcu100CommandId::
                    SetExteriorLightsState),
            vehicle_can_protocol::
                EXTERIOR_LIGHT_STATE_PAYLOAD_LENGTH,
            validate_exterior_light_state,
        },
        {
            remote_id(
                vehicle_can_protocol::RemoteCommandId::
                    LightingSetBrakeState),
            ecu_id(vehicle_can_protocol::EcuRole::Lighting),
            lmcu_command_id(
                vehicle_can_protocol::Lmcu100CommandId::
                    SetBrakeState),
            vehicle_can_protocol::BRAKE_STATE_PAYLOAD_LENGTH,
            validate_binary_state,
        },
        {
            remote_id(
                vehicle_can_protocol::RemoteCommandId::
                    LightingSetReverseLightState),
            ecu_id(vehicle_can_protocol::EcuRole::Lighting),
            lmcu_command_id(
                vehicle_can_protocol::Lmcu100CommandId::
                    SetReverseLightState),
            vehicle_can_protocol::REVERSE_LIGHT_STATE_PAYLOAD_LENGTH,
            validate_binary_state,
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
