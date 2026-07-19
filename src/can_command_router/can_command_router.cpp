#include "can_command_router/can_command_router.h"

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace
{
    bool is_valid_can_identifier(
        const can_command_router::EcuDefinition &ecu)
    {
        return ecu.extended_identifier
                   ? ecu.command_can_identifier <= 0x1FFFFFFFU
                   : ecu.command_can_identifier <= 0x7FFU;
    }

    const can_command_router::EcuDefinition *find_ecu(
        const can_command_router::Config &config,
        can_command_router::EcuId ecu_id)
    {
        for (std::size_t index = 0;
             index < config.ecu_count;
             ++index)
        {
            if (config.ecu_registry[index].ecu_id == ecu_id)
            {
                return &config.ecu_registry[index];
            }
        }

        return nullptr;
    }

    const can_command_router::CommandRoute *find_route(
        const can_command_router::Config &config,
        uint16_t remote_message_id)
    {
        for (std::size_t index = 0;
             index < config.command_route_count;
             ++index)
        {
            if (config.command_routes[index].remote_message_id ==
                remote_message_id)
            {
                return &config.command_routes[index];
            }
        }

        return nullptr;
    }
}

namespace can_command_router
{
    bool is_valid_config(const Config &config)
    {
        if (config.ecu_registry == nullptr ||
            config.ecu_count == 0 ||
            config.command_routes == nullptr ||
            config.command_route_count == 0)
        {
            return false;
        }

        for (std::size_t ecu_index = 0;
             ecu_index < config.ecu_count;
             ++ecu_index)
        {
            const EcuDefinition &ecu =
                config.ecu_registry[ecu_index];

            if (ecu.ecu_id == INVALID_ECU_ID ||
                !is_valid_can_identifier(ecu))
            {
                return false;
            }

            for (std::size_t other_index = ecu_index + 1;
                 other_index < config.ecu_count;
                 ++other_index)
            {
                if (config.ecu_registry[other_index].ecu_id ==
                    ecu.ecu_id)
                {
                    return false;
                }
            }
        }

        for (std::size_t route_index = 0;
             route_index < config.command_route_count;
             ++route_index)
        {
            const CommandRoute &route =
                config.command_routes[route_index];
            const EcuId namespace_ecu =
                static_cast<EcuId>(
                    (route.remote_message_id >> 8U) & 0xFFU);

            if (route.remote_message_id == 0 ||
                route.destination_ecu == INVALID_ECU_ID ||
                route.destination_ecu != namespace_ecu ||
                route.expected_payload_length >
                    MAX_COMMAND_PAYLOAD_LENGTH ||
                route.validate_payload == nullptr ||
                find_ecu(config, route.destination_ecu) == nullptr)
            {
                return false;
            }

            for (std::size_t other_index = route_index + 1;
                 other_index < config.command_route_count;
                 ++other_index)
            {
                if (config.command_routes[other_index].remote_message_id ==
                    route.remote_message_id)
                {
                    return false;
                }
            }
        }

        return true;
    }

    RouteResult build_frame(
        const remote_protocol::Message &message,
        const Config &config,
        CanFrame &frame)
    {
        frame = {};

        if (!is_valid_config(config))
        {
            return RouteResult::InvalidConfiguration;
        }

        if (message.type != remote_protocol::MessageType::Command ||
            (message.flags & remote_protocol::FlagIsResponse) != 0)
        {
            return RouteResult::InvalidMessageType;
        }

        const CommandRoute *route = find_route(
            config,
            message.message_id);

        if (route == nullptr)
        {
            return RouteResult::UnknownCommand;
        }

        if (message.payload_length !=
            route->expected_payload_length)
        {
            return RouteResult::InvalidPayloadLength;
        }

        if (!route->validate_payload(
                message.payload,
                message.payload_length))
        {
            return RouteResult::InvalidPayloadValue;
        }

        const EcuDefinition *ecu = find_ecu(
            config,
            route->destination_ecu);

        if (ecu == nullptr)
        {
            return RouteResult::InvalidConfiguration;
        }

        frame.identifier = ecu->command_can_identifier;
        frame.extended_identifier = ecu->extended_identifier;
        frame.data[0] = route->ecu_command_id;
        frame.data_length =
            static_cast<uint8_t>(message.payload_length + 1U);

        if (message.payload_length > 0)
        {
            std::memcpy(
                &frame.data[1],
                message.payload,
                message.payload_length);
        }

        return RouteResult::Ok;
    }

    const char *to_string(RouteResult result)
    {
        switch (result)
        {
        case RouteResult::Ok:
            return "Ok";
        case RouteResult::InvalidConfiguration:
            return "InvalidConfiguration";
        case RouteResult::InvalidMessageType:
            return "InvalidMessageType";
        case RouteResult::UnknownCommand:
            return "UnknownCommand";
        case RouteResult::InvalidPayloadLength:
            return "InvalidPayloadLength";
        case RouteResult::InvalidPayloadValue:
            return "InvalidPayloadValue";
        default:
            return "Unknown";
        }
    }
}
