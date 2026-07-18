#include "can_manager/can_manager.h"

#include "esp_log.h"

namespace
{
constexpr char TAG[] = "CanManager";
}

CanManager::CanManager(const Config &config)
    : config_(config), driver_(&spi_)
{
}

CanManager::CanManager()
    : CanManager(Config{})
{
}

esp_err_t CanManager::begin()
{
    if (initialized_)
    {
        return ESP_OK;
    }

    spi_bus_config_t bus_config = {};
    bus_config.mosi_io_num = config_.mosi;
    bus_config.miso_io_num = config_.miso;
    bus_config.sclk_io_num = config_.sclk;
    bus_config.quadwp_io_num = -1;
    bus_config.quadhd_io_num = -1;
    bus_config.max_transfer_sz = 32;

    esp_err_t err = spi_bus_initialize(config_.spi_host, &bus_config,
                                       SPI_DMA_CH_AUTO);
    if (err != ESP_OK)
    {
        return err;
    }

    spi_device_interface_config_t device_config = {};
    device_config.clock_speed_hz = config_.clock_hz;
    device_config.mode = 0;
    device_config.spics_io_num = config_.chip_select;
    device_config.queue_size = 7;

    err = spi_bus_add_device(config_.spi_host, &device_config, &spi_);
    if (err != ESP_OK)
    {
        spi_bus_free(config_.spi_host);
        return err;
    }

    if (driver_.reset() != MCP2515::ERROR_OK ||
        driver_.setBitrate(config_.bitrate, config_.oscillator) != MCP2515::ERROR_OK ||
        driver_.setNormalMode() != MCP2515::ERROR_OK)
    {
        ESP_LOGW(TAG, "MCP2515 not detected; CAN is disabled");
        spi_bus_remove_device(spi_);
        spi_ = nullptr;
        spi_bus_free(config_.spi_host);
        return ESP_FAIL;
    }

    initialized_ = true;
    ESP_LOGI(TAG, "MCP2515 initialized");
    return ESP_OK;
}

CanManager::TransmitResult CanManager::send(
    const can_frame &frame)
{
    if (!initialized_)
    {
        return TransmitResult::NotInitialized;
    }

    if (frame.can_dlc > CAN_MAX_DLEN)
    {
        return TransmitResult::InvalidFrame;
    }

    can_frame mutable_frame = frame;
    const MCP2515::ERROR result = driver_.sendMessage(
        &mutable_frame,
        config_.transmit_timeout_ms);

    switch (result)
    {
    case MCP2515::ERROR_OK:
        return TransmitResult::Ok;
    case MCP2515::ERROR_ALLTXBUSY:
        return TransmitResult::Busy;
    case MCP2515::ERROR_TXTIMEOUT:
        return TransmitResult::Timeout;
    case MCP2515::ERROR_FAILTX:
        return TransmitResult::Failed;
    default:
        return TransmitResult::Failed;
    }
}
