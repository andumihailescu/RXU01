#pragma once

#include <cstdint>

#include "can_command_router/can_command_router.h"

namespace vehicle_can_protocol
{
    /**
     * Roluri logice din retea. Valorile devin namespace-ul superior al
     * identificatorilor ESP-NOW si nu depind de modelul fizic al ECU-ului.
     */
    enum class EcuRole : uint8_t
    {
        Lighting = 0x01
    };

    constexpr uint16_t make_remote_message_id(
        EcuRole destination,
        uint8_t command)
    {
        return static_cast<uint16_t>(
            (static_cast<uint16_t>(destination) << 8U) |
            command);
    }

    /**
     * ID-uri logice folosite intre TXU01 si RXU01.
     * Aceste valori nu sunt identificatori CAN.
     * Byte-ul superior identifica rolul logic al ECU-ului destinatar.
     */
    enum class RemoteCommandId : uint16_t
    {
        LightingGetSoftwareVersion =
            make_remote_message_id(EcuRole::Lighting, 0x01),
        LightingSetIndicatorState =
            make_remote_message_id(EcuRole::Lighting, 0x03),
        LightingSetExteriorLightsState =
            make_remote_message_id(EcuRole::Lighting, 0x04),
        LightingSetBrakeState =
            make_remote_message_id(EcuRole::Lighting, 0x05),
        LightingSetReverseLightState =
            make_remote_message_id(EcuRole::Lighting, 0x06)
    };

    /**
     * ID provizoriu pentru comenzile primite de LMCU100.
     * LMCU100 foloseste momentan 0x100 pentru raspunsul de versiune.
     */
    constexpr uint16_t LMCU100_COMMAND_CAN_ID = 0x101;
    constexpr uint16_t LMCU100_SOFTWARE_VERSION_CAN_ID = 0x100;

    enum class Lmcu100CommandId : uint8_t
    {
        GetSoftwareVersion = 0x01,
        Reserved = 0x02,
        SetIndicatorState = 0x03,
        SetExteriorLightsState = 0x04,
        SetBrakeState = 0x05,
        SetReverseLightState = 0x06
    };

    enum class IndicatorStateField : uint8_t
    {
        Warning = 0,
        Left = 1,
        Right = 2
    };

    enum class BinaryState : uint8_t
    {
        Off = 0x00,
        On = 0x01
    };

    constexpr uint8_t INDICATOR_STATE_PAYLOAD_LENGTH = 3;

    enum class ExteriorLightMode : uint8_t
    {
        Off = 0,
        Drl = 1,
        Positions = 2,
        LowBeam = 3
    };

    enum class ExteriorLightStateField : uint8_t
    {
        Mode = 0,
        FrontProjectors = 1,
        FogLights = 2,
        HighBeam = 3
    };

    constexpr uint8_t EXTERIOR_LIGHT_STATE_PAYLOAD_LENGTH = 4;
    constexpr uint8_t BRAKE_STATE_PAYLOAD_LENGTH = 1;
    constexpr uint8_t REVERSE_LIGHT_STATE_PAYLOAD_LENGTH = 1;

    static_assert(
        static_cast<uint16_t>(
            RemoteCommandId::LightingSetIndicatorState) == 0x0103);
    static_assert(
        static_cast<uint16_t>(
            RemoteCommandId::LightingSetExteriorLightsState) == 0x0104);
    static_assert(
        static_cast<uint16_t>(
            RemoteCommandId::LightingSetBrakeState) == 0x0105);
    static_assert(
        static_cast<uint16_t>(
            RemoteCommandId::LightingSetReverseLightState) == 0x0106);

    /**
     * Returneaza registry-ul ECU si tabela de comenzi ale masinii.
     * Memoria referita de configuratie este statica.
     */
    can_command_router::Config make_command_router_config();
}
