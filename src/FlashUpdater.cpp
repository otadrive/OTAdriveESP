#ifdef ESP32
#include "FlashUpdater.h"
#include <StreamString.h>
#include "tinyHTTP.h"
#include <esp_partition.h>
#include <esp_ota_ops.h> // get running partition
#include <otadrive_esp.h>

using namespace OTAdrive_ns;

// To do extern "C" uint32_t _SPIFFS_start;
// To do extern "C" uint32_t _SPIFFS_end;

FlashUpdater::FlashUpdater(void)
    : _httpClientTimeout(8000), _ledPin(-1)
{
    _followRedirects = HTTPC_DISABLE_FOLLOW_REDIRECTS;
}

FlashUpdater::FlashUpdater(int httpClientTimeout)
    : _httpClientTimeout(httpClientTimeout), _ledPin(-1)
{
    _followRedirects = HTTPC_DISABLE_FOLLOW_REDIRECTS;
}

FlashUpdater::~FlashUpdater(void)
{
}

HTTPUpdateResult FlashUpdater::update(Client &client, const String &url)
{
    return handleUpdate(client, url);
}

/**
 * return error code as int
 * @return int error code
 */
int FlashUpdater::getLastError(void)
{
    return _lastError;
}

/**
 * return error code as String
 * @return String error
 */
String FlashUpdater::getLastErrorString(void)
{

    if (_lastError == 0)
    {
        return String(); // no error
    }

    // error from Update class
    if (_lastError > 0)
    {
        StreamString error;
        Update.printError(error);
        error.trim(); // remove line ending
        return String("Update error: ") + error;
    }

    // error from http client
    if (_lastError > -100)
    {
        return String("HTTP error: ") + String(_lastError);
    }

    switch (_lastError)
    {
    case HTTP_UE_TOO_LESS_SPACE:
        return "Not Enough space";
    case HTTP_UE_SERVER_NOT_REPORT_SIZE:
        return "Server Did Not Report Size";
    case HTTP_UE_SERVER_FILE_NOT_FOUND:
        return "File Not Found (404)";
    case HTTP_UE_SERVER_FORBIDDEN:
        return "Forbidden (403)";
    case HTTP_UE_SERVER_WRONG_HTTP_CODE:
        return "Wrong HTTP Code";
    case HTTP_UE_SERVER_FAULTY_MD5:
        return "Wrong MD5";
    case HTTP_UE_BIN_VERIFY_HEADER_FAILED:
        return "Verify Bin Header Failed";
    case HTTP_UE_BIN_FOR_WRONG_FLASH:
        return "New Binary Does Not Fit Flash Size";
    case HTTP_UE_NO_PARTITION:
        return "Partition Could Not be Found";
    }

    return String();
}

String FlashUpdater::createHeaders(void)
{
    String hdr = "";
    hdr = "\r\nUser-Agent: ESP32-http-Update"
          "\r\nCache-Control: no-cache";
    hdr += "\r\nx-ESP32-STA-MAC: " + WiFi.macAddress();
    hdr += "\r\nx-ESP32-AP-MAC: " + WiFi.softAPmacAddress();
    hdr += "\r\nx-ESP32-free-space: " + String(ESP.getFreeSketchSpace());
    hdr += "\r\nx-ESP32-sketch-size: " + String(ESP.getSketchSize());
    if (MD5_Match)
        hdr += "\r\nx-check-sketch-md5: 1";

    String sketchMD5 = ESP.getSketchMD5();
    if (sketchMD5.length() != 0)
    {
        hdr += "\r\nx-ESP32-sketch-md5: " + sketchMD5;
    }

    hdr += "\r\nx-ESP32-chip-size: " + String(ESP.getFlashChipSize());
    hdr += "\r\nx-ESP32-sdk-version: " + String(ESP.getSdkVersion());
    hdr += "\r\nx-ESP32-mode: sketch";

    return hdr;
}

/**
 *
 * @param http GsmHttpClient *
 * @param currentVersion const char *
 * @return HTTPUpdateResult
 */
HTTPUpdateResult FlashUpdater::handleUpdate(Client &client, const String &url)
{
    HTTPUpdateResult ret = HTTPUpdateResult::HTTP_UPDATE_FAILED;
    TinyHTTP http(client);
    http.user_headers = createHeaders();

    if (!http.get(url, 0, 16))
    {
        if (http.resp_code == HTTP_CODE_NOT_MODIFIED)
            return HTTP_UPDATE_NO_UPDATES;
        return HTTP_UPDATE_FAILED;
    }

    if (http.total_len > 0)
    {
        bool startUpdate = true;

        int sketchFreeSpace = ESP.getFreeSketchSpace();
        if (!sketchFreeSpace)
        {
            _lastError = HTTP_UE_NO_PARTITION;
            return HTTP_UPDATE_FAILED;
        }

        if (http.total_len > sketchFreeSpace)
        {
            otd_log_e("FreeSketchSpace to low (%d) needed: %d\n", sketchFreeSpace, http.total_len);
            startUpdate = false;
        }

        if (!startUpdate)
        {
            _lastError = HTTP_UE_TOO_LESS_SPACE;
            ret = HTTP_UPDATE_FAILED;
        }
        else
        {
            // Warn main app we're starting up...
            if (_cbStart)
            {
                _cbStart();
            }

            delay(100);
            otd_log_d("runUpdate flash...\n");

            // check for valid first magic byte
            uint8_t buf[16];
            client.readBytes(buf, 16);
            if (buf[0] != 0xE9)
            {
                otd_log_e("Magic header does not start with 0xE9\n");
                _lastError = HTTP_UE_BIN_VERIFY_HEADER_FAILED;
                // client.stop();
                return HTTP_UPDATE_FAILED;
            }

            // TODO:
            // uint32_t bin_flash_size = http.total_len;
            // check if new bin fits to SPI flash
            // if(bin_flash_size > ESP.getFlashChipSize())
            // {
            //     log_e("New binary does not fit SPI Flash size\n");
            //     _lastError = HTTP_UE_BIN_FOR_WRONG_FLASH;
            //     client.stop();
            //     return HTTP_UPDATE_FAILED;
            // }

            if (runUpdate(http, U_FLASH))
            {
                ret = HTTP_UPDATE_OK;
                otd_log_d("Update ok\n");
                // client.stop();
                // Warn main app we're all done
                if (_cbEnd)
                {
                    _cbEnd();
                }

                if (_rebootOnUpdate)
                {
                    ESP.restart();
                }
            }
            else
            {
                ret = HTTP_UPDATE_FAILED;
                otd_log_e("Update failed\n");
            }
        }
    }
    else
    {
        _lastError = HTTP_UE_SERVER_NOT_REPORT_SIZE;
        ret = HTTP_UPDATE_FAILED;
        otd_log_e("Content-Length was 0 or wasn't set by Server?!\n");
    }

    // client.stop();
    return ret;
}

#define BUF_SIZE 1024 * 1
#define GET_SIZE 1024 * 128
/**
 * write Update to flash
 * @param in Stream&
 * @param size uint32_t
 * @param md5 String
 * @return true if Update ok
 */
bool FlashUpdater::runUpdate(TinyHTTP http, int command)
{
    StreamString error;

    if (_cbProgress)
    {
        Update.onProgress(_cbProgress);
    }

    if (!Update.begin(http.total_len, command, _ledPin, _ledOn))
    {
        _lastError = Update.getError();
        Update.printError(error);
        error.trim(); // remove line ending
        otd_log_e("Update.begin failed! (%s)\n", error.c_str());
        Update.end();
        return false;
    }

    if (_cbProgress)
    {
        _cbProgress(0, http.total_len);
    }

    if (http.xMD5.length())
    {
        if (!Update.setMD5(http.xMD5.c_str()))
        {
            _lastError = HTTP_UE_SERVER_FAULTY_MD5;
            otd_log_e("Update.setMD5 failed! (%s)\n", http.xMD5.c_str());
            return false;
        }
    }

    // To do: the SHA256 could be checked if the server sends it
    MD5Builder md5c;
    md5c.begin();
    uint32_t t0 = millis();
    uint8_t tmp_buf[BUF_SIZE];
    size_t remain = http.total_len;
    size_t remain_get = 0;
    http.user_headers = "";

    for (uint32_t i = 0; i < http.total_len; tmp_buf)
    {
        otd_log_i("Begin Download part ind:%d, get buf:%d,rem:%d\n", i, remain_get, remain);
        size_t rd;
        if (remain_get)
        {
            if(remain_get < BUF_SIZE)
                rd = http.client.readBytes(tmp_buf, remain_get);
            else
                rd = http.client.readBytes(tmp_buf, BUF_SIZE);
            remain_get -= rd;
            otd_log_i("Begin Download part %d\n", i);
        }
        else
        {
            if (remain > GET_SIZE)
            {
                for (uint8_t t = 0; t < 3; t++)
                {
                    if (http.get("", i, GET_SIZE))
                        break;
                }
                rd = http.client.readBytes(tmp_buf, BUF_SIZE);
                remain_get = GET_SIZE - BUF_SIZE;
            }
            else
            {
                for (uint8_t t = 0; t < 3; t++)
                {
                    if (http.get("", i, remain))
                        break;
                }
                if (remain > BUF_SIZE)
                    rd = http.client.readBytes(tmp_buf, BUF_SIZE);
                else
                    rd = http.client.readBytes(tmp_buf, remain);
                remain_get = remain - rd;
            }
        }

        remain -= rd;
        otd_log_i("Downloaded part %d-%d %d\n", i, rd, remain);
        if (rd == 0)
        {
            // Update.printError(error);
            // error.trim(); // remove line ending
            otd_log_e("read from stream failed!\n");
            remain_get = 0;
            // return false;
            continue;
        }

        md5c.add(tmp_buf, rd);
        if (Update.write(tmp_buf, rd) != rd)
        {
            _lastError = Update.getError();
            Update.printError(error);
            error.trim(); // remove line ending
            otd_log_e("Update.writeStream failed! (%s)\n", error.c_str());
            return false;
        }

        i += rd;

        otd_log_i("U[%d] %d,%d\n", millis() - t0, http.total_len, i);
        if (_cbProgress)
        {
            _cbProgress(i, http.total_len);
        }
    }

    md5c.calculate();
    otd_log_i("Download end MD5 %s\n", md5c.toString().c_str());
    if (!Update.end())
    {
        _lastError = Update.getError();
        Update.printError(error);
        error.trim(); // remove line ending
        otd_log_e("Update.end failed! (%s)\n", error.c_str());
        return false;
    }

    return true;
}
#endif