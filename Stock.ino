#include <EEPROM.h>

#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>

#include <Adafruit_GFX.h>
#include <Adafruit_NeoMatrix.h>
#include <Adafruit_NeoPixel.h>

#include <String.h>
#include <NTPClient.h>
#include <Time.h>
#include <TimeLib.h>
#include <Timezone.h>

#define ARDUINOJSON_USE_DOUBLE 1
#include <ArduinoJson.h>

#include "keys.h"

// Stock ticker settings
const int EEPROM_SIZE = 512;  //Can be max of 4096
const int WIFI_TIMEOUT = 30000; //In ms
int serialPhase = 0;
bool connectSuccess = false;
bool startUpCall = true;

#define HOST  "apidojo-yahoo-finance-v1.p.rapidapi.com"

// For HTTPS requests
WiFiClientSecure client;

// Clock Settings
#define NTP_OFFSET   60 * 60      // In seconds
#define NTP_INTERVAL 60 * 1000    // In miliseconds
#define NTP_ADDRESS  "us.pool.ntp.org"  // change this to whatever pool is closest (see ntp.org)
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, NTP_ADDRESS, NTP_OFFSET, NTP_INTERVAL);

// Variables for timing
const int MAX_NUM_TICKERS = 50;
const int LED_MATRIX_WIDTH = 32;
const int LED_BRIGHTNESS = 30;
const int LED_UPDATE_TIME = 75;  //In ms
const int QUOTE_UPDATE_TIME = 1800000;
const int CLOCK_UPDATE_TIME = 100;
const int DATE_UPDATE_TIME = 300000;

unsigned long updateLEDTime = 0;
unsigned long updateQuoteTime = 0;
unsigned long updateClockTime = 0;
unsigned long updateDateTime = 0;

// Variables for storing quote data
int numTickers = 0;
String tickers[MAX_NUM_TICKERS];
float values[MAX_NUM_TICKERS];
double changes[MAX_NUM_TICKERS];

// Ticker Change website
const char* header = "<h1>Ticker Updater</h1>";
const char* tail = "<form  name='frm' method='post'>Ticker:<input type='text' name='ticker'><input type='submit' name ='Add' value='Add'><input type='submit' name='Remove' value='Remove'></form>";

int currentTicker = 0;
int currentCursor = LED_MATRIX_WIDTH;

ESP8266WebServer server(80);

// Variables for clock
String date;
String t;
const char * days[] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"} ;
const char * months[] = {"Jan", "Feb", "Mar", "Apr", "May", "June", "July", "Aug", "Sep", "Oct", "Nov", "Dec"} ;
const char * ampm[] = {"AM", "PM"} ;

// LED Matrix setup
Adafruit_NeoMatrix matrix = Adafruit_NeoMatrix(32, 8, 2, 1, 0,
  NEO_MATRIX_BOTTOM + NEO_MATRIX_RIGHT +
  NEO_MATRIX_COLUMNS + NEO_MATRIX_ZIGZAG,
  NEO_GRB + NEO_KHZ800);

void setup(){
  Serial.begin(115200);
  delay(10);

  //Read EEPROM to get ssid/password and stock tickers
  readEEPROM();

  timeClient.begin();   // Start the NTP UDP client

  //Connect to the wifi
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(ssid);
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  WiFi.begin(ssid.c_str(), password.c_str());

  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
    if(millis() - start > WIFI_TIMEOUT)
    {
      connectSuccess = false;
      Serial.println("");
      Serial.println("Failed to connect to wifi."); 
      return;
    }
  }
  connectSuccess = true;

  Serial.println("");
  Serial.println("WiFi connected");  
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());

  //Start the server for the webpage
  server.on("/", HTTP_GET, defaultPage);
  server.on("/", HTTP_POST, response);
  server.begin();

  //Setup the LED matrix
  matrix.begin();
  matrix.setTextWrap(false);
  matrix.setBrightness(LED_BRIGHTNESS);

  displayIP();

  //Set insecure so fingerprints are not needed
  client.setInsecure();
}

// Clock Setup
long lastTime = millis();
int count_sec = 0;


void loop(){
  // Update time
  if(millis()-lastTime>=1000){
    count_sec++;
    if(count_sec>70)
      count_sec = 0;
    lastTime = millis();
    }

  if(count_sec>60 || startUpCall){
    read_time_date();
  }
    
  server.handleClient();

  checkSerial();

  // Setup vars for time conditions
  String hour_string = t.substring(0,2);
  String am_pm = t.substring(6,8);

  if(hour_string == "08" && am_pm == "AM" || hour_string == "09" && am_pm == "AM" || hour_string == "10" && am_pm == "AM" || hour_string == "11" && am_pm == "AM" ||
      hour_string == "12" && am_pm == "PM" || hour_string == "01" && am_pm == "PM" || hour_string == "02" && am_pm == "PM" || hour_string == "03" && am_pm == "PM" ||
      hour_string == "04" && am_pm == "PM"){
    // Call Yahoo finance API every 30 mins, or at start up to update quote data
      if(connectSuccess && currentCursor == LED_MATRIX_WIDTH && millis() - updateQuoteTime > QUOTE_UPDATE_TIME || connectSuccess && currentCursor == LED_MATRIX_WIDTH && startUpCall )
      {
        if(startUpCall == true){
          startUpCall = false;
        }
        updateCurrentTicker();
        updateQuoteTime = millis();
    }
      // Call function to update stock data displayed
    if(millis() - updateLEDTime > LED_UPDATE_TIME){
      if(connectSuccess){
        displayStock();
      }else{
        displayNoConnection();
      }
      updateLEDTime = millis();
    }
  }else{
    displayTime();
  } 
}

void checkSerial(){
  String tmp;

  while(Serial.available() > 0)
  {
    tmp = Serial.readStringUntil('\n');
    if(serialPhase == 0 && tmp == "wifi")
    {
      Serial.println("Changing wifi settings.");
      Serial.print("SSID: ");
      serialPhase++;
    }
    else if(serialPhase == 1)
    {
      ssid = tmp;
      Serial.print(ssid + "\nPassword: ");
      serialPhase++;
    }
    else if(serialPhase == 2)
    {
      password = tmp;
      Serial.println(password + "\nUpdated the SSID and Password");
      writeEEPROM();
      serialPhase = 0;
      break;
    }
  }
}


void updateCurrentTicker(){
  Serial.println("Updating Tickers");
  // Use WiFiClient class to create TCP connections
  if (!client.connect(HOST, 443)) {
    Serial.println("connection failed");
    return;
  }
  
  // give the esp a breather
  //yield();

  // Send HTTPS request
  client.print(F("GET "));
  // This is the second half of a request (everything that comes after the base URL)
  client.print(F("/market/v2/get-quotes?region=US&symbols="));
  for(int i = 0; i < numTickers; i++){
    client.print(tickers[i]);
    //Serial.print(tickers[i]);
    if(i != (numTickers - 1)){
      client.print(F("%2C"));
      //Serial.print("%2C");
    }
  };
  client.println(F(" HTTP/1.1"));

  //Headers
  client.print(F("Host: "));
  client.println(HOST);
  client.print(F("x-rapidapi-key: "));
  client.println(X_RAPID_KEY);
  client.print(F("x-rapidapi-host: "));
  client.println(HOST);
  //End of request
  client.println(F("Cache-Control: no-cache"));
  //client.println(F("Connection: close"));
  
  if (client.println() == 0)
  {
    Serial.println(F("Failed to send request"));
    return;
  }

  // Check HTTP status
  char status[32] = {0};
  client.readBytesUntil('\r', status, sizeof(status));
  if (strcmp(status, "HTTP/1.1 200 OK") != 0)
  {
    Serial.print(F("Unexpected response: "));
    Serial.println(status);
    return;
  }

  // Skip HTTP headers
  char endOfHeaders[] = "\r\n\r\n";
  if (!client.find(endOfHeaders)) {
    Serial.println(F("Invalid response"));
    client.stop();
    return;
  }

  while (client.available()) {
    // The filter: it contains "true" for each value we want to keep
    StaticJsonDocument<200> filter;
    JsonObject filter_quoteResponse_result_0 = filter["quoteResponse"]["result"].createNestedObject();
    filter_quoteResponse_result_0["regularMarketChange"] = true;
    filter_quoteResponse_result_0["regularMarketPrice"] = true;

    // Setup Document Size, this is considerably larger than suggested to hopefully acomodate more quote requests
    StaticJsonDocument<500> doc;
    // Deserialize JSON response and check for errors
    DeserializationError error = deserializeJson(doc, client, DeserializationOption::Filter(filter));
    
    if (error) {
      Serial.print(F("deserializeJson() failed: "));
      Serial.println(error.f_str());
      return;
    }
    // Pull the value and change for each ticker requested. This has to be done in a for loop to make sure that all of the data
    // is gotten. 
    for(int i = 0; i < numTickers; i++){
      changes[i] = doc["quoteResponse"]["result"][i]["regularMarketChange"];
      values[i] = doc["quoteResponse"]["result"][i]["regularMarketPrice"];
    }
  }
}

String listOfTickers(){
  String html = "<ul style=\"list-style-type:none\">";
  for(int i=0; i < numTickers; i++)
  {
    html += "<li>" + tickers[i] + "</li>";
  }

  html += "</ul>";
  return html;
}

void defaultPage(){
  webpage("");
}

void webpage(String status) {
  server.send(200, "text/html", header + status + listOfTickers() + tail);
}

void response(){
  if(server.hasArg("Add") && (server.arg("ticker").length()>0)){
    if(numTickers != MAX_NUM_TICKERS)
    {
      tickers[numTickers] = server.arg("ticker");
      numTickers++;
      writeEEPROM();
      webpage("<p><font color=\"green\">Successfully added " + server.arg("ticker") + "</font></p>");
    }
    else
    {
      webpage("<p><font color=\"red\">At maximum number of tickers!</font></p>"); 
    }
  } 
  else if(server.hasArg("Remove") && (server.arg("ticker").length()>0)){
    String tick = server.arg("ticker");
    bool found = false;
    for(int i=0; i < numTickers; i++)
    {
      if(tickers[i] == tick)
        found = true;
      if(found && (i != (numTickers-1)))
        tickers[i] = tickers[i+1];
    }
    if(found)
    {
      numTickers--;
      tickers[numTickers] = "";
      writeEEPROM();
      webpage("<p><font color=\"green\">Successfully removed " + tick + "</font></p>");
    }
    else
    {
      webpage("<p><font color=\"red\">Could not find " + tick + "</font></p>");
    }
  }
}

void readEEPROM(){
  String string = "";
  int phase = 0;
  EEPROM.begin(EEPROM_SIZE);
  for(int i=0; i < EEPROM_SIZE; i++)
  {
    char tmp = EEPROM.read(i);
    if(phase == 0 && tmp == NULL)
    {
      ssid = string;
      string = "";
      phase++;
    }
    else if(phase == 1 && tmp == NULL)
    {
      password = string;
      string = "";
      phase++;
    }
    else if(phase == 2 && tmp == ';')
    {
      tickers[numTickers] = string;
      numTickers++;
      string = "";
    }
    else if(phase == 2 && tmp == NULL)
    {
      break;
    }
    else
      string += tmp;
  }
}

void writeEEPROM(){
  int EEPROMAddr = 0;
  //Write out SSID
  for(int i=0; i < ssid.length(); i++)
  {
    EEPROM.write(EEPROMAddr, ssid.charAt(i));
    EEPROMAddr++;
  }
  EEPROM.write(EEPROMAddr, NULL);
  EEPROMAddr++;
  //Write out password
  for(int i=0; i < password.length(); i++)
  {
    EEPROM.write(EEPROMAddr, password.charAt(i));
    EEPROMAddr++;
  }
  EEPROM.write(EEPROMAddr, NULL);
  EEPROMAddr++;
  //Write out tickers
  for(int i=0; i < numTickers; i++)
  {
    for(int j=0; j < tickers[i].length(); j++)
    {
      EEPROM.write(EEPROMAddr, tickers[i].charAt(j));
      EEPROMAddr++;
    }
    EEPROM.write(EEPROMAddr, ';');
    EEPROMAddr++;
  }
  while(EEPROMAddr <EEPROM_SIZE)
  {
    EEPROM.write(EEPROMAddr, 0);
    EEPROMAddr++;
  }
  EEPROM.commit();
}

void displayStock(){
  //Add up number of characters in each text. Dont forget spaces and characters added. Then multiply by 6 (font is 5 pixel + 1 space pixel).
  int sign = changes[currentTicker] >=0 ? 1 : 0;
  int bits = (tickers[currentTicker].length() + String(values[currentTicker]).length() + String(changes[currentTicker]).length() + sign + 3) * -6; 
  if(currentCursor > bits)
  {
    matrix.clear();
    matrix.setCursor(currentCursor,0);
    matrix.setTextColor(matrix.Color(255,255,255));
    matrix.print(tickers[currentTicker] + " ");
    matrix.setTextColor(matrix.Color(255,255,0));
    matrix.print("$" + String(values[currentTicker]) + " ");
    if(changes[currentTicker] >=0)
    {
      matrix.setTextColor(matrix.Color(0,255,0));
      matrix.print("+" + String(changes[currentTicker]) + " ");
    }
    else
    {
      matrix.setTextColor(matrix.Color(255,0,0));
      matrix.print(String(changes[currentTicker]) + " ");
    }
    matrix.show();
    currentCursor--;
  }
  else
  {
    currentCursor = LED_MATRIX_WIDTH;
    currentTicker++;
    if(currentTicker >= numTickers)
      currentTicker = 0;
    Serial.println("Current ticker is: " + String(currentTicker));
    Serial.println(tickers[(currentTicker)] + " $" + values[(currentTicker)] + " " + changes[(currentTicker)]);
  }
}

void displayNoConnection(){
  if(currentCursor > -65)
  {
    matrix.clear();
    matrix.setCursor(currentCursor,0);
    matrix.setTextColor(matrix.Color(255,255,255));
    matrix.print("Unable to connect to network. Check network settings and restart.");
    matrix.show();
    currentCursor--;
  }
  else
  {
    currentCursor = LED_MATRIX_WIDTH;
  }
}

void displayIP(){
  int cur = LED_MATRIX_WIDTH;
  String ip = WiFi.localIP().toString();
  int chars = ip.length() * -6;
  
  while(cur > chars)
  {
    matrix.clear();
    matrix.setCursor(cur,0);
    matrix.setTextColor(matrix.Color(255,255,255));
    matrix.print(ip);
    matrix.show();
    delay(LED_UPDATE_TIME);
    cur--;
  }
}

void read_time_date(){
    date = "";  // clear the variables
    t = "";
    
    // update the NTP client and get the UNIX UTC timestamp 
    timeClient.update();
    unsigned long epochTime =  timeClient.getEpochTime();

    // convert received time stamp to time_t object
    time_t local, utc;
    utc = epochTime;

    // Then convert the UTC UNIX timestamp to local time
    TimeChangeRule usEDT = {"EDT", Second, Sun, Mar, 2, -360};  //UTC - 5 hours - change this as needed
    TimeChangeRule usEST = {"EST", First, Sun, Nov, 2, -420};   //UTC - 6 hours - change this as needed
    Timezone usEastern(usEDT, usEST);
    local = usEastern.toLocal(utc);

    // now format the Time variables into strings with proper names for month, day etc
    date += days[weekday(local)-1];
    date += ", ";
    date += months[month(local)-1];
    date += " ";
    date += day(local);
    date += ", ";
    date += year(local);

    // format the time to 12-hour format with AM/PM and no seconds
    t += hourFormat12(local);
    t += ":";
    if(minute(local) < 10)  // add a zero if minute is under 10
      t += "0";
    t += minute(local);
    t += " ";
    t += ampm[isPM(local)];
    if(t.length() < 8){
      t = "0" + t;
    }
    //Serial.println(date);
    //Serial.println(t);
}

void displayTime(){
  if(millis() - updateClockTime > CLOCK_UPDATE_TIME ){
    matrix.fillScreen(0);
    matrix.setCursor(1, 0);
    matrix.setTextColor(matrix.Color(255, 255, 255));
    matrix.print(t);
    matrix.show();
    
    updateClockTime = millis();
  }else if(millis() - updateDateTime > DATE_UPDATE_TIME){
    int cur = LED_MATRIX_WIDTH;
    int chars = date.length() * -6;
    
    while(cur > chars)
    {
      matrix.clear();
      matrix.setCursor(cur,0);
      matrix.setTextColor(matrix.Color(255,255,255));
      matrix.print(date);
      matrix.show();
      delay(LED_UPDATE_TIME);
      cur--;
    }
    updateDateTime = millis();
  }
}
