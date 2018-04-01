#include <EEPROM.h>

#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>

#include <Adafruit_GFX.h>
#include <Adafruit_NeoMatrix.h>
#include <Adafruit_NeoPixel.h>

const int EEPROM_SIZE = 512;  //Can be max of 4096
const int WIFI_TIMEOUT = 30000; //In ms

int serialPhase = 0;

String ssid = "XXXXXXXX";
String password = "XXXXXXXX";

bool connectSuccess = false;

char host[] = "api.iextrading.com";

const int MAX_NUM_TICKERS = 50;
const int LED_MATRIX_WIDTH = 64;
const int LED_BRIGHTNESS = 30;
const int LED_UPDATE_TIME = 75;  //In ms

int numTickers = 0;
String tickers[MAX_NUM_TICKERS];
float values[MAX_NUM_TICKERS];
float changes[MAX_NUM_TICKERS];

const char* header = "<h1>Ticker Updater</h1>";
const char* tail = "<form  name='frm' method='post'>Ticker:<input type='text' name='ticker'><input type='submit' name ='Add' value='Add'><input type='submit' name='Remove' value='Remove'></form>";

unsigned long updateLEDTime = 0;

int currentTicker = 0;
int currentCursor = LED_MATRIX_WIDTH;


ESP8266WebServer server(80);

Adafruit_NeoMatrix *matrix = new Adafruit_NeoMatrix(32, 8, 2, 1, 4,
  NEO_MATRIX_TOP     + NEO_MATRIX_LEFT +
  NEO_MATRIX_COLUMNS + NEO_MATRIX_ZIGZAG,
  NEO_GRB + NEO_KHZ800);

void setup(){
  Serial.begin(115200);
  delay(10);

  //Read EEPROM to get ssid/password and stock tickers
  readEEPROM();

  //Connect to the wifi
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(ssid);
  WiFi.mode(WIFI_STA);
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
  matrix->begin();
  matrix->setTextWrap(false);
  matrix->setBrightness(LED_BRIGHTNESS);

  displayIP();
}

void loop(){
  server.handleClient();

  checkSerial();

  if(connectSuccess && currentCursor == LED_MATRIX_WIDTH)
  {
    updateCurrentTicker();
    Serial.println(tickers[currentTicker] + " $" + values[currentTicker] + " " + changes[currentTicker]); 
  }

  if(millis() - updateLEDTime > LED_UPDATE_TIME)
  {
    if(connectSuccess)
      displayStock();
    else
      displayNoConnection();
    updateLEDTime = millis();
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
  // Use WiFiClient class to create TCP connections
  WiFiClientSecure client;
  const int httpsPort = 443;
  if (!client.connect(host, httpsPort)) {
    Serial.println("connection failed");
    return;
  }

  // This will send the request to the server
  client.print(String("GET ") + "/1.0/stock/" + tickers[currentTicker] + "/quote" + " HTTP/1.1\r\n" +
    "Host: " + host + "\r\n" + 
    "Connection: close\r\n\r\n");
  unsigned long timeout = millis();
  while (client.available() == 0) {
    if (millis() - timeout > 5000) {
      Serial.println(">>> Client Timeout !");
      client.stop();
      return;
    }
  }

  //Read all the lines of the reply from server and print them to Serial
  char status[32] = {0};
  client.readBytesUntil('\r', status, sizeof(status));
  if (strcmp(status, "HTTP/1.1 200 OK") != 0) {
    Serial.print(status);
    Serial.print("Unexpected HTTP response");
  }

  //Skip Headers
  char endOfHeaders[] = "\r\n\r\n";
  client.find(endOfHeaders) || Serial.print("Invalid response");

  client.find("\"latestPrice\":");
  values[currentTicker] = client.parseFloat();
  client.find("\"change\":");
  changes[currentTicker] = client.parseFloat();
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
    matrix->clear();
    matrix->setCursor(currentCursor,0);
    matrix->setTextColor(matrix->Color(255,255,255));
    matrix->print(tickers[currentTicker] + " ");
    matrix->setTextColor(matrix->Color(255,255,0));
    matrix->print("$" + String(values[currentTicker]) + " ");
    if(changes[currentTicker] >=0)
    {
      matrix->setTextColor(matrix->Color(0,255,0));
      matrix->print("+" + String(changes[currentTicker]) + " ");
    }
    else
    {
      matrix->setTextColor(matrix->Color(255,0,0));
      matrix->print(String(changes[currentTicker]) + " ");
    }
    matrix->show();
    currentCursor--;
  }
  else
  {
    currentCursor = LED_MATRIX_WIDTH;
    currentTicker++;
    if(currentTicker >= numTickers)
      currentTicker = 0;
    Serial.println("Current ticker is: " + String(currentTicker));
  }
}

void displayNoConnection(){
  if(currentCursor > -65)
  {
    matrix->clear();
    matrix->setCursor(currentCursor,0);
    matrix->setTextColor(matrix->Color(255,255,255));
    matrix->print("Unable to connect to network. Check network settings and restart.");
    matrix->show();
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
    matrix->clear();
    matrix->setCursor(cur,0);
    matrix->setTextColor(matrix->Color(255,255,255));
    matrix->print(ip);
    matrix->show();
    delay(LED_UPDATE_TIME);
    cur--;
  }
}
