#include <Arduino.h>
#pragma once

enum class update_result : int
{
    ConnectError = 0,
    DeviceUnauthorized = 401,
    AlreadyUpToDate = 304,
    NoFirmwareExists = 404,
    Success = 200
};

enum FotaResult {
    FOTA_UPDATE_FAILED,
    FOTA_UPDATE_NO_UPDATES,
    FOTA_UPDATE_OK
};