#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <stdio.h>
#include <FS.h>
#include <ArduinoOTA.h>
#include <ArduinoJson.h>
#include "ringstream.h"
#include "wifipw.h" //Contains my password and ssid so is not on the repository :)
#include <time.h>
#define BUFFERSIZE 0xFFF
//const char* ssid = "";
//const char* password = "";
//const char* otapw = "";
ESP8266WebServer server(80);
LoopbackStream stream(BUFFERSIZE); //Serial outgoing
LoopbackStream stream2(BUFFERSIZE); //Serial incoming + prints
const int led = LED_BUILTIN;

float lastHum = -1;
float lastTemp = -1;
float targetHum = -1;
float targetTemp = -1;

unsigned long delayStart = 0; // the time the delay started
bool delayRunning = false; // true if still waiting for delay to finish
bool blinking = false;
void OTASetup();

enum Command{
    NONE = 1,
    STATUS_RETURN,
    SET_TARGET_TEMP,
    SET_TARGET_HUM,
    GET_TEMP,
    GET_HUM,
    START_FLASH_LED,
    STOP_FLASH_LED
};

enum StatusReturns
{
    SUCCESS = 1,
    FAILURE
};

template<typename ... Args>
String string_format( const String& format, Args ... args )
{
    size_t size = snprintf( nullptr, 0, format.c_str(), args ... ) + 1; // Extra space for '\0'
    char* buf =  new char[ size ] ; 
    snprintf( buf, size, format.c_str(), args ... );
    String out = String( buf );
    free(buf);
    return out; // We don't want the '\0' inside
}

time_t getLocalTime()
{
  return (time_t)time(nullptr) - 7*3600;;
}

String getFormattedLocalTime()
{
  auto now = getLocalTime();
  tm split = *localtime(&now);
  return String(string_format("%d-%02d-%02d %02d:%02d:%02d", split.tm_year+1900, split.tm_mon+1, split.tm_mday, split.tm_hour, split.tm_min, split.tm_sec));
}

//TODO:
//REST CLIENT
//REST COMMANDS TO RECEIVE SERIAL READ VIA JAVASCRIPT
//CIRCULAR LIST FOR OUTPUT? OR just make a stream thing :shrug:
//SERIAL COMM WITH MSP430

void console_send() {
	long chars;
    if ((chars = stream.available())) {
        long i = 0;
        while( i< chars){
          auto c = stream.read();
          stream2.write(c);
          Serial.write(c);  
          delay(0); 
          i++;
    }
  }
}

void startMessageLine()
{
  stream2.print(getFormattedLocalTime()+": ");
}
template <class T>
void StatusPrintln(T&& line)
{
  startMessageLine();
  stream2.println(line);
}

void startCommand(Command cmd, int argcount)
{
  stream.print("$");stream.print((char)cmd); stream.print((char)argcount+1);
}

void writeArgument(const String&& value)
{
  stream.print("&"); stream.print(value);
}

void endCommand()
{
  stream.println("~");
}


void bufferSerial() {
  long chars;
  if ((chars = Serial.available())) {
    long i = 0;
    if(stream2.lastWritten() == '\n')//Check for last character in stream2 being \n
    {
      startMessageLine();
    }
    while ( i < chars) {
      char c = Serial.read();
      stream2.write(c);

      if(c=='\n' && i< (chars-1))
      {
        startMessageLine();
      }
      delay(0); 
      i++;
    }
  } 
}



String readFile(String path) { // send the right file to the client (if it exists)
  startMessageLine();
  stream2.print("handleFileRead: " + path);
  //if (path.endsWith("/")) path += "index.html";         // If a folder is requested, send the index file
  String contentType = "text/html";            // Get the MIME type
  if (SPIFFS.exists(path)) {                            // If the file exists
    File file = SPIFFS.open(path, "r");                 // Open it
    auto text = file.readString();
    file.close();
    //size_t sent = server.streamFile(file, contentType); // And send it to the client
    //file.close();                                       // Then close the file again
    return text;
  }
  stream2.println("\tFile Not Found");
  return "";                                         // If the file doesn't exist, return false
}
String getContentType(String filename){
  if(filename.endsWith(".htm")) return "text/html";
  else if(filename.endsWith(".html")) return "text/html";
  else if(filename.endsWith(".css")) return "text/css";
  else if(filename.endsWith(".js")) return "application/javascript";
  else if(filename.endsWith(".png")) return "image/png";
  else if(filename.endsWith(".gif")) return "image/gif";
  else if(filename.endsWith(".jpg")) return "image/jpeg";
  else if(filename.endsWith(".ico")) return "image/x-icon";
  else if(filename.endsWith(".xml")) return "text/xml";
  else if(filename.endsWith(".pdf")) return "application/x-pdf";
  else if(filename.endsWith(".zip")) return "application/x-zip";
  else if(filename.endsWith(".gz")) return "application/x-gzip";
  return "text/plain";
}
bool handleFileRead(String path){  // send the right file to the client (if it exists)
  startMessageLine();
  stream2.print("handleFileRead: " + path);
  if(path.endsWith("/")) path += "status.html";           // If a folder is requested, send the index file
  String contentType = getContentType(path);             // Get the MIME type
  if(SPIFFS.exists(path)){  // If the file exists, either as a compressed archive, or normal                                       // Use the compressed version
    File file = SPIFFS.open(path, "r");                    // Open the file
    size_t sent = server.streamFile(file, contentType);    // Send it to the client
    file.close();                                          // Close the file again
    stream2.println(String("\tSent file: ") + path);
    return true;
  }
  stream2.println(String("\tFile Not Found: ") + path);
  return false;                                          // If the file doesn't exist, return false
}

bool writeFile(String text, String path)
{
  StatusPrintln("Trying to write file...");
  int bufsize= text.length()*sizeof(char);
  StatusPrintln(string_format("file size is: %i", bufsize)); 
  FSInfo info;
  SPIFFS.info(info);
  unsigned int freebytes = info.totalBytes-info.usedBytes;
  StatusPrintln(string_format("free bytes on flash: %i", freebytes)); 
  return true;
}

String getFlashData()
{
  uint32_t realSize = ESP.getFlashChipRealSize();
  uint32_t ideSize = ESP.getFlashChipSize();
  FlashMode_t ideMode = ESP.getFlashChipMode();
  String mess = "";
  mess+=string_format("Flash real id:   %08X\n", ESP.getFlashChipId());
  mess+=string_format("Flash real size: %u bytes\n\n", realSize);

  mess+=string_format("Flash ide  size: %u bytes\n", ideSize);
  mess+=string_format("Flash ide speed: %u Hz\n", ESP.getFlashChipSpeed());
  mess+=string_format("Flash ide mode:  %s\n", (ideMode == FM_QIO ? "QIO" : ideMode == FM_QOUT ? "QOUT" : ideMode == FM_DIO ? "DIO" : ideMode == FM_DOUT ? "DOUT" : "UNKNOWN"));

  if (ideSize != realSize) {
    mess+=string_format("Flash Chip configuration wrong!\n");
  } else {
    mess+=string_format("Flash Chip configuration ok.\n");
  }
  FSInfo info;
  SPIFFS.info(info);
  mess+=string_format("total bytes on flash: %i\n", info.totalBytes);
  mess+=string_format("used bytes on flash: %i\n", info.usedBytes);
  return mess;
}

void handleSubmit() {
  digitalWrite(led, 1);
  String mess ="Submission received. \n Arguments:";
  mess += "\nMethod: ";
  mess += (server.method() == HTTP_GET)?"GET":"POST";
  mess+="\n";
  for(int i = 0; i < server.args();++i)
  {
    mess += server.argName(i) +": " + server.arg(i)+"\n";
  }
  server.sendHeader("Location", "/");
  server.send(303, "text/plain", mess);
  //delay(500);

  digitalWrite(led, 0);
}

void handleFlash()
{
  digitalWrite(led, 1);
  String mess =getFlashData();
  server.send(200, "text/plain", mess);
  digitalWrite(led, 0);
}

void handleNotFound(){
  digitalWrite(led, 1);
  String message = "File Not Found\n\n";
  message += "URI: ";
  message += server.uri();
  message += "\nMethod: ";
  message += (server.method() == HTTP_GET)?"GET":"POST";
  message += "\nArguments: ";
  message += server.args();
  message += "\n";
  for (uint8_t i=0; i<server.args(); i++){
    message += " " + server.argName(i) + ": " + server.arg(i) + "\n";
  }
  server.send(404, "text/plain", message);
  digitalWrite(led, 0);
}

void handleStatusData()
{
  StaticJsonBuffer<0x1FF> jsonBuffer; //TODO SOMETHING + STATUS BUFFER SIZE
  JsonObject& jsonObj = jsonBuffer.createObject();
  char JSONmessageBuffer[0x1FF];
  jsonObj["tval"] = lastTemp;
  jsonObj["hval"] = lastHum;
  jsonObj["ttarget"] = targetTemp;
  jsonObj["htarget"] = targetHum;
  jsonObj["status"] = stream2.available()?stream2.readString():""; //TODO move time thing to read buffers part on condition of encountering a \n :)
  jsonObj.prettyPrintTo(JSONmessageBuffer, sizeof(JSONmessageBuffer));
  server.send(200, "application/json", JSONmessageBuffer);
}

void handleUpdateTarget()
{
  int c = server.args();
  for(int i =0; i<c;++i)
  {
    StatusPrintln(string_format("Arg %i: %s", i, server.arg(i).c_str())); 
  }
  StatusPrintln("Target update successful."); 
  stream.print("$");stream.print((char)5); stream.print((char)1);stream.println("~");
  //stream.println(string_format("$%c%c&%c~", (char)START_FLASH_LED, (char)1, char(95)));
  blinking = ! blinking;
  server.send(202);
}

void handleSerialInput()
{
  bool nextcharisnumber=false;
  if(server.args() > 0&& server.hasArg("input"))
  {
    for(auto c : server.arg("input"))
    {
      if(c=='\\')
        nextcharisnumber=true;
      else
      {
        nextcharisnumber?stream.print((char)String(c).toInt()):stream.print(c);
        nextcharisnumber=false;
      }
    }
    //stream.println("");
  }
  server.send(200);
}

void addRESTSources()
{
  server.on("/get/status", HTTP_GET, handleStatusData);
  server.on("/update/target", HTTP_PUT, handleUpdateTarget);
  server.on("/update/serialinput", HTTP_PUT, handleSerialInput);
  
  
}

void setup(void){
  stream.clear();
  stream2.clear();
  pinMode(led, OUTPUT);
  digitalWrite(led, 0);
  Serial.begin(9600);
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  stream2.println("");

  startMessageLine();
  // Wait for connection
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    stream2.print(".");
  }
  stream2.println("");
  startMessageLine();
  stream2.print("Connected to ");
  stream2.println(ssid); 
  startMessageLine();
  stream2.print("IP address: ");
  stream2.println(WiFi.localIP()); 

  if (MDNS.begin("shrooms")) {
    StatusPrintln("MDNS responder started"); 
  }

  server.on("/submit", HTTP_POST, handleSubmit);
  server.on("/flash", HTTP_GET, handleFlash);

  addRESTSources();
  // server.on("/inline", [](){
  //   server.send(200, "text/plain", "this works as well");
  // });

  server.onNotFound([]() {                              // If the client requests any URI
      if (!handleFileRead(server.uri()))                  // send it if it exists
        handleNotFound(); // otherwise, respond with a 404 (Not Found) error
    });


  OTASetup();
  SPIFFS.begin();
  server.begin();
  StatusPrintln("HTTP server started");
  startMessageLine();
  stream2.print("free heap=");
  stream2.println(ESP.getFreeHeap());
  startMessageLine();
  stream2.print("free sketch space=");
  stream2.println(ESP.getFreeSketchSpace());
delayStart = millis();   // start delay
  delayRunning = true; // not finished yet
  configTime(10*3600, 0, "pool.ntp.org", "time.nist.gov");
  StatusPrintln("Waiting for time");
  startMessageLine();
  while (!time(nullptr)) {
    stream2.print(".");
    delay(1000);
  }
}

void loop(void){
  bufferSerial();

  ArduinoOTA.handle();
// if (delayRunning && ((millis() - delayStart) >= 2000)) {
//      stream2.print(stream2.available());
//   stream2.print(" | ");
//   stream2.println(stream2.pos);
//   delayStart = millis();
//   }
    server.handleClient();
  console_send();}

void OTASetup()
{
  ArduinoOTA.setHostname("root");
  ArduinoOTA.setPassword(otapw);
  ArduinoOTA.setPort(8266);
  ArduinoOTA.onStart([]() {
    stream2.println("Start");
  });
  ArduinoOTA.onEnd([]() {
    stream2.println("\nEnd");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    stream2.printf("Progress: %u%%\r", (progress / (total / 100)));
  });
  ArduinoOTA.onError([](ota_error_t error) {
    stream2.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) stream2.println("Auth Failed");
    else if (error == OTA_BEGIN_ERROR) stream2.println("Begin Failed");
    else if (error == OTA_CONNECT_ERROR) stream2.println("Connect Failed");
    else if (error == OTA_RECEIVE_ERROR) stream2.println("Receive Failed");
    else if (error == OTA_END_ERROR) stream2.println("End Failed");
  });
  ArduinoOTA.begin();
  stream2.println("OTA ready"); 

}