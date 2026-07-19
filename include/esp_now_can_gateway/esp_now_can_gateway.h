#pragma once

#include <cstddef>
#include <cstdint>

#include "esp_err.h"

#include "can_command_router/can_command_router.h"
#include "remote_protocol/remote_protocol.h"

namespace esp_now_can_gateway
{
    using CanFrame = can_command_router::CanFrame;

    enum class TransmitResult
    {
        Ok,
        Busy,
        Failed,
        Timeout
    };

    enum class ProcessResult
    {
        Ok,
        InvalidMessage,
        UnsupportedMessage,
        UnknownCommand,
        InvalidPayloadLength,
        InvalidPayloadValue,
        CanBusy,
        CanTransmitFailed,
        CanTimeout
    };

    /**
     * Main-ul va adapta acest callback la
     * metoda reala din driverul MCP2515.
     */
    using TransmitCallback =
        TransmitResult (*)(
            const CanFrame &frame,
            void *context);

    struct Config
    {
        TransmitCallback transmit =
            nullptr;

        void *transmit_context =
            nullptr;

        can_command_router::Config command_router{};
    };

    struct Statistics
    {
        uint32_t commands_routed = 0;
        uint32_t frames_sent = 0;
        uint32_t validation_errors = 0;
        uint32_t transmit_errors = 0;
    };

    /**
     * Nu este thread-safe.
     * Apeleaza componenta dintr-un singur task.
     */
    esp_err_t init(const Config &config);

    esp_err_t deinit();

    bool is_initialized();

    ProcessResult process_message(
        const remote_protocol::Message &message);

    const char *to_string(ProcessResult result);

    Statistics get_statistics();

    void reset_statistics();
}
