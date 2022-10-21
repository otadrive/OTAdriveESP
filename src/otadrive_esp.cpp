#include "otadrive_esp.h"
#include "FlashUpdater.h"
#include "tinyHTTP.h"

otadrive_ota OTADRIVE;
otadrive_ota::THandlerFunction_Progress otadrive_ota::_progress_callback = nullptr;

otadrive_ota::otadrive_ota()
{
#ifdef OTA_FILE_SYS
    setFileSystem(&OTA_FILE_SYS);
#endif
}

/**
 * Sets ApiKey and current firmware version of your device
 *
 */
void otadrive_ota::setInfo(String ApiKey, String Version)
{
    this->ProductKey = ApiKey;
    this->Version = Version;
}

String otadrive_ota::baseParams()
{
    return "k=" + ProductKey + "&v=" + Version + "&s=" + getChipId();
}

/**
 * Returns your chip ID
 *
 * @return string of your unique chipID
 */
String otadrive_ota::getChipId()
{
#ifdef ESP8266
    return String(ESP.getChipId());
#elif defined(ESP32)
    String ChipIdHex = String((uint32_t)(ESP.getEfuseMac() >> 32), HEX);
    ChipIdHex += String((uint32_t)ESP.getEfuseMac(), HEX);
    return ChipIdHex;
#endif
}

String otadrive_ota::downloadResourceList(Client &client)
{
    String url = OTADRIVE_URL "resource/list?plain&";
    url += baseParams();

    String res;
    download(client, url, nullptr, &res);
    return res;
}

// update_result otadrive_ota::head(String url, String &resultStr, const char *reqHdrs[1], uint8_t reqHdrsCount)
// {
//     WiFiClient client;
//     HTTPClient http;

//     client.setTimeout(TIMEOUT_MS / 1000);
// #ifdef ESP32
//     http.setConnectTimeout(TIMEOUT_MS);
// #endif
//     http.setTimeout(TIMEOUT_MS);

//     if (http.begin(client, url))
//     {
//         http.collectHeaders(reqHdrs, reqHdrsCount);
//         int httpCode = http.sendRequest("HEAD");

//         // httpCode will be negative on error
//         if (httpCode == HTTP_CODE_OK)
//         {
//             String hdrs = "";
//             for (uint8_t i = 0; i < http.headers(); i++)
//                 hdrs += http.headerName(i) + ": " + http.header(i) + "\n";
//             resultStr = hdrs;
//             return update_result::Success;
//         }
//         else
//         {
//             otd_log_e("downloaded error %d, %s", httpCode, http.errorToString(httpCode).c_str());
//             if (httpCode == 404)
//                 return update_result::NoFirmwareExists;
//             else if (httpCode == 304)
//                 return update_result::AlreadyUpToDate;
//             else if (httpCode == 401)
//                 return update_result::DeviceUnauthorized;
//             return update_result::ConnectError;
//         }
//     }

//     resultStr = "";
//     return update_result::ConnectError;
// }

bool otadrive_ota::download(Client &client, String url, File *file, String *outStr)
{
    OTAdrive::TinyHTTP http(client);
    client.setTimeout(TIMEOUT_MS / 1000);

    if (http.get(url))
    {
        // httpCode will be negative on error
        if (http.resp_code == HTTP_CODE_OK)
        {
            if (file)
            {
                int n = 0;
                uint8_t wbuf[256];
                while (http.client.available())
                {
                    int rd = http.client.readBytes(wbuf, sizeof(wbuf));
                    n += file->write(wbuf, rd);
                }

                otd_log_i("%d bytes downloaded and writted to file : %s", n, file->name());
                return true;
            }

            if (outStr)
            {
                *outStr = http.client.readString();
                return true;
            }
        }
        else
        {
            otd_log_e("downloaded error code %d, %s", http.resp_code, http.client.readString().c_str());
        }
    }

    return false;
}

/**
 * Call update API of the OTAdrive server and gets information about new firmware
 *
 * @return updateInfo object, contains information about new version on server
 */
void otadrive_ota::setFileSystem(FS *fileObj)
{
    this->fileObj = fileObj;
}

bool otadrive_ota::syncResources()
{
    WiFiClient client;
    return syncResources(client);
}
/**
 * Call resource API of the OTAdrive server and sync local files with server
 *
 * @return returns success if no error happens in the procedure
 */
bool otadrive_ota::syncResources(Client &client)
{
    if (fileObj == nullptr)
    {
        otd_log_e("file system doesn't set, call setFileSystem() in setup");
        return false;
    }

    String list = downloadResourceList(client);
    String baseurl = OTADRIVE_URL "resource/get?";
    baseurl += baseParams();

    while (list.length())
    {
        // extract file info from webAPI
        String fn = cutLine(list);
        String fk = cutLine(list);
        String md5 = cutLine(list);
        if (!fn.startsWith("/"))
            fn = "/" + fn;

        otd_log_d("file data: %s, MD5=%s", fn.c_str(), md5.c_str());

        // check local file MD5 if exists
        String md5_local = "";
        if (fileObj->exists(fn))
        {
            File file = fileObj->open(fn, "r");
            md5_local = file_md5(file);
            file.close();
        }

        // compare local and server file checksum
        if (md5_local == md5)
        {
            otd_log_i("local MD5 is match for %s", fn.c_str());
            continue;
        }
        else
        {
            // lets download and replace file
            otd_log_i("MD5 not match for %s, lets download", fn.c_str());
            File file = fileObj->open(fn, "w+");
            if (!file)
            {
                otd_log_e("Faild to create file\n");
                return false;
            }

            download(client, baseurl + "&fk=" + fk, &file, nullptr);
            file.close();
        }
    }

    return true;
}

bool otadrive_ota::sendAlive()
{
    WiFiClient client;
    return sendAlive(client);
}
/**
 * Call alive API of the OTAdrive server and sends some device info
 *
 * @return returns success if no error happens in procedure
 */
bool otadrive_ota::sendAlive(Client &client)
{
    String url = OTADRIVE_URL "alive?";
    url += baseParams();
    return download(client, url, nullptr, nullptr);
}

#ifdef ESP32
updateInfo otadrive_ota::updateFirmware(Client &client, bool reboot)
{
    updateInfo inf = updateFirmwareInfo(client);
    if (!inf.available)
    {
        otd_log_i("No new firmware available");
        return inf;
    }

    String url = OTADRIVE_URL "update?";
    url += baseParams();

    OTAdrive::FlashUpdater updater;
    updater.rebootOnUpdate(false);
    updater.onProgress(updateFirmwareProgress);
    t_httpUpdate_return ret = updater.update(client, url);

    switch (ret)
    {
    case HTTP_UPDATE_FAILED:
        otd_log_i("HTTP_UPDATE_FAILED Error (%d): %s\n", updateObj.getLastError(), updateObj.getLastErrorString().c_str());
        break;

    case HTTP_UPDATE_NO_UPDATES:
        otd_log_i("HTTP_UPDATE_NO_UPDATES");
        break;

    case HTTP_UPDATE_OK:
    {
        Version = inf.version;
        sendAlive(client);
        otd_log_i("HTTP_UPDATE_OK");
        if (reboot)
            ESP.restart();
        break;
    }
    default:
        otd_log_i("HTTP_UPDATE_CODE: %d", ret);
        break;
    }

    return inf;
}
#endif

/**
 * Call update API of the OTAdrive server and download new firmware version if available
 * If new version download you never get out of this function. MCU will reboot
 */
updateInfo otadrive_ota::updateFirmware(bool reboot)
{
    updateInfo inf = updateFirmwareInfo();

    if (!inf.available)
    {
        otd_log_i("No new firmware available");
        return inf;
    }

    String url = OTADRIVE_URL "update?";
    url += baseParams();

    WiFiClient client;

    Update.onProgress(updateFirmwareProgress);
    updateObj.rebootOnUpdate(false);
    t_httpUpdate_return ret = updateObj.update(client, url, Version);

    switch (ret)
    {
    case HTTP_UPDATE_FAILED:
        otd_log_i("HTTP_UPDATE_FAILED Error (%d): %s\n", updateObj.getLastError(), updateObj.getLastErrorString().c_str());
        break;

    case HTTP_UPDATE_NO_UPDATES:
        otd_log_i("HTTP_UPDATE_NO_UPDATES");
        break;

    case HTTP_UPDATE_OK:
    {
        Version = inf.version;
        sendAlive();
        otd_log_i("HTTP_UPDATE_OK");
        if (reboot)
            ESP.restart();
        break;
    }
    default:
        otd_log_i("HTTP_UPDATE_CODE: %d", ret);
        break;
    }

    return inf;
}

/**
 * Set callback for onProgress during firmware update
 *
 */
void otadrive_ota::onUpdateFirmwareProgress(THandlerFunction_Progress fn)
{
    _progress_callback = fn;
}

void otadrive_ota::updateFirmwareProgress(int progress, int totalt)
{
    if (_progress_callback != nullptr)
        _progress_callback(progress, totalt);
}

updateInfo otadrive_ota::updateFirmwareInfo()
{
    WiFiClient client;
    return updateFirmwareInfo(client);
}
/**
 * Call update API of the OTAdrive server and gets information about new firmware
 *
 * @return updateInfo object, contains information about new version on server
 */
updateInfo otadrive_ota::updateFirmwareInfo(Client &client)
{
    updateInfo inf;

    String url = OTADRIVE_URL "update?";
    url += baseParams();

    OTAdrive::TinyHTTP http(client);
    http.get(url, 0, 0);

    otd_log_i("heads [%d] \n", (int)http.resp_code);

    inf.available = false;
    inf.code = update_result::ConnectError;
    inf.version = http.xVersion;
    inf.size = http.total_len;

    switch (http.resp_code)
    {
    case 0:
        otd_log_e("connect error\n");
        inf.code = update_result::ConnectError;
        break;

    case HTTP_CODE_NOT_FOUND:
        otd_log_i("not found error\n");
        inf.code = update_result::NoFirmwareExists;
        break;

    case HTTP_CODE_NOT_MODIFIED:
        otd_log_i("no new firmware\n");
        inf.code = update_result::AlreadyUpToDate;
        break;

    case HTTP_CODE_UNAUTHORIZED:
        otd_log_e("unauthorized error\n");
        inf.code = update_result::DeviceUnauthorized;
        break;

    case HTTP_CODE_OK:
        inf.available = inf.version != Version;
        inf.code = update_result::Success;
        break;
    }

    return inf;
}

String otadrive_ota::getConfigs()
{
    WiFiClient client;
    return getConfigs(client);
}
/**
 * Call configuration API of the OTAdrive server and gets device configuration as string
 *
 * @return configuration of device as String
 */
String otadrive_ota::getConfigs(Client &client)
{
    String url = OTADRIVE_URL "getconfig?";
    url += baseParams();

    String conf;
    download(client, url, nullptr, &conf);

    return conf;
}

size_t updateInfo::printTo(Print &p) const
{
    String s = toString();
    return p.print(s);
}

String updateInfo::toString() const
{
    if (code == update_result::AlreadyUpToDate)
    {
        return String("Firmware already uptodate.\n");
    }
    else if (code == update_result::Success)
    {
        char t[64];
        sprintf(t, "Firmware update from %s to %s.\n", old_version.c_str(), version.c_str());
        return String(t);
    }
    if (code == update_result::DeviceUnauthorized)
    {
        return String("Device Unauthorized. Change the device state on OTAdrive.\n");
    }
    else
    {
        char t[32];
        sprintf(t, "OTA update Faild, %s.\n", code_str());
        return String(t);
    }
}

const char *updateInfo::code_str() const
{
    static const char *messages[] = {
        "Connect Error", "Device Unauthorized",
        "Already UpToDate", "No Firmware Exists",
        "Success", "Unkwon"};
    switch (code)
    {
    case update_result::Success:
        return messages[4];
    case update_result::ConnectError:
        return messages[0];
    case update_result::DeviceUnauthorized:
        return messages[1];
    case update_result::AlreadyUpToDate:
        return messages[2];
    case update_result::NoFirmwareExists:
        return messages[3];

    default:
        return messages[5];
        break;
    }
}

bool otadrive_ota::timeTick(uint16_t seconds)
{
    if (millis() > tickTimestamp)
    {
        tickTimestamp = millis() + ((uint32_t)seconds) * 1000;
        return true;
    }
    return false;
}