#ifndef _OTADRIVE_ESP_H_
#define _OTADRIVE_ESP_H_

#include <Arduino.h>
#include <FS.h>
#include "KeyValueList.h"
#include "otadrive_cert.h"
#include "FlashUpdater.h"
#include "types.h"

#ifdef ESP8266
#include <LittleFS.h>
#define OTA_FILE_SYS LittleFS
#elif defined(ESP32)
#include <SPIFFS.h>
#define OTA_FILE_SYS SPIFFS

using fs::File;
using fs::FS;
#endif

#ifndef OTADRIVE_URL
#define OTADRIVE_URL "http://otadrive.com/deviceapi/"
#endif
#define OTADRIVE_SDK_VER "22"

class otadrive_ota;
class updateInfo;

class updateInfo : public Printable
{
    const char *code_str() const;
    String old_version;

public:
    bool available;
    String version;
    int size;
    update_result code;

    virtual size_t printTo(Print &p) const;
    String toString() const;

    friend class otadrive_ota;
};

class device_status
{
    float battery_voltage;
    bool power_plugged;
    double latitude;
    double longitude;
};

class otadrive_ota
{
private:
    uint32_t tickTimestamp = 0;
    const uint16_t TIMEOUT_MS = 10000;
    bool MD5_Match = true;
    bool _useSSL;

    String cutLine(String &str);
    String serverUrl(String uri);
    String baseParams();
    bool download(Client &client, String url, File *file, String *outStr);
    // update_result head(String url, String &resultStr, const char *reqHdrs[1], uint8_t reqHdrsCount);
    String file_md5(File &f);
    String downloadResourceList(Client &client);

    static void updateFirmwareProgress(int progress, int totalt);
    String force_chipId;

public:
    String ProductKey;
    String Version;

    typedef std::function<void(size_t, size_t)> THandlerFunction_Progress;
    FS *fileObj;

    otadrive_ota();
    void setInfo(String ApiKey, String Version);
    String getChipId();
    void setChipId(String id);
    void useSSL(bool ssl);
    
    bool sendAlive();
    bool sendAlive(Client &client);

    void useMD5Matcher(bool useMd5);
    updateInfo updateFirmware(bool reboot = true);
    updateInfo updateFirmware(Client &client, bool reboot = true);
    updateInfo updateFirmwareInfo();
    updateInfo updateFirmwareInfo(Client &client);
    void onUpdateFirmwareProgress(THandlerFunction_Progress fn);

    bool syncResources();
    bool syncResources(Client &client);
    void setFileSystem(FS *fileObj);

    [[deprecated("Use getJsonConfigs().")]]    
    String getConfigs();
    [[deprecated("Use getJsonConfigs(Client).")]]    
    String getConfigs(Client &client);

    String getJsonConfigs();
    String getJsonConfigs(Client &client);

    OTAdrive_ns::KeyValueList getConfigValues();
    OTAdrive_ns::KeyValueList getConfigValues(Client &client);

    bool timeTick(uint16_t seconds);

private:
    static THandlerFunction_Progress _progress_callback;
};

extern otadrive_ota OTADRIVE;

#define otd_pre ""

#ifdef DEBUGV
#define otd_log_v(format, ...) DEBUGV("[V]" otd_pre format "\n", ##__VA_ARGS__)
#define otd_log_d(format, ...) DEBUGV("[D]" otd_pre format "\n", ##__VA_ARGS__)
#define otd_log_i(format, ...) DEBUGV("[I]" otd_pre format "\n", ##__VA_ARGS__)
#define otd_log_e(format, ...) DEBUGV("[E]" otd_pre format "\n", ##__VA_ARGS__)
#elif defined(ESP8266)
#if ARDUHAL_LOG_LEVEL >= 5
#define otd_log_v(format, ...) Serial.printf("[V]" otd_pre format "\n", ##__VA_ARGS__)
#else
#define otd_log_v(format, ...)
#endif
#if ARDUHAL_LOG_LEVEL >= 4
#define otd_log_d(format, ...) Serial.printf("[D]" otd_pre format "\n", ##__VA_ARGS__)
#else
#define otd_log_d(format, ...)
#endif
#if ARDUHAL_LOG_LEVEL >= 3
#define otd_log_i(format, ...) Serial.printf("[I]" otd_pre format "\n", ##__VA_ARGS__)
#else
#define otd_log_i(format, ...)
#endif
#if ARDUHAL_LOG_LEVEL >= 1
#define otd_log_e(format, ...) Serial.printf("[E]" otd_pre format "\n", ##__VA_ARGS__)
#else
#define otd_log_e(format, ...)
#endif
// end ESP8266
#elif defined(ESP32)
#define otd_log_v(format, ...) log_v(format, ##__VA_ARGS__)
#define otd_log_d(format, ...) log_d(format, ##__VA_ARGS__)
#define otd_log_i(format, ...) log_i(format, ##__VA_ARGS__)
#define otd_log_e(format, ...) log_e(format, ##__VA_ARGS__)
// end ESP32
#endif

#endif
