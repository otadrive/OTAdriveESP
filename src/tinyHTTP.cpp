
#ifdef ESP8266
#include "tinyHTTP.h"
#include <ESP8266HTTPClient.h>
#include <otadrive_esp.h>
#elif defined(ESP32)
#include "tinyHTTP.h"
#include <HTTPClient.h>
#include <otadrive_esp.h>
#endif

using namespace OTAdrive_ns;

#define REQ_PARTIAL_LEN (1024 * 32)

TinyHTTP::TinyHTTP(Client &c, bool useSSL) : client(c)
{
    _useSSL = useSSL;
}

bool TinyHTTP::begin_connect(const String &url)
{
    String u = url;
    int port = _useSSL ? 443 : 80;
    // TODO: HTTPS Not supported yet
    if (u.startsWith("https://"))
    {
        u.remove(0, 8);
        port = 443;
    }
    else if (u.startsWith("http://"))
        u.remove(0, 7);

    //
    host = u.substring(0, u.indexOf('/'));
    uri = u.substring(host.length());
    // TODO: Port may be diffrent than 80

    client.setTimeout(10000);
    if (client.connected())
        client.stop();
    for (uint8_t tr = 0;; tr++)
    {
        if (tr == 3)
            return false;
        if (!client.connect(host.c_str(), port))
        {
            otd_log_e("connect to %s faild\n", host.c_str());
            continue;
        }
        break;
    }

    return true;
}

bool TinyHTTP::get(String url, int partial_st, int partial_len)
{
    if (url.isEmpty())
        url = _url;
    else
        _url = url;

    bool head = partial_st == 0 && partial_len == 0;
    resp_code = 0;

    if (!begin_connect(url))
        return false;

    String http_get = (head ? "HEAD " : "GET ") + uri + " HTTP/1.1";
    String http_hdr;
    total_len = 0;

    otd_log_d("%s\n", http_get.c_str());
    client.setTimeout(30 * 1000);
    // use HTTP/1.0 for update since the update handler not support any transfer Encoding
    http_hdr += "\r\nHost: " + host;
    http_hdr += "\r\nConnection: Keep-Alive";
    http_hdr += "\r\nCache-Control: no-cache, no-store, must-revalidate";
    http_hdr += "\r\nPragma: no-cache";
    http_hdr += "\r\nExpires: 0";
    http_hdr += user_headers;

    // const char *headerkeys[] = {"x-MD5"};
    // size_t headerkeyssize = sizeof(headerkeys) / sizeof(char *);
    String total_req = http_get +
                       http_hdr +
                       (head || partial_len == INT_MAX ? "" : "\r\nRange: bytes=" + String(partial_st) + "-" + String((partial_st + partial_len) - 1)) +
                       "\r\n\r\n";
    otd_log_d("request: %s", total_req.c_str());
    client.print(total_req.c_str());
    client.flush();
    // clear all junk data
    while (client.available())
        client.read();

    int content_len = 0;

    String resp = client.readStringUntil('\n');
    otd_log_d("hdr %s", resp.c_str());
    // HTTP/1.1 401 Unauthorized
    // HTTP/1.1 200 OK
    if (!resp.startsWith("HTTP/1.1 "))
        return false;
    resp_code = resp.substring(9, 9 + 3).toInt();

    if (resp_code != HTTP_CODE_OK && resp_code != HTTP_CODE_PARTIAL_CONTENT)
    {
        otd_log_e("HTTP error: %d,%s\n", resp_code, resp.c_str());
        return false;
    }
    isPartial = (resp_code == HTTP_CODE_PARTIAL_CONTENT);

    // response headers
    do
    {
        resp = client.readStringUntil('\n');
        resp.trim();
        resp.toLowerCase();
        otd_log_i("hdr %s", resp.c_str());
        if (resp.startsWith("content-length"))
        {
            content_len = resp.substring(16).toInt();
        }
        else if (resp.startsWith("x-md5"))
        {
            xMD5 = resp.substring(7);
        }
        else if (resp.startsWith("x-version"))
        {
            xVersion = resp.substring(11);
        }
        else if (resp.startsWith("content-range: bytes"))
        {
            // Content-Range: bytes 0-51199/939504
            int total_ind = resp.indexOf('/');
            total_len = resp.substring(total_ind + 1).toInt();
        }

        if (resp.isEmpty())
            break;
    } while (true);

    if (total_len == 0 && isPartial)
        return false;
    if (total_len == 0)
        total_len = content_len;

    return true;
}
