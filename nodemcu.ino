#include <NTPClient.h>
#include <ESP8266WiFi.h>
#include <WiFiUdp.h>
#include "DHT.h"
#include <Wire.h>
#include <Adafruit_BMP085.h>
#include <BH1750.h>
#include <EEPROM.h>
#include <ArduinoJson.h>
#include <ESP8266HTTPClient.h>

// === Necessary Variables Defining ===

// --- Wi-Fi Details---
const char* ssid = "Everything is";    //Wi-Fi ssid
const char* password = "David246";  //password

//--- adjusting the UTC offset to our timezone in milliseconds ---
//UTC +5.30 = (+ 5 * 60 *60) + (30 * 60) : 19800 s
const long utcOffsetInSeconds = 19800;

//--- NTP Client to get time ---
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", utcOffsetInSeconds);

//--- DHT11 (to get temperature and humidity) ---
#define DHTTYPE DHT11   
#define dht_dpin 0
DHT dht(dht_dpin, DHTTYPE); 

//--- BMP180 (to get pressure) ---
Adafruit_BMP085 BMP;
#define ALTITUDE 1655.0 // Altitude in meters

//--- BH1750 (to get ambient light intensity) ---
BH1750 lightMeter(0x23);

//--- REST API URL ---
const char* serverName = "http://192.168.43.142:3000/insert"; // localhost backend server URL API

//--- Other necessary variables ---
const int cacheArraySize = 30; // 2 data set per min for 15 min 30.
float temperatureCache[cacheArraySize];
float humidityCache[cacheArraySize];     // Caches for weather values
float pressureCache[cacheArraySize];
float ambientCache[cacheArraySize];

const int cacheWifiCacheSize = 4; // number of value sets for one hour
String wifiCache[cacheWifiCacheSize]; // cache to keep when wi-fi connection is lost.

const unsigned long loopTime = 15*60*1000;



// === Necessary Functions Defining ===

String getCurrentTime(){
  timeClient.update();
  String current_time = timeClient.getFormattedTime();
  return current_time;
}

unsigned long getCurrentEpochTime(){
  timeClient.update();
  unsigned long current_epoch_time = timeClient.getEpochTime();
  return current_epoch_time;
}

// --- Locally save the current weather values to EEPROM ---
void saveLocallyWeatherValues(String xml){
  
  for(int i=0;i<xml.length();i++)
  {
    EEPROM.write(0x0F+i, xml[i]); //Write one by one with starting address of 0x0F
  }
  EEPROM.commit();    //Store data to EEPROM

}

// --- Read locally save weather values from EEPROM ---
String readLocallyWeatherValues()
{
  char data[512]; //Max 100 Bytes
  int len=0;
  unsigned char k;
  k=EEPROM.read(0x0F);
  while(k != '\0' && len<512)   //Read until null character
  {    
    k=EEPROM.read(0x0F+len);
    data[len]=k;
    len++;
  }
  data[len]='\0';
  return String(data);
}

float mean(float arr[]){
  float sum = 0;
  for (int i=0; i<cacheArraySize; i++){
    sum+=arr[i];
  }
  return sum/(float) cacheArraySize;
}

// --- Function to connect to Wi-Fi initially ---
void initialWifiConnect(){
  delay(1000);
  WiFi.mode(WIFI_OFF);        //Prevents reconnection issue (taking too long to connect)
  delay(1000);
  WiFi.mode(WIFI_STA);        //This line hides the viewing of ESP as wifi hotspot
  WiFi.begin(ssid, password);     //Connect to your WiFi router
  Serial.println("");
  Serial.print("Connecting");
  // Wait for connection
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  //If connection successful show IP address in serial monitor 
  Serial.print("Connected to ");
  Serial.println(ssid);
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());  //IP address assigned to your ESP
}

// --- Function to connect to Wi-Fi if it loses connection -- 
void connectWifiWhenLostConnection(){
  WiFi.mode(WIFI_OFF);
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);   //Connect to your WiFi router
  Serial.print("Reconnecting"); //Wait for connection
  int connectionTimeOut = 0;
  while(WiFi.status() != WL_CONNECTED && con_time_out<20 ){ // try reconnecting for 10 seconds
    delay(500);
    Serial.print(".");
    connectionTimeOut++;
  }
  if (connectionTimeOut==20){
  Serial.println("Connection Failed!");
  } else{
  Serial.println("Reconnected!");
  } 
}

// --- Send the initially saved values in EEPROM to the server --- 
void postRequestSetup(String savedXml){
  http.begin(serverName);
      
  http.addHeader("Content-Type", "text/xml"); // set the header type (XML)
  int httpResponseCode = http.POST(savedXml);
  String payload = http.getString();
  http.end();
  if (httpResponseCode==200){
    Serial.print('Weather Values Succesfully Sent to the Server!');
    j++;
  } else{
    Serial.print('Error Occured!');
  }
  Serial.print('payload : ');
  Serial.println(payload);  
  
}

// --- send POST request to the backend server ---
int postRequestLoop(String xml){
  int cacheOverflowCheck = 1; 

  // save the weather values in cache
  for(int i=0; i<cacheWifiCacheSize; i++){
    if (wifiCache[i] == NULL || wifiCache[i]=="0"){
      wifiCache[i] = xml;
    cacheOverflowCheck = 0;
      break;
    }
  }
  // save values in the EEPROM
  saveLocallyWeatherValues(xml);
  
  int tries = 0;
  
  int j=0;
  while(cacheOverflowCheck==0&&j<cacheWifiCacheSize && tries<5){
  if (wifiCache[j]!=NULL && wifiCache[j]!="0") {
    if(WiFi.status()== WL_CONNECTED){
      Serial.println("WiFi Connected");
      HTTPClient http;
    
      // Domain name with the IP address with path
      http.begin(serverName);
      
      http.addHeader("Content-Type", "text/xml"); // set the header type (XML)
      int httpResponseCode = http.POST(wifiCache[j]);
      String payload = http.getString();
      http.end();
      if (httpResponseCode==200){
        Serial.print('Weather Values Succesfully Sent to the Server!');
        j++;
      } else{
        Serial.print('Error Occured!');
      }
      Serial.print('payload : ');
      Serial.println(payload);
      
      
    }
    else{
      Serial.println("WiFi Disconnected");
      connectWifiWhenLostConnection(); //trying to reconnect to Wi-Fi
      tries++;
    }
  }
  else{    
      j++;
    }
  }
  
  return cacheOverflowCheck;
}

//--- Function to convert the data values to CAP format ---
String dataToXml(float temperature, float humidity, float pressure, float ambient, String time)
{
  String head = "<alert xmlns='urn:oasis:names:tc:emergency:cap:1.2'><identifier>TRI13970876.2</identifier><sender>trinet@caltech.edu</sender><sent>"+time+"</sent><status>Actual</status><msgType>Update</msgType><scope>Private</scope><references>200</references><info><category>Env</category><event>Weather</event><urgency>Expected</urgency><severity>Minor</severity><certainty>Observed</certainty>";
  
  String temp = "<parameter><valuename>temperature</valuename><value>"+String(temperature)+"</value></parameter>"
  
  String pres = "<parameter><valuename>pressure</valuename><value>"+String(pressure)+"</value></parameter>";
  
  String hum =  "<parameter><valuename>humidity</valuename><value>"+String(humidity)+"</value></parameter>";
   
  String ambi = "<parameter><valuename>ambient</valuename><value>"+String(ambient)+"</value></parameter>";
  
  String tail = "<area><areadesc> Kirilawalla, Sri Lanka (7.030159, 79.980276)</areadesc></area></info></alert>";
  
  String xml = head + temp + pres + hum + ambi + tail;
  return xml;
}

// === main Code ===
void setup(){
  
  Serial.begin(115200);
  
  EEPROM.begin(512); //Initialize EEPROM
    
  dht.begin(); // start DHT11 sensor
  if (!BMP.begin()){ // start BMP180 sensor
    Serial.println("BMP180 init failed");
    while(1){}
  }
  lightMeter.begin() //start BH1750 sensor
  
  connectWifi(); // connect Wi-Fi
  
  String initialWeatherValues = readLocallyWeatherValues();
  
  if(initialWeatherValues!='/0'){
    postRequestSetup(initialWeatherValues);
  }
    
  timeClient.begin(); // start time 
}

unsigned long lastUpdatedTime = getCurrentEpochTime();
int jj = 0; // temporary counter

void loop() {
  
  float h = dht.readHumidity(); // in %
  float t = dht.readTemperature(); // in Celcius (C)
  float p = BMP.readSealevelPressure(); // in Pascal (Pa)
  float a = (float)lightMeter.readLightLevel(); // in Lux (lx)
  
  temperatureCache[jj] = t;
  humidityCache[jj] = h;
  pressureCache[jj] = p;
  ambientCache[jj] = a;
  jj+=1;
  
  unsigned long currentTimeGap = getCurrentEpochTime()-lastUpdatedTime;
  
  if (currentTimeGap>=loopTime){
  float tMean = mean(temperatureCache);
  float hMean = mean(humidityCache);
  float pMean = mean(pressureCache);
  float aMean = mean(ambientCache);
  
  String currentTime = getFormattedTime();
  
  // String dataToXml(float temperature, float humidity, float pressure, float ambient, String time)
  String xml = dataToXml(tMean,hMean,pMean,aMean,currentTime);
  int cacheCheck = postRequestLoop(xml);
  
  if(cacheCheck==1){
    Serial.print('Cache Overflow Occured!');
  }
  
  temperatureCache[cacheArraySize] = {0};
  humidityCache[cacheArraySize] = {0};    
  pressureCache[cacheArraySize] = {0};
  ambientCache[cacheArraySize] = {0};
  
  jj = 0;
  lastUpdatedTime = getCurrentEpochTime();
  }

  delay(30000); // wait 30s to read the next value set
} 
