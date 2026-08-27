// main/include/interfaces/i_command_handler.hpp
#pragma once

#include <cstdint>

/**
 * @struct CommandProcessResult
 * @brief Result flags returned after draining and processing incoming ESP-NOW commands.
 */
struct CommandProcessResult
{
    bool time_synced{false};      ///< True if clock was synchronized from incoming SYNC_TIME packet
    bool ota_requested{false};    ///< True if START_OTA command was received
    bool reboot_requested{false}; ///< True if REBOOT command was received
};

/**
 * @class ICommandHandler
 * @brief Interface for processing incoming ESP-NOW commands for the SolarSensor node.
 */
class ICommandHandler
{
public:
    virtual ~ICommandHandler() = default;

    /**
     * @brief Drains all pending messages in rx_queue and processes commands.
     * @return CommandProcessResult struct with status flags.
     */
    virtual CommandProcessResult process() = 0;
};
