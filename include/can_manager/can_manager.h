#pragma once

#include <cstdint>

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_err.h"
#include "mcp2515/mcp2515.h"

class CanManager
{
public:
    enum class TransmitResult
    {
        Ok,
        NotInitialized,
        InvalidFrame,
        Busy,
        Failed,
        Timeout
    };

    struct Config
    {
        gpio_num_t miso = GPIO_NUM_5;
        gpio_num_t mosi = GPIO_NUM_6;
        gpio_num_t sclk = GPIO_NUM_4;
        gpio_num_t chip_select = GPIO_NUM_7;
        spi_host_device_t spi_host = SPI2_HOST;
        int clock_hz = 1000000;
        CAN_SPEED bitrate = CAN_1000KBPS;
        CAN_CLOCK oscillator = MCP_8MHZ;
        uint32_t transmit_timeout_ms = 50;
    };

    CanManager();
    explicit CanManager(const Config &config);

    esp_err_t begin();
    TransmitResult send(const can_frame &frame);

private:
    Config config_;
    spi_device_handle_t spi_ = nullptr;
    MCP2515 driver_;
    bool initialized_ = false;
};
