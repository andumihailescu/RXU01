#include "rxu01_router/rxu01_router.h"

#include "esp_now_can_gateway/esp_now_can_gateway.h"

namespace
{
    rxu01_router::Config g_config{};
    rxu01_router::Statistics g_statistics{};

    bool g_initialized = false;

    esp_err_t deliverLocally(
        const remote_protocol::Message &message)
    {
        if (g_config.local_handler == nullptr)
        {
            return ESP_ERR_NOT_SUPPORTED;
        }

        const esp_err_t result =
            g_config.local_handler(
                message,
                g_config.local_context);

        if (result == ESP_OK)
        {
            ++g_statistics.local_messages;
        }

        return result;
    }

    esp_err_t forwardToCan(
        const remote_protocol::Message &message)
    {
        if (!esp_now_can_gateway::
                is_initialized())
        {
            return ESP_ERR_INVALID_STATE;
        }

        const esp_err_t result =
            esp_now_can_gateway::
                process_message(message);

        if (result == ESP_OK)
        {
            ++g_statistics.can_messages;
        }

        return result;
    }
}

namespace rxu01_router
{
    esp_err_t init(const Config &config)
    {
        if (g_initialized)
        {
            return ESP_OK;
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

    esp_err_t process_packet(
        const uint8_t *packet,
        std::size_t packet_length)
    {
        if (!g_initialized)
        {
            return ESP_ERR_INVALID_STATE;
        }

        remote_protocol::Message message{};

        const remote_protocol::DecodeResult
            decode_result =
                remote_protocol::decode(
                    packet,
                    packet_length,
                    message);

        if (decode_result !=
            remote_protocol::
                DecodeResult::Ok)
        {
            ++g_statistics.invalid_packets;
            ++g_statistics.routing_errors;

            return ESP_ERR_INVALID_RESPONSE;
        }

        ++g_statistics.valid_packets;

        return process_message(message);
    }

    esp_err_t process_message(
        const remote_protocol::Message &message)
    {
        if (!g_initialized)
        {
            return ESP_ERR_INVALID_STATE;
        }

        esp_err_t result = ESP_OK;

        switch (message.destination)
        {
        case remote_protocol::
            Destination::Rxu01:
            result =
                deliverLocally(message);
            break;

        case remote_protocol::
            Destination::Broadcast:
        {
            bool handled = false;

            if (g_config
                    .deliver_broadcast_locally &&
                g_config.local_handler != nullptr)
            {
                result =
                    deliverLocally(message);

                if (result != ESP_OK)
                {
                    ++g_statistics.routing_errors;
                    return result;
                }

                handled = true;
            }

            if (g_config
                    .forward_broadcast_to_can)
            {
                result =
                    forwardToCan(message);

                if (result != ESP_OK)
                {
                    ++g_statistics.routing_errors;
                    return result;
                }

                handled = true;
            }

            if (!handled)
            {
                result =
                    ESP_ERR_NOT_SUPPORTED;
            }

            break;
        }

        default:
            result =
                forwardToCan(message);
            break;
        }

        if (result != ESP_OK)
        {
            ++g_statistics.routing_errors;
        }

        return result;
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
