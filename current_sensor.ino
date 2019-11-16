
/*
 * This sketch must work in conjunction with Arduino_heat_sensor.ino sketch. The Arduino portion below will connect to Omron  D6T-1A-01 and get the 
 * one single temperature. Then it will pass it by Serial port to ESP8266 as a 2 byte short integer. ESP8266 takes value and sends to MQTT message
 * NOTE: MAKE SURE YOU COMPILE THIS WITH A FLASH SIZE THAT INCLUDES SPIFFS. This will guarantee space on ESP8266 to save this config below.
 */
 
#include <FS.h>                   //this needs to be first, or it all crashes and burns...
#include <SoftwareSerial.h>

#include <ESP8266WiFi.h>          //https://github.com/esp8266/Arduino

//needed for library
#include <DNSServer.h>
#include <ESP8266WebServer.h>
#include <WiFiManager.h>          //https://github.com/tzapu/WiFiManager

#include <ArduinoJson.h>          //https://github.com/bblanchon/ArduinoJson
#include <PubSubClient.h>

#define VIN A0 


int samplecount = 0;
int peekvalue = 0;
int SAMPLECOUNTSIZE = 1000;
int ONOFFTHRESHOLD = 886;
int lastvalue = 0;
int devicestatus = -1;


//define your default values here, if there are different values in config.json, they are overwritten.
char mqtt_server[40];
char mqtt_port[6] = "1883";
char mqtt_topic[40];


char chipid[40];

long count = 0;

//flag for saving data
bool shouldSaveConfig = false;


//callback notifying us of the need to save config
void saveConfigCallback () {
  Serial.println("Should save config");
  shouldSaveConfig = true;
}

WiFiClient espClient;
PubSubClient client(espClient);


void callback(char* topic, byte* payload, unsigned int length) {
 
  Serial.print("Message arrived in topic: ");
  Serial.println(topic);
 
  Serial.print("Message:");
  for (int i = 0; i < length; i++) {
    Serial.print((char)payload[i]);
  }
 
  Serial.println();
  Serial.println("-----------------------");
 
}


void setup() {
  
  // put your setup code here, to run once:
  Serial.begin(115200);
  Serial.println();

  String ci = "ESP8266" + String(ESP.getChipId());
  ci.toCharArray(chipid,40);

  //clean FS, for testing. This clears the saved config file
  //SPIFFS.format();

  //read configuration from FS json
  Serial.println("mounting FS...");

  if (SPIFFS.begin()) {
    Serial.println("mounted file system");
    if (SPIFFS.exists("/config.json")) {
      //file exists, reading and loading
      Serial.println("reading config file");
      File configFile = SPIFFS.open("/config.json", "r");
      if (configFile) {
        Serial.println("opened config file");
        size_t size = configFile.size();
        // Allocate a buffer to store contents of the file.
        std::unique_ptr<char[]> buf(new char[size]);

        configFile.readBytes(buf.get(), size);
        DynamicJsonBuffer jsonBuffer;
        JsonObject& json = jsonBuffer.parseObject(buf.get());
        json.printTo(Serial);
        if (json.success()) {
          Serial.println("\nparsed json");

          strcpy(mqtt_server, json["mqtt_server"]);
          strcpy(mqtt_port, json["mqtt_port"]);
          strcpy(mqtt_topic, json["mqtt_topic"]);

        } else {
          Serial.println("failed to load json config");
        }
        configFile.close();
      }
    }
  } else {
    Serial.println("failed to mount FS");
  }
  //end read



  // The extra parameters to be configured (can be either global or just in the setup)
  // After connecting, parameter.getValue() will get you the configured value
  // id/name placeholder/prompt default length
  WiFiManagerParameter custom_mqtt_server("server", "mqtt server", mqtt_server, 40);
  WiFiManagerParameter custom_mqtt_port("port", "mqtt port", mqtt_port, 6);
  WiFiManagerParameter custom_mqtt_topic("topic", "mqtt_topic", mqtt_topic, 40);

  //WiFiManager
  //Local intialization. Once its business is done, there is no need to keep it around
  WiFiManager wifiManager;

  //set config save notify callback
  wifiManager.setSaveConfigCallback(saveConfigCallback);

  //set static ip
  //wifiManager.setSTAStaticIPConfig(IPAddress(10,0,1,99), IPAddress(10,0,1,1), IPAddress(255,255,255,0));pi
  
  //add all your parameters here
  wifiManager.addParameter(&custom_mqtt_server);
  wifiManager.addParameter(&custom_mqtt_port);
  wifiManager.addParameter(&custom_mqtt_topic);

  //reset settings - for testing. Use this if you want to clear the WIFI setttings and reconfigure it.
  //wifiManager.resetSettings();

  //set minimu quality of signal so it ignores AP's under that quality
  //defaults to 8%
  //wifiManager.setMinimumSignalQuality();
  
  //sets timeout until configuration portal gets turned off
  //useful to make it all retry or go to sleep
  //in seconds
  //wifiManager.setTimeout(120);

  //fetches ssid and pass and tries to connect
  //if it does not connect it starts an access point with the specified name
  //here  "AutoConnectAP"
  //and goes into a blocking loop awaiting configuration
  if (!wifiManager.autoConnect("AutoConnectAP")) {
    Serial.println("failed to connect and hit timeout");
    delay(3000);
    //reset and try again, or maybe put it to deep sleep
    ESP.reset();
    delay(5000);
  }

  //if you get here you have connected to the WiFi
  Serial.println("connected...yeey :)");

  //read updated parameters
  strcpy(mqtt_server, custom_mqtt_server.getValue());
  strcpy(mqtt_port, custom_mqtt_port.getValue());
  strcpy(mqtt_topic, custom_mqtt_topic.getValue());

  Serial.println("MQTT");
  Serial.println(mqtt_server);
  Serial.println("Topic");
  Serial.println(mqtt_topic);

  //save the custom parameters to FS
  if (shouldSaveConfig) {
    Serial.println("saving config");
    DynamicJsonBuffer jsonBuffer;
    JsonObject& json = jsonBuffer.createObject();
    json["mqtt_server"] = mqtt_server;
    json["mqtt_port"] = mqtt_port;
    json["mqtt_topic"] = mqtt_topic;

    File configFile = SPIFFS.open("/config.json", "w");
    if (!configFile) {
      Serial.println("failed to open config file for writing");
    }

    json.printTo(Serial);
    json.printTo(configFile);
    configFile.close();
    //end save
  }

  Serial.println("local ip");
  Serial.println(WiFi.localIP());

  String port = mqtt_port;
  client.setServer(mqtt_server, port.toInt());
  client.setCallback(callback);
  
  while (!client.connected()) {
    Serial.println("Connecting to MQTT...");
 
    if (client.connect(chipid)) {
 
      Serial.println("connected");  
      //you can subscribe to topics here if you want to.
      //client.subscribe("projector");
 
    } else {
 
      Serial.print("failed with state ");
      Serial.print(client.state());
      //delay(2000);
    }
  }
}

void clientpublish(char* c) {
  Serial.println("Publishing");
  while (!client.connected()) {
    if (!client.connect(chipid)) {
      Serial.print("failed with state ");
      Serial.println(client.state());
    } else {
      Serial.println("Connected");
    }
  }   
  if(!client.publish(mqtt_topic, c)) {
    Serial.println("Failed to publish");
  } else {
    Serial.print("Published: ");
    Serial.println(c);
  }

}

void loop() {
  // put your main code here, to run repeatedly:

  while (!client.connected()) {
    Serial.println("Connecting to MQTT...");
 
    if (client.connect(chipid)) {
 
      Serial.println("connected");  
      //you can subscribe to topics here if you want to.
      //client.subscribe("projector");
 
    } else {
 
      Serial.print("failed with state ");
      Serial.print(client.state());
      //delay(2000);
    }
  } 
  client.loop();
  delay(10);

  int curvalue = analogRead(VIN);

 // 35 is the limit after which it means projector is ON with lights and all . Below that is either off or standbye

  //This sensor returns values that flunctuate even with current. The trick is to find the peek value in a sample size
  //and use that peek to see if it passing current.
  
  samplecount++;
  if(samplecount > SAMPLECOUNTSIZE) {
     //Serial.println(peekvalue);
     char buf[10];
     String valstring = String(peekvalue);
     valstring.toCharArray(buf,10);
     //Serial.println("1");
     //clientpublish(buf);
     if(peekvalue >= ONOFFTHRESHOLD) {
        if(devicestatus != 1) {
          devicestatus = 1;
          Serial.println(peekvalue);
          Serial.println("Projector is ON");
          clientpublish("1");
         // clientpublish(buf);
        }
     } else {
      //Serial.println("0");
        if(devicestatus != 0) {
          devicestatus = 0;
          Serial.println("Projector is OFF");
            //clientpublish(buf);
          clientpublish("0");
        }
     }
    /*
      if(peekvalue > THRESHOLD) {
        if(peekvalue != lastvalue and peekvalue != (lastvalue-1) and peekvalue != (lastvalue + 1)) {
            lastvalue = peekvalue;
            Serial.println(peekvalue);
            char buf[10];
            String valstring = String(peekvalue);
            valstring.toCharArray(buf,10);
            client.publish(mqtt_topic, buf);
        }
      } else {
        if(peekvalue != lastvalue and peekvalue != (lastvalue-1) and peekvalue != (lastvalue + 1)) {
            lastvalue = peekvalue;
            Serial.println("No Current");
            client.publish(mqtt_topic,"0");
        }
      } */
    samplecount=0;
    peekvalue = 0;
  } else {
    if(curvalue > peekvalue) {
      peekvalue = curvalue;
    }
  }


}
