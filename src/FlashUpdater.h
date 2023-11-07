
#if !defined(___GSMHTTP_UPDATE_H___)// && defined(ESP32)
#define ___GSMHTTP_UPDATE_H___

#include <Arduino.h>
#include <WiFiClient.h>
#include <WiFiUdp.h>
#include "tinyHTTP.h"
#include "otadrive_esp.h"
#include "types.h"

namespace OTAdrive_ns
{
/// note we use HTTP client errors too so we start at 100
#define HTTP_UE_TOO_LESS_SPACE (-100)
#define HTTP_UE_SERVER_NOT_REPORT_SIZE (-101)
#define HTTP_UE_SERVER_FILE_NOT_FOUND (-102)
#define HTTP_UE_SERVER_FORBIDDEN (-103)
#define HTTP_UE_SERVER_WRONG_HTTP_CODE (-104)
#define HTTP_UE_SERVER_FAULTY_MD5 (-105)
#define HTTP_UE_BIN_VERIFY_HEADER_FAILED (-106)
#define HTTP_UE_BIN_FOR_WRONG_FLASH (-107)
#define HTTP_UE_NO_PARTITION (-108)

    // enum HTTPUpdateResult {
    //     HTTP_UPDATE_FAILED,
    //     HTTP_UPDATE_NO_UPDATES,
    //     HTTP_UPDATE_OK
    // };

    // typedef HTTPUpdateResult t_httpUpdate_return; // backward compatibility

    using HTTPUpdateStartCB = std::function<void()>;
    using HTTPUpdateEndCB = std::function<void()>;
    using HTTPUpdateErrorCB = std::function<void(int)>;
    using HTTPUpdateProgressCB = std::function<void(int, int)>;

    class FlashUpdater
    {
        String host;
        String uri;

    public:
        FlashUpdater(void);
        FlashUpdater(int httpClientTimeout);
        ~FlashUpdater(void);

        void rebootOnUpdate(bool reboot)
        {
            _rebootOnUpdate = reboot;
        }

        void setLedPin(int ledPin = -1, uint8_t ledOn = HIGH)
        {
            _ledPin = ledPin;
            _ledOn = ledOn;
        }

        FotaResult update(Client &client, const String &url);

        // Notification callbacks
        void onStart(HTTPUpdateStartCB cbOnStart) { _cbStart = cbOnStart; }
        void onEnd(HTTPUpdateEndCB cbOnEnd) { _cbEnd = cbOnEnd; }
        void onError(HTTPUpdateErrorCB cbOnError) { _cbError = cbOnError; }
        void onProgress(HTTPUpdateProgressCB cbOnProgress) { _cbProgress = cbOnProgress; }

        int getLastError(void);
        String getLastErrorString(void);
        static String createHeaders(void);

        bool MD5_Match = true;

    protected:
        FotaResult handleUpdate(Client &http, const String &currentVersion);
        bool runUpdate(TinyHTTP http, int command = 0); // U_FLASH

        // Set the error and potentially use a CB to notify the application
        void _setLastError(int err)
        {
            _lastError = err;
            if (_cbError)
            {
                _cbError(err);
            }
        }
        int _lastError;
        bool _rebootOnUpdate = true;

    private:
        int _httpClientTimeout;

        // Callbacks
        HTTPUpdateStartCB _cbStart;
        HTTPUpdateEndCB _cbEnd;
        HTTPUpdateErrorCB _cbError;
        HTTPUpdateProgressCB _cbProgress;

        int _ledPin;
        uint8_t _ledOn;
    };
}
#endif /* ___HTTP_UPDATE_H___ */
