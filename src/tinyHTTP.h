#pragma once
#include <Arduino.h>

namespace OTAdrive
{
    class TinyHTTP
    {
        String _url;
        String host;
        String uri;

        bool begin_connect(const String &url);

    public:
        TinyHTTP(Client &c);
        bool get_partial(String url, int partial_st = 0, int partial_len = INT_MAX);
        String user_headers;

        uint32_t total_len;
        int16_t resp_code;
        String MD5;
        bool isPartial;
        Client &client;
    };
}