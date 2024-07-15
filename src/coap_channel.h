#pragma once

#include <WiFi.h>
#include <Arduino.h>
#include <mbedtls/ecdh.h>
#include <mbedtls/entropy.h>
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/aes.h>
#include <mbedtls/error.h>
#include <mbedtls/pk.h>

class otadrive_coap
{
    friend class otadrive_ota;
private:
    bool keys_generated = false;
    unsigned char sharedKey[32];
    mbedtls_aes_context aes;

    void print_hex(const char *title, const unsigned char *data, size_t len);
    void print_mbedtls_error(int ret);
    int udpRequest(char *req, size_t reqSize, char *resp, size_t respSize);
    void print_mpi(const char *title, const mbedtls_mpi *a);
    void print_ecp(const char *title, mbedtls_ecp_point *Q);
    int makeSecureBlock(const uint8_t *input_data, uint8_t *encrypted_data);
    int makeSecurePacket(uint8_t *input_data, size_t len, uint8_t *encrypted_data);

public:
    otadrive_coap();
    bool begin();
    int keyExchange(bool forceRenew = false);
    int setSharedKey(const uint8_t sharedKey[32]);
    const uint8_t* getSharedKey();
    int putLog(const char *data);
};