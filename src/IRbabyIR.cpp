#include "IRbabyIR.h"
#include "ESP8266HTTPClient.h"
#include "IRbabySerial.h"
#include "IRbabyUDP.h"
#include "IRbabyUserSettings.h"
#include <LittleFS.h>
#include "IRbabyGlobal.h"
#include "defines.h"

#define DOWNLOAD_PREFIX "http://irext-debug.oss-cn-hangzhou.aliyuncs.com/irda_"
#define DOWNLOAD_SUFFIX ".bin"
#define SAVE_PATH "/bin/"

decode_results results; // Somewhere to store the results
const uint8_t kTimeout = 50;
// As this program is a special purpose capture/decoder, let us use a larger
// than normal buffer so we can handle Air Conditioner remote codes.
const uint16_t kCaptureBufferSize = 1024;
static IRsend *ir_send = nullptr;
static IRrecv *ir_recv = nullptr;
bool saveSignal();

void downLoadFile(String file)
{
    HTTPClient http_client;
    String download_url = DOWNLOAD_PREFIX + file + DOWNLOAD_SUFFIX;
    String save_path = BIN_SAVE_PATH + file;
    File cache = LittleFS.open(save_path, "w");
    if (cache)
    {
        http_client.begin(wifi_client, download_url);
        if (http_client.GET() == HTTP_CODE_OK)
        {
            WiFiClient &wificlient = http_client.getStream();
            uint8_t buff[512];
            int n = wificlient.readBytes(buff, http_client.getSize());
            cache.write(buff, n);
            cache.flush();
        }
    }
    else
    {
        clearBinFiles();
    }
    cache.close();
    http_client.end();
}

bool sendIR(String file_name)
{
    String save_path = SAVE_PATH + file_name;
    if (LittleFS.exists(save_path))
    {
        File cache = LittleFS.open(save_path, "r");
        if (!cache)
        {
            ERRORF("Failed to open %s", save_path.c_str());
            return false;
        }
        Serial.println();
        uint16_t *data_buffer = (uint16_t *)malloc(sizeof(uint16_t) * 512);
        uint16_t length = cache.size() / 2;
        memset(data_buffer, 0x0, 512);
        INFOF("file size = %d\n", cache.size());
        INFOLN();
        cache.readBytes((char *)data_buffer, cache.size());
        disableIR();
        ir_send->sendRaw(data_buffer, length, 38);
        enableIR();
        free(data_buffer);
        cache.close();
        return true;
    }
    return false;
}

void sendStatus(String file, t_remote_ac_status status)
{
    sendControl(file, REMOTE_CATEGORY_AC, 0, &status);
}

void recvIR()
{
    if (ir_recv->decode(&results))
    {
        DEBUGF("raw length = %d\n", results.rawlen - 1);
        String raw_data;
        for (int i = 1; i < results.rawlen; i++)
            raw_data += String(*(results.rawbuf + i) * kRawTick) + " ";
        ir_recv->resume();
        send_msg_doc.clear();
        send_msg_doc["cmd"] = "record_rt";
        send_msg_doc["params"]["signal"] = "IR";
        send_msg_doc["params"]["length"] = results.rawlen;
        send_msg_doc["params"]["value"] = raw_data.c_str();
        DEBUGLN(raw_data.c_str());
        sendUDP(&send_msg_doc, remote_ip);
        saveSignal();
    }
}

bool saveIR(String file_name)
{
    String save_path = SAVE_PATH;
    save_path += file_name;
    return LittleFS.rename("/bin/test", save_path);
}

bool saveSignal()
{
    String save_path = SAVE_PATH;
    save_path += "test";
    DEBUGF("save raw data as %s\n", save_path.c_str());
    File cache = LittleFS.open(save_path, "w");
    if (!cache)
    {
        ERRORLN("Failed to create file");
        return false;
    }
    for (size_t i = 0; i < results.rawlen; i++)
        *(results.rawbuf + i) = *(results.rawbuf + i) * kRawTick;
    cache.write((char *)(results.rawbuf + 1), (results.rawlen - 1) * 2);
    cache.close();
    return true;
}

void initAC(String file)
{
    ACStatus[file]["power"] = 0;
    ACStatus[file]["temperature"] = 8;
    ACStatus[file]["mode"] = 2;
    ACStatus[file]["swing"] = 0;
    ACStatus[file]["speed"] = 0;
}

bool sendKey(String file, int key_code, const UINT8 category)
{
    sendControl(file, category, key_code, NULL);
    return true;
}

void loadIRPin(uint8_t send_pin, uint8_t recv_pin)
{
    if (ir_send != nullptr)
    {
        delete ir_send;
    }
    if (ir_recv != nullptr)
    {
        delete ir_recv;
    }
    ir_send = new IRsend(send_pin);
    DEBUGF("Load IR send pin at %d\n", send_pin);
    ir_send->begin();
    ir_recv = new IRrecv(recv_pin, kCaptureBufferSize, kTimeout, true);
    disableIR();
}

void disableIR()
{
    pinMode(14,OUTPUT);
    digitalWrite(14,LOW);    
    ir_recv->disableIRIn();
}

void enableIR()
{
    ir_recv->enableIRIn();
    pinMode(14,INPUT_PULLUP);
    pinMode(13,OUTPUT);
    digitalWrite(13,LOW);    
}

void sendControl(String file, const UINT8 category, UINT8 key_code, t_remote_ac_status* status)
{
    String save_path = SAVE_PATH + file;
    if (!LittleFS.exists(save_path))
        downLoadFile(file);

    sendLogUDP(file.c_str());
    if (LittleFS.exists(save_path))
    {
        File cache = LittleFS.open(save_path, "r");
        if (cache)
        {
            UINT16 content_size = cache.size();
            DEBUGF("content size = %d\n", content_size);

            if (content_size != 0)
            {
                UINT8 *content = (UINT8 *)malloc(content_size * sizeof(UINT8));
                cache.seek(0L, fs::SeekSet);
                cache.readBytes((char *)content, content_size);
                ir_binary_open(category, 1, content, content_size);
                UINT16 *user_data = (UINT16 *)malloc(1024 * sizeof(UINT16));
                UINT16 data_length = ir_decode(key_code, user_data, status, FALSE);

                sendLogUDP((char*)user_data);

                DEBUGF("data_length = %d\n", data_length);
                disableIR();
                ir_send->sendRaw(user_data, data_length, 38);
                enableIR();
                ir_close();
                free(user_data);
                free(content);
                if (NULL != status)
                {
                    saveACStatus(file, status);
                }
            }
            else
                ERRORF("Open %s is empty\n", save_path.c_str());
        }
        cache.close();
    }
}