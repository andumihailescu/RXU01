#include "esp_now_can_gateway/esp_now_can_gateway.h"

namespace
{
    esp_now_can_gateway::Config g_config{};
    esp_now_can_gateway::Statistics g_statistics{};
    bool g_initialized = false;

    esp_now_can_gateway::ProcessResult map_route_result(
        can_command_router::RouteResult result)
    {
        using RouteResult = can_command_router::RouteResult;
        using ProcessResult = esp_now_can_gateway::ProcessResult;

        switch (result)
        {
        case RouteResult::Ok:
            return ProcessResult::Ok;
        case RouteResult::InvalidConfiguration:
            return ProcessResult::CanTransmitFailed;
        case RouteResult::InvalidMessageType:
            return ProcessResult::UnsupportedMessage;
        case RouteResult::UnknownCommand:
            return ProcessResult::UnknownCommand;
        case RouteResult::InvalidPayloadLength:
            return ProcessResult::InvalidPayloadLength;
        case RouteResult::InvalidPayloadValue:
            return ProcessResult::InvalidPayloadValue;
        default:
            return ProcessResult::InvalidMessage;
        }
    }

    esp_now_can_gateway::ProcessResult map_transmit_result(
        esp_now_can_gateway::TransmitResult result)
    {
        using TransmitResult = esp_now_can_gateway::TransmitResult;
        using ProcessResult = esp_now_can_gateway::ProcessResult;

        switch (result)
        {
        case TransmitResult::Ok:
            return ProcessResult::Ok;
        case TransmitResult::Busy:
            return ProcessResult::CanBusy;
        case TransmitResult::Timeout:
            return ProcessResult::CanTimeout;
        case TransmitResult::Failed:
        default:
            return ProcessResult::CanTransmitFailed;
        }
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

        if (config.transmit == nullptr ||
            !can_command_router::is_valid_config(
                config.command_router))
        {
            return ESP_ERR_INVALID_ARG;
        }

        g_config = config;
        reset_statistics();
        g_initialized = true;

        return ESP_OK;
    }

    esp_err_t deinit()
    {
        g_initialized = false;
        g_config = {};
        reset_statistics();

        return ESP_OK;
    }

    bool is_initialized()
    {
        return g_initialized;
    }

    ProcessResult process_message(
        const remote_protocol::Message &message)
    {
        if (!g_initialized)
        {
            return ProcessResult::CanTransmitFailed;
        }

        if (!remote_protocol::is_valid_message_type(message.type) ||
            !remote_protocol::are_valid_flags(message.flags) ||
            (message.flags &
             remote_protocol::FlagIsResponse) != 0 ||
            message.payload_length >
                remote_protocol::MAX_PAYLOAD_SIZE)
        {
            ++g_statistics.validation_errors;
            return ProcessResult::InvalidMessage;
        }

        CanFrame frame{};
        const can_command_router::RouteResult route_result =
            can_command_router::build_frame(
                message,
                g_config.command_router,
                frame);

        if (route_result != can_command_router::RouteResult::Ok)
        {
            ++g_statistics.validation_errors;
            return map_route_result(route_result);
        }

        ++g_statistics.commands_routed;

        const TransmitResult transmit_result =
            g_config.transmit(
                frame,
                g_config.transmit_context);

        if (transmit_result == TransmitResult::Ok)
        {
            ++g_statistics.frames_sent;
        }
        else
        {
            ++g_statistics.transmit_errors;
        }

        return map_transmit_result(transmit_result);
    }

    const char *to_string(ProcessResult result)
    {
        switch (result)
        {
        case ProcessResult::Ok:
            return "Ok";
        case ProcessResult::InvalidMessage:
            return "InvalidMessage";
        case ProcessResult::UnsupportedMessage:
            return "UnsupportedMessage";
        case ProcessResult::UnknownCommand:
            return "UnknownCommand";
        case ProcessResult::InvalidPayloadLength:
            return "InvalidPayloadLength";
        case ProcessResult::InvalidPayloadValue:
            return "InvalidPayloadValue";
        case ProcessResult::CanBusy:
            return "CanBusy";
        case ProcessResult::CanTransmitFailed:
            return "CanTransmitFailed";
        case ProcessResult::CanTimeout:
            return "CanTimeout";
        default:
            return "Unknown";
        }
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
