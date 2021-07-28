/**
 *
 * Copyright (c) 2020 IRbaby
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <Arduino.h>
#include <ESP8266HTTPClient.h>

#include <ESP8266HTTPUpdateServer.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>

#include <Ticker.h>

#include "IRbabyGlobal.h"
#include "IRbabyIR.h"
#include "IRbabyMQTT.h"
#include "IRbabyMsgHandler.h"
#include "IRbabyOTA.h"
#include "IRbabyRF.h"
#include "IRbabyUDP.h"
#include "IRbabyUserSettings.h"
#include "OneButton.h"
#include "defines.h"

#ifdef BLINKER_WIFI
#include "Blinker.h"
#define BLINKER_MIOT_OUTLET
char auth[] = "05efae5f33fa";
#endif                 

#ifdef USE_SENSOR
#include "IRbabyBinarySensor.h"
Ticker sensor_ticker;  // binary sensor ticker
#endif                 // USE_SENSOR

void uploadIP();    // device info upload to devicehive
void httpUpdate();  // define the ota httpupdate function

Ticker mqtt_check;  // MQTT check timer
Ticker disable_ir;  // disable IR receive
Ticker disable_rf;  // disable RF receive
Ticker save_data;   // save data


ESP8266WebServer httpServer(80);        //用于web服务器
ESP8266HTTPUpdateServer httpUpdater;   //用于代码OTA更新

const char* host = "esp8266-webupdate";

OneButton button(RESET_PIN, true);

void setup() {
  if (LOG_DEBUG || LOG_ERROR || LOG_INFO) Serial.begin(BAUD_RATE);
  pinMode(RESET_PIN, INPUT_PULLUP);
  INFOLN();
  INFOLN("8888888 8888888b.  888               888               ");
  INFOLN("  888   888   Y88b 888               888               ");
  INFOLN("  888   888    888 888               888               ");
  INFOLN("  888   888   d88P 88888b.   8888b.  88888b.  888  888 ");
  INFOLN("  888   8888888P   888  88b      88b 888  88b 888  888 ");
  INFOLN("  888   888 T88b   888  888 .d888888 888  888 888  888 ");
  INFOLN("  888   888  T88b  888 d88P 888  888 888 d88P Y88b 888 ");
  INFOLN("8888888 888   T88b 88888P    Y888888 88888P     Y88888 ");
  INFOLN("                                              Y8b d88P ");
  INFOLN("                                                 Y88P  ");
#ifdef USE_LED
  led.Off();
#endif  // USE_LED
  bool res;
  res = wifi_manager.autoConnect();
#ifdef BLINKER_WIFI
  if (res) {
    Blinker.begin(auth, wifi_manager.getSSID().c_str(), wifi_manager.getPassword().c_str());
  }
#endif

#ifdef USE_LED
  led.On();
#endif  // USE_LED

  settingsLoad();  // load user settings form fs
  
  pinMode(16,OUTPUT);
  digitalWrite(16,HIGH);
  delay(5);
  digitalWrite(16,LOW);
  delay(5);
  udpInit();   // udp init
  mqttInit();  // mqtt init

#ifdef HTTP_UPDATE 
  httpUpdate();
#endif

#ifdef USE_RF
  initRF();  // RF init
#endif

  loadIRPin(ConfigData["pin"]["ir_send"], ConfigData["pin"]["ir_receive"]);

#ifdef USE_INFO_UPLOAD
  uploadIP();
#endif

  mqtt_check.attach_scheduled(MQTT_CHECK_INTERVALS, mqttCheck);
  disable_ir.attach_scheduled(DISABLE_SIGNAL_INTERVALS, disableIR);
  disable_rf.attach_scheduled(DISABLE_SIGNAL_INTERVALS, disableRF);
  save_data.attach_scheduled(SAVE_DATA_INTERVALS, settingsSave);

#ifdef USE_SENSOR
  binary_sensor_init();
  sensor_ticker.attach_scheduled(SENSOR_UPLOAD_INTERVAL, binary_sensor_loop);
#endif  // USE_SENSOR

  button.setPressTicks(3000);
  button.attachLongPressStart([]() { settingsClear(); });
#ifdef USE_LED
  led.Blink(200, 200).Repeat(10);
#endif  // USE_LED
  pinMode(14,OUTPUT);
  digitalWrite(14,HIGH);
  pinMode(13,OUTPUT);
  digitalWrite(13,LOW);
}

void httpUpdate()
{
  MDNS.begin(host);
  httpUpdater.setup(&httpServer);
  httpServer.begin();
  MDNS.addService("http", "tcp", 80);
}


void resethigh()
{
    pinMode(13,OUTPUT);
    digitalWrite(13,LOW);
    pinMode(14,INPUT_PULLUP);
}

void loop() {
  /* IR receive */
  resethigh();
  recvIR();

#ifdef USE_RF
  /* RF receive */
  recvRF();
#endif
  /* UDP receive and handle */
  char *msg = udpRecive();
  if (msg) {
    udp_msg_doc.clear();
    DeserializationError error = deserializeJson(udp_msg_doc, msg);
    if (error) ERRORLN("Failed to parse udp message");
    msgHandle(&udp_msg_doc, MsgType::udp);
  }

#ifdef HTTP_UPDATE 
  httpServer.handleClient();
#endif
  /* mqtt loop */
  mqttLoop();
  yield();
}

// only upload chip id
void uploadIP() {
  HTTPClient http;
  StaticJsonDocument<128> body_json;
  String chip_id = String(ESP.getChipId(), HEX);
  chip_id.toUpperCase();
  String head = "http://playground.devicehive.com/api/rest/device/";
  head += chip_id;
  http.begin(wifi_client, head);
  http.addHeader("Content-Type", "application/json");
  http.addHeader(
      "Authorization",
      "Bearer "
      "eyJhbGciOiJIUzI1NiJ9."
      "eyJwYXlsb2FkIjp7ImEiOlsyLDMsNCw1LDYsNyw4LDksMTAsMTEsMTIsMTUsMTYsMTddLCJl"
      "IjoxNzQzNDM2ODAwMDAwLCJ0IjoxLCJ1Ijo2NjM1LCJuIjpbIjY1NDIiXSwiZHQiOlsiKiJd"
      "fX0.WyyxNr2OD5pvBSxMq84NZh6TkNnFZe_PXenkrUkRSiw");
  body_json["name"] = chip_id;
  body_json["networkId"] = "6542";
  String body = body_json.as<String>();
  INFOF("update %s to devicehive\n", body.c_str());
  http.PUT(body);
  http.end();
}