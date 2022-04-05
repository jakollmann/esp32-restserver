#include <StreamUtils.h>
#include <WiFi.h>
#include <SPIFFS.h>
#include <ESPAsyncWebServer.h>
#include <TokenIterator.h>
#include <UrlTokenBindings.h>
#include <AsyncJson.h>
#include <ArduinoJson.h>
#include <FreeRTOS.h>

/*
 * Simple REST interface for switching/checking GPIO ports 
 * 
 */


String wifiSSID;
String wifiPWD;

// Web server running on port 80
AsyncWebServer server(80);

// JSON data buffer
StaticJsonDocument<250> jsonDocument;
char outputBuffer[250];

int maxValveID = 0;
int *valveGPIO = NULL;

void connectToWiFi() {
  Serial.print("Connecting to ");
  Serial.println(wifiSSID);

  WiFi.begin(wifiSSID.c_str(), wifiPWD.c_str());

  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    delay(500);
  }

  Serial.print("Connected IP: ");
  Serial.println(WiFi.localIP());
}

void setupRouting() {
  /*
    server.on("/valve/*", HTTP_GET, [](AsyncWebServerRequest *request){

    char templatePath[] = "/valve/:valveId/:action";
    char urlBuffer[60];
    request->url().toCharArray(urlBuffer, 60);
    int urlLength = request->url().length();
    TokenIterator templateIterator(templatePath, strlen(templatePath), '/');
    TokenIterator pathIterator(urlBuffer, urlLength, '/');
    UrlTokenBindings bindings(templateIterator, pathIterator);

    int valveId = atoi ( bindings.get("valveId") );
    char action[10];
    strncpy ( action, bindings.get("action"), 10 );

    int httpCode = 200;
    jsonDocument.clear();
    jsonDocument["Valve ID"] = valveId;
    jsonDocument["action"] = action;

    if ( strcmp ( action, "status" ) == 0 ) {
      if ( valveId > maxValveID ) {
        httpCode = 404;
        jsonDocument["result"] = "404 - no valid id";
      } else {
        int value = digitalRead ( valveGPIO[valveId-1] );
        jsonDocument["result"] = value == HIGH ? "HIGH" : "LOW";
      }

    } else {
      httpCode = 404;
      jsonDocument["result"] = "404 - no valid status";
    }

    serializeJson(jsonDocument, outputBuffer);

    request->send(httpCode, "application/json", outputBuffer);
    });
  */
  // server.on("/valve/*", HTTP_POST, setValveStatus);

  server.on("/", HTTP_GET, [](AsyncWebServerRequest * request) {
    jsonDocument.clear();
    jsonDocument["action"] = "welcome";
    jsonDocument["result"] = "OK";

    serializeJson(jsonDocument, outputBuffer);
    request->send(200, "application/json", outputBuffer);
  });


  AsyncCallbackJsonWebHandler *valveRestHandler = new AsyncCallbackJsonWebHandler("/valve/*", [](AsyncWebServerRequest * request, JsonVariant & jsonInput) {
    StaticJsonDocument<200> dataInput;
    Serial.println("Start Handler for /valve/*");

    if (jsonInput.is<JsonArray>())
    {
      dataInput = jsonInput.as<JsonArray>();
    }
    else if (jsonInput.is<JsonObject>())
    {
      dataInput = jsonInput.as<JsonObject>();
    }

    char templatePath[] = "/valve/:valveId/:action";
    char urlBuffer[60];
    request->url().toCharArray(urlBuffer, 60);
    int urlLength = request->url().length();
    TokenIterator templateIterator(templatePath, strlen(templatePath), '/');
    TokenIterator pathIterator(urlBuffer, urlLength, '/');
    UrlTokenBindings bindings(templateIterator, pathIterator);

    int valveId = atoi ( bindings.get("valveId") );
    char action[10];
    strncpy ( action, bindings.get("action"), 10 );

    int httpCode = 404;
    jsonDocument.clear();
    jsonDocument["Valve ID"] = valveId;
    jsonDocument["action"] = action;
    if ( request->methodToString() == "POST" ) {

    } else if ( request->methodToString() == "PUT" )  {
      if ( strcmp ( action, "status" ) == 0 ) {
        if ( valveId > maxValveID ) {
          httpCode = 404;
          jsonDocument["result"] = "404 - no valid id";
        } else {
          pinMode(valveGPIO[valveId - 1], OUTPUT);
          if ( strcmp ( dataInput["value"], "HIGH" ) == 0 )  {
            digitalWrite ( valveGPIO[valveId - 1], HIGH );
          } else {
            digitalWrite ( valveGPIO[valveId - 1], LOW );
          }
          httpCode = 200;
          jsonDocument["result"] = "OK";
          jsonDocument["value"] = dataInput["value"];
        }

      } else {
        httpCode = 404;
        jsonDocument["result"] = "404 - no valid status";
      }
    } else if ( request->methodToString() == "GET" )  {
      if ( strcmp ( action, "status" ) == 0 ) {
        if ( valveId > maxValveID ) {
          httpCode = 404;
          jsonDocument["result"] = "404 - no valid id";
        } else {
          int value = digitalRead ( valveGPIO[valveId - 1] );
          jsonDocument["result"] = "OK";
          jsonDocument["value"] = value == HIGH ? "HIGH" : "LOW";
          httpCode = 200;
        }

      } else {
        httpCode = 404;
        jsonDocument["result"] = "404 - no valid status";
      }
    } else if ( request->methodToString() == "OPTIONS" )  {

    } else {
      jsonDocument["result"] = "404 - method not implemented";
    }

    serializeJson(jsonDocument, outputBuffer);
    Serial.println("Handler sending response");
    request->send(httpCode, "application/json", outputBuffer);
  });
  server.addHandler(valveRestHandler);

  server.onNotFound([](AsyncWebServerRequest * request) {
    jsonDocument.clear();
    jsonDocument["result"] = "404 - not found";

    serializeJson(jsonDocument, outputBuffer);
    request->send(404, "application/json", outputBuffer);
  });


  // start server
  server.begin();
}

int readSetup() {
  if (!SPIFFS.begin(true)) {
    Serial.println("An Error has occurred while mounting SPIFFS");
    return 0;
  }
  File settingsFD = SPIFFS.open("/settings.json");
  if (!settingsFD) {
    Serial.println("Failed to open settings file");
    return 0;
  }
  ReadBufferingStream bufferingStream(settingsFD, 64);
  StaticJsonDocument<512> jsonDoc;
  DeserializationError error = deserializeJson ( jsonDoc, bufferingStream );
  if (error) {
    Serial.print(F("deserializeJson() failed: "));
    Serial.println(error.f_str());
    return 0;
  }
  wifiSSID = jsonDoc["wifi"]["ssid"].as<const char*>();
  wifiPWD = jsonDoc["wifi"]["pass"].as<const char*>();
  JsonArray valveList = jsonDoc["valves"].as<JsonArray>();
  maxValveID = valveList.size() - 1;
  valveGPIO = new int[maxValveID + 1];
  int i = 0;
  for (JsonVariant v : valveList) {
    valveGPIO[i++] = v.as<int>();
  }
  return 1;
}


void setup() {
  Serial.begin(115200);
  Serial.print("Start\n");

  readSetup();
  connectToWiFi();
  setupRouting();

}

void loop() {
}
