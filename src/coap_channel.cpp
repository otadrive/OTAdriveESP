#include "coap_channel.h"
#include "otadrive_esp.h"
WiFiUDP udp;
otadrive_coap::otadrive_coap()
{
}

bool otadrive_coap::begin()
{
    return udp.begin(OTADRIVE_UDP_PORT);
}

void otadrive_coap::print_hex(const char *title, const unsigned char *data, size_t len)
{
#ifdef OTD_COAP_DEBUG
    Serial.printf("%s: ", title);
    for (size_t i = 0; i < len; i++)
    {
        Serial.printf("%02X", data[i]);
    }
    Serial.printf("\n");
#endif
}

void otadrive_coap::print_mbedtls_error(int ret)
{
    if (ret == 0)
        return;
    char error_buf[100];
    mbedtls_strerror(ret, error_buf, 100);
    Serial.printf("Error: %s\n", error_buf);
}

int otadrive_coap::udpRequest(char *req, size_t reqSize, char *resp, size_t respSize)
{
    // clear in buf
    udp.parsePacket();
    while (udp.available())
        udp.read();

    udp.beginPacket(OTADRIVE_HOST, OTADRIVE_UDP_PORT);
    udp.write((uint8_t *)req, reqSize);
    udp.endPacket();

    if (resp == NULL)
        return 0;

    udp.setTimeout(10000);

    for (int w = 0; w < 10000; w++)
    {
        if (udp.parsePacket())
        {
            log_i("udp read %d bytes after %dms", udp.available(), w);
            int n = udp.readBytes((uint8_t *)resp, udp.available());
            print_hex("udp resp", (uint8_t *)resp, n);
            return n;
        }
        delay(1);
    }

    return 0;
}

void otadrive_coap::print_mpi(const char *title, const mbedtls_mpi *a)
{
#ifdef OTD_COAP_DEBUG
    char str[200];
    size_t olen2;
    print_mbedtls_error(mbedtls_mpi_write_string(a, 0x10, str, sizeof(str), &olen2));
    log_i("%s (%d): %s", title, olen2, str);
#endif
}

void otadrive_coap::print_ecp(const char *title, mbedtls_ecp_point *Q)
{
#ifdef OTD_COAP_DEBUG
    char t[32];
    sprintf(t, "%s Q.X", title);
    print_mpi(t, &Q->X);

    sprintf(t, "%s Q.Y", title);
    print_mpi(t, &Q->Y);

    sprintf(t, "%s Q.Z", title);
    print_mpi(t, &Q->Z);
#endif
}

int otadrive_coap::KeyExchange(bool forceRenew, const char *privateKey, const char *publicKey)
{
    if (keys_generated && !forceRenew)
        return 0;
    const char helloResource[] = "hello";
    char buf[200];
    char cmd[200];
    int msgKeyIndex;
    int ret = 0;
    bool fixedKeysMode = false;
    size_t olen, olen2;

    mbedtls_ecdh_context ecdh_client;
    mbedtls_entropy_context entropy;
    mbedtls_ctr_drbg_context ctr_drbg;

    {
        // CoAp CON,POST with option 11: 'hello'
        // 10 01 04 D2 B3 6C 6F 67 FF 48 65 6C 6C 6F 20 43 6F 41 50
        memset(cmd, 0, sizeof(cmd));
        cmd[0] = 0x60;
        cmd[1] = 0x01; // POST
        uint16_t messageID = 123;
        cmd[2] = messageID >> 8;
        cmd[3] = messageID;
        // add option
        sprintf(&cmd[4], "\xB0%s", helloResource);
        cmd[4] |= sizeof(helloResource) - 1;

        int i = 4 + sizeof(helloResource);

        // payload begin
        cmd[i] = 0xff;
        i++;
        // [0] Ver
        // [1-16] API-KEY
        // [16-48] Ascii Serial (fixed 32)
        // [48-113] Public Key Prefix-X-Y (1 X32 Y32)
        cmd[i++] = 1; // V1
        memcpy(&cmd[i], OTADRIVE.deviceHash, 16);
        i += 16;
        strcpy(&cmd[i], OTADRIVE.getChipId().c_str());
        i += 32;
        msgKeyIndex = i;
    }

    if (keys_generated)
    {
        log_i("Keys available");

        // Just send public key to the server
        // [48-113] Public Key Prefix-X-Y (1 X32 Y32)
        memcpy(&cmd[msgKeyIndex], udpPublicKey, 32);
        print_hex("cmd", (uint8_t *)cmd, msgKeyIndex + 32);
        int n = udpRequest(cmd, msgKeyIndex + 32, buf, sizeof(buf));
        log_i("udp result:%d", n);
        if (n != 43)
            return 1;

        return 0;
    }

    if (privateKey != NULL && publicKey != NULL)
    {
        log_i("Fixed keys mode");
        fixedKeysMode = true;
    }

    do
    {
        mbedtls_ecdh_init(&ecdh_client);
        mbedtls_entropy_init(&entropy);
        mbedtls_ctr_drbg_init(&ctr_drbg);

        // Initialize RNG
        print_mbedtls_error(ret = mbedtls_ctr_drbg_seed(&ctr_drbg, mbedtls_entropy_func, &entropy, (const unsigned char *)pers, strlen(pers)));
        if (ret)
            break;

        // Setup ECDH contexts
        print_mbedtls_error(ret = mbedtls_ecdh_setup(&ecdh_client, MBEDTLS_ECP_DP_CURVE25519));
        if (ret)
            break;

        if (!fixedKeysMode)
        {
            log_i("init group nbits:%d T_size:%d", ecdh_client.grp.nbits, ecdh_client.grp.T_size);
            log_i("gen keys");
            print_mbedtls_error(ret = mbedtls_ecdh_gen_public(&ecdh_client.grp, &ecdh_client.d, &ecdh_client.Q, mbedtls_ctr_drbg_random, &ctr_drbg));
            if (ret)
                break;

            mbedtls_ecp_point_write_binary(&ecdh_client.grp, &ecdh_client.Q, MBEDTLS_ECP_PF_UNCOMPRESSED, &olen, (uint8_t *)buf, 200);
            log_i("len %d", olen);
            print_hex("Q export", (uint8_t *)buf, olen);
            mbedtls_mpi_write_binary(&ecdh_client.d, (uint8_t *)buf, 200);
            print_hex("d export", (uint8_t *)buf, ecdh_client.d.n);
        }
        else
        {
            print_mbedtls_error(ret = mbedtls_ecp_point_read_string(&ecdh_client.Q, 16, (const char *)publicKey, ""));
            if (ret)
            {
                log_e("wrong public key");
                break;
            }

            print_mbedtls_error(ret = mbedtls_mpi_read_string(&ecdh_client.d, 16, (const char *)privateKey));
            if (ret)
            {
                log_e("wrong private key");
                break;
            }
        }

        print_mbedtls_error(ret = mbedtls_ecp_point_write_binary(&ecdh_client.grp, &ecdh_client.Q, MBEDTLS_ECP_PF_UNCOMPRESSED, &olen, udpPublicKey, 32));
        if ((olen != 32 && olen != 65) || ret) // 65 BP256
        {
            log_i("Bad len %d", olen);
            break;
        }

        print_ecp("Q client pub", &ecdh_client.Q);
        print_mpi("client d", &ecdh_client.d);

        log_i("Lets say hello");
        // [48-113] Public Key Prefix-X-Y (1 X32 Y32)
        memcpy(&cmd[msgKeyIndex], udpPublicKey, 32);
        print_hex("cmd", (uint8_t *)cmd, msgKeyIndex + 32);
        int n = udpRequest(cmd, msgKeyIndex + 32, buf, sizeof(buf));
        log_i("udp result:%d", n);
        if (n != 79 && n != 43)
            break;

        if (strncmp(&buf[6], "hello", 5))
        {
            log_i("udp bad resp");
            break;
        }

        if (true)
            sprintf(&buf[11], "\x23\x98\xDA\x44\x08\xD5\x1E\xE4\x3D\xF2\x92\x16\x29\xB9\xEE\x43\x20\xAC\x6D\x86\x8B\x23\xB0\xA8\x18\x20\x54\xE3\x9C\x80\x63\x1D");
        print_mbedtls_error(ret = mbedtls_ecp_point_read_binary(&ecdh_client.grp, &ecdh_client.Qp, (uint8_t *)buf + 11, 32));

        if (ret)
            break;
        print_ecp("Q server pub", &ecdh_client.Qp);

        log_i("compute shared");
        print_mbedtls_error(ret = mbedtls_ecdh_compute_shared(&ecdh_client.grp, &ecdh_client.z, &ecdh_client.Qp, &ecdh_client.d, mbedtls_ctr_drbg_random, &ctr_drbg));
        if (ret)
            break;
        print_mpi("shared Z", &ecdh_client.z);

        print_mbedtls_error(ret = mbedtls_mpi_write_binary(&ecdh_client.z, shared_secret_client, sizeof(shared_secret_client)));
        if (ret)
            break;

        mbedtls_aes_init(&aes);
        print_mbedtls_error(ret = mbedtls_aes_setkey_enc(&aes, shared_secret_client, 256));
        if (ret)
            break;

        keys_generated = true;
    } while (false);

    mbedtls_ecdh_free(&ecdh_client);
    mbedtls_entropy_free(&entropy);
    mbedtls_ctr_drbg_free(&ctr_drbg);
    return ret;
}

int otadrive_coap::makeSecureBlock(const uint8_t *input_data, uint8_t *encrypted_data)
{
    int ret;

    if (!keys_generated)
        return 1;
    do
    {

        print_mbedtls_error(ret = mbedtls_aes_crypt_ecb(&aes, MBEDTLS_AES_ENCRYPT, input_data, encrypted_data));
        if (ret)
            break;

        print_hex("Encrypted Data", encrypted_data, 16);
        print_hex("Encrypted Key", shared_secret_client, 32);
    } while (false);

    return ret;
}

// in & out should be x16 size arrays
int otadrive_coap::makeSecurePacket(uint8_t *input_data, size_t len, uint8_t *encrypted_data)
{
    if (!keys_generated)
        return 1;
    for (int i = 0; i < len; i += 16)
    {
        makeSecureBlock(input_data + i, encrypted_data + i);
    }

    return 0;
}

int otadrive_coap::putLog(char *data)
{
    if (!keys_generated)
        return 1;

    String logTxt = " ";
    logTxt += data;
    // limit to maximum length
    logTxt.substring(850);

    size_t len = logTxt.length();

    char cmd[32] = {0};
    int cmdLen = 0;
    {
        const char logResource[] = "log";
        // CoAp NON,PUT with option 11: 'log'
        memset(cmd, 0, sizeof(cmd));
        cmd[0] = 0x70;
        cmd[1] = 0x03; // PUT
        uint16_t messageID = 123;
        cmd[2] = messageID >> 8;
        cmd[3] = messageID;
        // add option
        sprintf(&cmd[4], "\xB0%s", logResource);
        cmd[4] |= sizeof(logResource) - 1;

        int i = 4 + sizeof(logResource);

        // payload begin
        cmd[i] = 0xff;
        i++;
        // [0] Ver
        // [1-16] Device Hash
        // [17-18] Msg len
        // [18] always 0x20 (space)
        // [19-...] Encrypted Msg
        cmd[i++] = 1; // V1
        memcpy(&cmd[i], OTADRIVE.deviceHash, 16);
        i += 16;
        cmdLen = i;
    }

    size_t outLen = ((len / 16) + 1) * 16;
    uint8_t *encrypted_data;
    encrypted_data = (uint8_t *)malloc(outLen);
    makeSecurePacket((uint8_t *)data, len, encrypted_data);

    udp.beginPacket(OTADRIVE_HOST, OTADRIVE_UDP_PORT);
    udp.write((uint8_t *)cmd, cmdLen); // cmd: v1, post log msg
    udp.write((len >> 8) & 0xff);
    udp.write((len >> 0) & 0xff);
    udp.write(encrypted_data, outLen);
    udp.endPacket();

    free(encrypted_data);
    return 0;
}
