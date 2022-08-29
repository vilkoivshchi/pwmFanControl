#include <ESP8266WiFi.h>
#include <EEPROM.h>
#include <ArduinoJson.h>

#include <OneWire.h>

#define ANALOG_PIN A0
#define PWM_PIN D1
#define TACHO_PIN_1 D5
#define TACHO_PIN_2 D6
#define ONE_WIRE_PIN D4
#define FREQ 25000

const char apSsid[] = "ESP8266";
const char *ssid = "MySSID";
const char *WiFiPass = "MyPass";

char *userSsid[32] = {0};
char *userWifiPass[32] = {0};

const uint8_t arraySize128 = 128;
const uint8_t arraySize32 = 32;
const uint8_t arraySize16 = 16;
const uint8_t arraySize8 = 8;
const uint8_t arraySize4 = 4;

unsigned long previousMillis = 0;
unsigned long tachoInterval = 1000;
unsigned long tempMeasureInterval = 5000;
unsigned long prevousTempMillis = 0;
volatile unsigned long tachoCounter1 = 0;
volatile unsigned long tachoCounter2 = 0;
uint16_t currentRpm1 = 0;
uint16_t currentRpm2 = 0;

IPAddress ip(0, 0, 0, 0);
IPAddress gateway(0, 0, 0, 0);
IPAddress mask(0, 0, 0, 0);

uint16_t pwmValue = 220;

bool isManualRegulation = false;

const uint8_t eepromSsidAddress = 47;
const uint8_t eepromWifiPassAddress = 79;
// this var need when you will change AP
uint8_t wifiConnRes = 0;
uint8_t oneWireAddress[arraySize8] = {0};
bool isDSPresent = false;
int8_t currentTemperature = -127;

uint8_t minPwmTemperature = 25;
uint8_t maxPwmTemperature = 35;
bool isWifiInStationMode = true;

/*
0...3 == IP
4...7 == subnet
8...11 == gateway
12...28 == web username
29...45 == web password
46 == auto/manual PWM control
47...78 == SSID
79...110 == WiFi pass
111 == должно быть \0. Иначе обнуляется EEPROM. Так задумано.
*/
const int eepromSize = 112;
const uint8_t isEEPromCorrectIndex = 111;
const uint8_t IPFromEEPROMIndex = 0;
const uint8_t subnetFromEEPROMIndex = 4;
const uint8_t gatewayFromEEPROMIndex = 8;

WiFiServer webserver(80);

OneWire ds(ONE_WIRE_PIN);

void setup()
{
  Serial.begin(115200);
  EEPROM.begin(eepromSize);
  if (EEPROM.read(isEEPromCorrectIndex) != 0)
  {
    EEPROMClear(eepromSize);
  }
  Serial.println();
  for (uint8_t k = 0; k < eepromSize; k++)
  {
    Serial.printf("%02x", EEPROM.read(k));
  }
  Serial.println();
  ReadIPFromEEPROM();
  char userSsid[32] = {0};
  ReadSSIDFromEEPROM(userSsid);
  char userWifiPass[32] = {0};
  ReadWiFiPassFromEEPROM(userWifiPass);
  const char* userEEPROMSSID = userSsid;
  const char* userEEPROMWWIFIPass = userWifiPass;
  if(userSsid[0] == 0)
  {
    WifiSetup(ssid, WiFiPass, ip, gateway, mask, apSsid);
  }
  else
  {
    WifiSetup(userEEPROMSSID, userEEPROMWWIFIPass, ip,  gateway, mask, apSsid);
  }
  webserver.begin();
  PwmSetup(ANALOG_PIN, TACHO_PIN_1, TACHO_PIN_2, FREQ);
  DsSetup();
}

IRAM_ATTR void OnPin1StateChange()
{
  tachoCounter1++;
}

IRAM_ATTR void OnPin2StateChange()
{
  tachoCounter2++;
}
void ReadSSIDFromEEPROM(char * userSSID)
{
  for(uint8_t i = 0; i < arraySize32; i++)
  {
    userSSID[i] = EEPROM.read(eepromSsidAddress + i);
  }
}

void ReadWiFiPassFromEEPROM(char * userPass)
{
  for(uint8_t i = 0; i < arraySize32; i++)
  {
    userPass[i] = EEPROM.read(eepromWifiPassAddress + i);
  }
}

void ReadIPFromEEPROM()
{
  for (uint8_t i = IPFromEEPROMIndex, j = 0; i < IPFromEEPROMIndex + arraySize4; i++, j++)
  {
    ip[j] = EEPROM.read(i);
  }
  for (uint8_t i = subnetFromEEPROMIndex, j = 0; i < subnetFromEEPROMIndex + arraySize4; i++, j++)
  {
    mask[j] = EEPROM.read(i);
  }
  for (uint8_t i = gatewayFromEEPROMIndex, j = 0; i < gatewayFromEEPROMIndex + arraySize4; i++, j++)
  {
    gateway[j] = EEPROM.read(i);
  }
}

void SaveIPToEEPROM(uint8_t EEPROMIndex, IPAddress ip)
{
  for (uint8_t i = EEPROMIndex, j = 0; i < EEPROMIndex + arraySize4; i++, j++)
  {
    EEPROM.put(i, ip[j]);
  }
  EEPROM.commit();
  Serial.print("Address ");
  Serial.print(ip);
  Serial.println(" was saved to EEPROM");
}

void DsSetup()
{
  if (ds.search(oneWireAddress))
  {
    Serial.print("Found sensor ");
    for (uint8_t i = 0; i < arraySize8; i++)
    {
      Serial.printf("%02x", oneWireAddress[i]);
    }
    Serial.println();
    isDSPresent = true;
  }
  else
  {
    Serial.println("No DS sensors found!");
  }
}

int8_t MeasureTemperature()
{
  uint8_t rawData[12] = {0};
  ds.reset();
  ds.select(oneWireAddress);
  ds.write(0x44, 0);
  ds.reset();
  delay(200);
  ds.select(oneWireAddress);
  ds.write(0xBE);
  for (uint8_t i = 0; i < 9; i++)
  {
    rawData[i] = ds.read();
  }

  int16_t raw = (rawData[1] << 8) | rawData[0];
  uint8_t cfg = (rawData[4] & 0x60);
  if (cfg == 0x00)
    raw = raw & ~7; // 9 bit resolution, 93.75 ms
  else if (cfg == 0x20)
    raw = raw & ~3; // 10 bit res, 187.5 ms
  else if (cfg == 0x40)
    raw = raw & ~1; // 11 bit res, 375 ms
  return raw / 16;
}

void EEPROMClear(uint8_t EEPROMSize)
{
  for (uint8_t i = 0; i < EEPROMSize; i++)
  {
    EEPROM.write(i, 0);
  }
  if (EEPROM.commit())
  {
    Serial.println("EEPROM was cleared");
  }
}

void PwmSetup(uint8_t analogPin, uint8_t tachoPin1, uint8_t tachoPin2, uint16_t freq)
{
  pinMode(analogPin, INPUT);
  analogWriteFreq(freq);
  pinMode(tachoPin1, INPUT_PULLUP);
  pinMode(tachoPin2, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(tachoPin1), OnPin1StateChange, FALLING);
  attachInterrupt(digitalPinToInterrupt(tachoPin2), OnPin2StateChange, FALLING);
}

// void WifiSetup()
void WifiSetup(const char *staSsid, const char *staPass, IPAddress staLocalIp, IPAddress staGw, IPAddress staMask, const char apSSID[])
{

  WiFi.mode(WIFI_STA);
  

  Serial.println(staLocalIp);
  Serial.println(staGw);
  Serial.println(staMask);

  if (!WiFi.config(staLocalIp, staGw, staMask))
  {
    Serial.println("WiFi config failed");
  }
  WiFi.begin(staSsid, staPass);
  uint8_t WifiConnectAttempts = 10;
  Serial.println();
  Serial.print("Connecting to ");
  Serial.print(staSsid);
  Serial.println();
  for (uint8_t i = 0; i < WifiConnectAttempts; i++)
  {

    if (WiFi.status() != WL_CONNECTED)
    {
      delay(1000);
      Serial.print(".");
    }
    else
    {
      Serial.println("Connected to " + WiFi.SSID());
      delay(1000);
      Serial.print("Local IP: ");
      Serial.println(WiFi.localIP());
      
      return;
    }
  }
  if (WiFi.status() != WL_CONNECTED)
  {
    Serial.print("Can't connect, creating network ");
    Serial.print(apSSID);
    Serial.println();
    WiFi.mode(WIFI_AP);
    WiFi.softAP(apSSID);
    isWifiInStationMode = false;
  }
}

void JsonWebPage(WiFiClient webClient)
{
  StaticJsonDocument<128> doc;
  doc["pwm"] = pwmValue;
  doc["rpm1"] = currentRpm1;
  doc["rpm2"] = currentRpm2;
  doc["temp"] = currentTemperature;
  doc["man_control"] = isManualRegulation;

  webClient.println(F("HTTP/1.1 200 OK"));
  webClient.println(F("Content-Type: application/json"));
  webClient.println(F("Connection: close"));
  webClient.print(F("Content-Length: "));
  webClient.println(measureJsonPretty(doc));
  webClient.println();
  serializeJsonPretty(doc, webClient);
}

void RootWebPage(WiFiClient webClient)
{
  webClient.println(F("HTTP/1.1 200 OK"));
  webClient.println(F("Content-Type: text/html"));

  webClient.println(F("Cache-Control: no-Cache"));
  webClient.println(F("Connection: close"));
  webClient.println();

  webClient.println(F("<!DOCTYPE HTML>"));
  webClient.println(F("<html><head><title>Fan control</title><meta http-equiv=\"Content-Type\" content=\"text/html; utf-8\">"));
  webClient.println(F("<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\"><link rel=\"shortcut icon\" href=\"#\">"));
  webClient.println(F("<style>body { background: #FE938C; font-family: Georgia, serif; font-size: large; margin: 0;}"));
  webClient.println(F(".rangeslider {-webkit-appearance: none; width: 100%; height: 34px; background: #9CAFB7; outline: none; margin-left: 10px; border-radius: 34px;}"));
  webClient.println(F(".rangeslider::-webkit-slider-thumb { -webkit-appearance: none; appearance: none; width: 26px; height: 26px; background: #ffd166; cursor: pointer; border-radius: 34px;}"));
  webClient.println(F(".rangeslider.before { border-radius: 50%; }"));
  //webClient.println(F(".switch { position: relative; display: inline-block; width: 60px; height: 34px; }"));
  //webClient.println(F(".switch input { opacity: 0; width: 0; height: 0; }"));
  //webClient.println(F(".slider { position: absolute; cursor: pointer; top: 0; left: 0; right: 0; bottom: 0; background-color: #ccc; -webkit-transition: .4s; transition: .4s; }"));
  //webClient.println(F(".slider:before { position: absolute; content: \"\"; height: 26px; width: 26px; left: 4px; bottom: 4px; background-color: #ffd166; -webkit-transition: .4s; transition: .4s; }"));
  webClient.println(F("input:checked + .slider { background-color: #9CAFB7; }"));
  webClient.println(F("input:focus + .slider { box-shadow: 0 0 1px #9CAFB7; }"));
  webClient.println(F("input:checked + .slider:before { -webkit-transform: translateX(26px); -ms-transform: translateX(26px); transform: translateX(26px); }"));
  webClient.println(F(".slider.round { border-radius: 34px; } .slider.round:before { border-radius: 50%; }"));
  webClient.println(F(".button { width: 120px; height: 34px; border-radius: 34px; border: none; font-size: large; background-color: #9CAFB7; color: #ffd166; margin-left: 10px;}</style></head>"));
  webClient.println(F("<body><table style=\"width:100%\">"));
  webClient.println(F("<tr><td><form id=\"man_pwm_change\" action=\"/\" method=\"post\">"));
  /*
  webClient.println(F("<label class=\"switch\"><input type=\"checkbox\" onChange=\"this.form.submit()\" name=\"is_man_pwm\" value=\"dummy\" "));
  webClient.print(isManualRegulation ? "checked>" : ">");
  webClient.println(F("<span class=\"slider round\"></span></label></form></td></tr><tr>"));
  */
  webClient.println(F("<input type=\"submit\" name=\"is_man_pwm\" value=\"Switch\" class=\"button\"></form></td>"));
  webClient.print(isManualRegulation ? "<td>Fan: Manual</td></tr>" : "<td>Fan: Auto</td></tr>");
  webClient.print(F("<tr><td width=\"80%\"><form id=\"auto_man_pwm\" method=\"post\"><input type=\"range\" name=\"pwm_value\" step=\"10\" min=\"1\" max=\"255\" value=\""));
  webClient.print(pwmValue);
  webClient.println(F("\" class=\"rangeslider\"></td>"));
  webClient.println(F("<td><input type=\"submit\" value=\"Set\" class=\"button\"></form></td>"));
  webClient.println(F("</tr></table></body></html>"));
}

void DummyPage(WiFiClient webClient)
{
  webClient.println(F("HTTP/1.1 200 OK"));
  webClient.println(F("Content-Type: text/html"));
  webClient.println(F("Connection: close"));
  webClient.println();
}

void WifiScanPage(WiFiClient webClient)
{
  IPAddress hostLocalIp(0,0,0,0);
  isWifiInStationMode ? hostLocalIp = WiFi.localIP() : hostLocalIp = WiFi.softAPIP();

  webClient.println(F("HTTP/1.1 200 OK"));
  webClient.println(F("Content-Type: text/html"));
  webClient.println(F("Cache-Control: no-Cache"));
  webClient.println(F("Connection: keep-alive"));
  webClient.println();

  webClient.println(F("<!DOCTYPE HTML>"));
  webClient.println(F("<html><head><link rel=\"shortcut icon\" href=\"#\" /><meta charset=\"utf-8\"><title>SSID list</title></head><body>"));
  // CSS begin here
  webClient.println(F("<style type=\"text/css\">"));
  webClient.println(F("body { background: #FE938C; font-family: Georgia, serif; }"));
  webClient.println(F("table { border-collapse: collapse; table-layout: fixed; width: 500px; padding: 5px; text-align: center; }"));
  webClient.println(F("table td { width: inherit; }"));
  webClient.println(F("input { font-family: inherit; }"));
  webClient.println(F("tr:nth-child(odd) { background: #EAD2AC; }"));
  webClient.println(F("tr:nth-child(even) { background: #E6B89C; }"));
  webClient.println(F("thead th, tfoot td { background: #9CAFB7; width: inherit; }"));
  webClient.println(F("</style>"));
  // CSS ends here
  webClient.println(F("<form id=\"ssid_choise\"  method=\"post\"></form>"));
  webClient.println(F("<table><thead><tr><th colspan = \"70%\">SSID</th><th colspan = \"15%\">RSSI</th><th colspan = \"15%\">&#128274;</th></thead>"));
  webClient.println(F("<tbody>"));

  uint8_t n = WiFi.scanNetworks();

  if (n == 0)
  {
    webClient.println(F("<tr><td colspan = \"100%\">No networks found</td></tr></table></body></html>"));
  }
  else
  {

    for (int i = 0; i < n; ++i)
    {
      // Print SSID and RSSI for each network found
      webClient.println(F("<tr><td colspan = \"70%\" align=\"left\">"));
      webClient.println(F("<input type=\"radio\" form=\"ssid_choise\" name=\"ssid\""));
      webClient.print(F(" value=\""));
      webClient.print(WiFi.SSID(i));
      webClient.print(F("\" required><label for=\"ssid\">"));
      webClient.print(WiFi.SSID(i));
      webClient.println(F("</label>"));
      webClient.println(F("</td><td colspan = \"15%\">"));
      webClient.print(WiFi.RSSI(i));
      webClient.println(F("</td><td colspan = \"15%\">"));
      webClient.println((WiFi.encryptionType(i) == ENC_TYPE_NONE) ? "no" : "yes");
      webClient.println(F("</td></tr>"));
    }
    webClient.println(F("<tr><td colspan=\"30%\" align=\"left\">&emsp; Password: </td><td colspan=\"55%\"><input type=\"password\" name=\"wifipass\" form=\"ssid_choise\" maxlength=\"32\" size=\"32\"></td>"));
    webClient.println(F("<td colspan = \"15%\"><input type=\"submit\" form=\"ssid_choise\" value=\"Set\" formmethod=\"post\"></td></tr>"));
    webClient.println(F("</tbody><tfoot><tr><td colspan = \"85%\"align=\"left\">&emsp; Total networks:</td><td colspan = \"15%\">"));
    webClient.print(n);
    webClient.println(F("</td></tr></table><p></p>"));
    webClient.print(F("<table><tfoot><tr><td colspan=\"50%\"><a href=\"http://"));
    webClient.print(hostLocalIp);
    webClient.print(F("/setup\">Back to Setup</a></td><td colspan = \"50%\"><a href=\"http://"));
    webClient.print(hostLocalIp);
    webClient.println(F("/wifiscan\">Refresh</a></td></tr></tfoot></table></body></html><p></p>"));
  }
}

void SetupPage(WiFiClient webClient)
{
  IPAddress hostSubnetMask(0, 0, 0, 0);
  IPAddress hostGateway(0, 0, 0, 0);
  IPAddress hostLocalIp(0, 0, 0, 0);
  if(isWifiInStationMode)
  {
    hostLocalIp = WiFi.localIP();
    hostGateway = WiFi.gatewayIP();
    hostSubnetMask = WiFi.subnetMask();
  }
  else
  {
    hostLocalIp = WiFi.softAPIP();
  }

  webClient.println(F("HTTP/1.1 200 OK"));
  webClient.println(F("Content-Type: text/html"));

  webClient.println(F("Cache-Control: no-Cache"));
  webClient.println(F("Connection: keep-alive"));
  webClient.println();

  webClient.println(F("<!DOCTYPE HTML>"));
  webClient.println(F("<html><head><link rel=\"shortcut icon\" href=\"#\" /><meta charset=\"utf-8\"><title>Setup</title></head><body>"));
  // CSS begin here

  webClient.println(F("<style type=\"text/css\">"));
  webClient.println(F("body { background: #FE938C; font-family: Georgia, serif; }"));
  webClient.println(F("table { border-collapse: collapse; table-layout: fixed; width: 300px; padding: 15px;  text-align: center; }"));
  webClient.println(F("input { font-family: inherit }"));
  webClient.println(F("tr:nth-child(odd) { background: #EAD2AC; }"));
  webClient.println(F("tr:nth-child(even) { background: #E6B89C; }"));
  webClient.println(F("thead th, tfoot td { background: #9CAFB7; }"));
  webClient.println(F("</style>"));
  // CSS ends here
  webClient.println(F("<form id=\"change_ip\" method=\"post\"></form>"));
  webClient.println(F("<table><thead><tr><th colspan = \"100%\" >Network parameters</th></thead>"));
  webClient.println(F("<tbody><tr><td colspan = \"40%\">ip address</td>"));
  webClient.print(F("<td colspan = \"15%\" ><input name=\"ip_a\" type=\"text\" form=\"change_ip\" maxlength=\"3\" size=\"3\" pattern=\"^(25[0-5]|2[0-4][0-9]|[01]?[1-9][0-9]?)$\" placeholder=\""));
  webClient.print(hostLocalIp[0]);
  webClient.print(F("\" required></td>"));
  webClient.println(F("<td colspan = \"15%\"><input name=\"ip_b\" type=\"text\" form=\"change_ip\" maxlength=\"3\" size=\"3\"  pattern=\"^(25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)$\" placeholder=\""));
  webClient.print(hostLocalIp[1]);
  webClient.print(F("\" required></td>"));
  webClient.println(F("<td colspan = \"15%\"><input name=\"ip_c\" type=\"text\" form=\"change_ip\" maxlength=\"3\" size=\"3\"  pattern=\"^(25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)$\" placeholder=\""));
  webClient.print(hostLocalIp[2]);
  webClient.print(F("\" required></td>"));
  webClient.println(F("<td colspan = \"15%\"><input name=\"ip_d\" type=\"text\" form=\"change_ip\" maxlength=\"3\" size=\"3\"  pattern=\"^(25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)$\" placeholder=\""));
  webClient.print(hostLocalIp[3]);
  webClient.print(F("\" required></td></tr>"));
  webClient.println(F("<tr><td colspan = \"40%\">subnet</td>"));
  webClient.println(F("<td colspan = \"15%\"><input name=\"mask_a\" type=\"text\" form=\"change_ip\" maxlength=\"3\" size=\"3\"  pattern=\"^(25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)$\" placeholder=\""));
  webClient.print(hostSubnetMask[0]);
  webClient.print(F("\" required></td>"));
  webClient.println(F("<td colspan = \"15%\"><input name=\"mask_b\" type=\"text\" form=\"change_ip\" maxlength=\"3\" size=\"3\"  pattern=\"^(25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)$\" placeholder=\""));
  webClient.print(hostSubnetMask[1]);
  webClient.print(F("\" required></td>"));
  webClient.println(F("<td colspan = \"15%\"><input name=\"mask_c\" type=\"text\" form=\"change_ip\" maxlength=\"3\" size=\"3\"  pattern=\"^(25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)$\" placeholder=\""));
  webClient.print(hostSubnetMask[2]);
  webClient.print(F("\" required></td>"));
  webClient.println(F("<td colspan = \"15%\"><input name=\"mask_d\" type=\"text\" form=\"change_ip\" maxlength=\"3\" size=\"3\"  pattern=\"^(25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)$\" placeholder=\""));
  webClient.print(hostSubnetMask[3]);
  webClient.print(F("\" required></td></tr>"));
  webClient.println(F("<tr><td colspan = \"40%\">gateway</td>"));
  webClient.println(F("<td colspan = \"15%\"><input name=\"gate_a\" type=\"text\" form=\"change_ip\" maxlength=\"3\" size=\"3\"  pattern=\"^(25[0-5]|2[0-4][0-9]|[01]?[1-9][0-9]?)$\" placeholder=\""));
  webClient.print(hostGateway[0]);
  webClient.print(F("\" required></td>"));
  webClient.println(F("<td colspan = \"15%\"><input name=\"gate_b\" type=\"text\" form=\"change_ip\" maxlength=\"3\" size=\"3\"  pattern=\"^(25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)$\" placeholder=\""));
  webClient.print(hostGateway[1]);
  webClient.print(F("\" required></td>"));
  webClient.println(F("<td colspan = \"15%\"><input name=\"gate_c\" type=\"text\" form=\"change_ip\" maxlength=\"3\" size=\"3\"  pattern=\"^(25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)$\" placeholder=\""));
  webClient.print(hostGateway[2]);
  webClient.print(F("\" required></td>"));
  webClient.println(F("<td colspan = \"15%\"><input name=\"gate_d\" type=\"text\" form=\"change_ip\" maxlength=\"3\" size=\"3\"  pattern=\"^(25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)$\" placeholder=\""));
  webClient.print(hostGateway[3]);
  webClient.print(F("\" required></td></tr>"));

  webClient.println(F("<tfoot><tr><td colspan = \"100%\"><input type=\"submit\" form=\"change_ip\"  value=\"Apply\"  formmethod=\"post\"></td></tr></tfoot></table><p></p>"));

  webClient.println(F("<form id=\"reset_ip\" method=\"post\"></form>"));
  webClient.println(F("<table><thead><tr><th colspan = \"100%\">WiFi setup</th></thead>"));
  webClient.println(F("<tr><td colspan = \"60%\">"));
  webClient.println(WiFi.SSID());
  webClient.println(F("</td><td colspan = \"15%\">"));
  webClient.println(WiFi.RSSI());
  webClient.println(F("</td><td colspan = \"25%\"><a href=\"http://"));
  webClient.print(hostLocalIp);
  webClient.println(F("/wifiscan\">Change</a></td></tr>"));
  webClient.println(F("<tr><td colspan = \"100%\">"));

  webClient.println(F("</td></tr>"));
  webClient.println(F("<tfoot><tr><td colspan = \"100%\"><a href=\"http://"));
  webClient.print(hostLocalIp);
  webClient.println(F("/\">Main page</a></td></tr></tfoot></table><p></p>"));
  webClient.println(F("<form id=\"reboot_device\" method=\"post\"></form>"));
  webClient.println(F("<table><thead><tr><th colspan = \"100%\">Reboot device</th></thead>"));
  webClient.println(F("<tbody><tr><td colspan=\"50%\">5+4=<input type=\"text\" name=\"reboot_sum\" form=\"reboot_device\" maxlength=\"2\" size=\"2\" pattern=\"^[9]$\" placeholder=\"9\" required></td>"));
  webClient.println(F("<td colspan = \"20%\">  </td><td colspan=\"15%\"><input type=\"submit\" form=\"reboot_device\" value=\"Reboot\" formmethod=\"post\"></td></td><td colspan = \"15%\">   </td></tr></tbody>"));
  webClient.println(F("<tfoot><tr><td colspan=\"100%\">Сhanges will take effect after reboot</td></tr></tfoot></table><p></p>"));

  webClient.println(F("</body></html>"));
}

void CheckWiFiConnect(WiFiClient webClient, char wifiSsid[], char wifiPass[])
{

  IPAddress hostLocalIp(0, 0, 0, 0);
  IPAddress hostSubnetMask(255, 255, 255, 0);
  IPAddress hostGateway(0, 0, 0, 0);
  if(isWifiInStationMode)
  {
    hostLocalIp = WiFi.localIP();
  }
  else
  {
    hostLocalIp = WiFi.softAPIP();
  }
  
  //IPAddress hostSubnetMask = WiFi.subnetMask();
  //IPAddress hostGateway = WiFi.gatewayIP();

  webClient.println(F("HTTP/1.1 200 OK"));
  webClient.println(F("Content-Type: text/html"));
  webClient.println(F("Connection: close"));
  webClient.println();
  webClient.println(F("<!DOCTYPE HTML>"));
  webClient.print(F("<html><head><link rel=\"shortcut icon\" href=\"#\"><meta http-equiv=\"Refresh\" content=\"10;url=http://"));
  webClient.print(hostLocalIp);
  webClient.print(F("/wificheckresult\"></head><body><style type=\"text/css\">body { background: #FE938C; font-family: Georgia, serif; }</style>Checking connection with "));
  webClient.print(wifiSsid);
  webClient.print(F(". You will be redirected after 10 seconds. Please stand by...</body></html>"));
  webClient.println();
  webClient.stop();

  
  WiFi.disconnect(0);
  WiFi.mode(WIFI_STA);
  WiFi.begin(wifiSsid, wifiPass);

  if (WiFi.waitForConnectResult() == WL_CONNECTED)
  {
    wifiConnRes = 0;
    // define adress of log in in EEPROM;

    for (uint8_t k = 0; k < arraySize32; k++)
    {
      EEPROM.write(eepromSsidAddress + k, wifiSsid[k]);
    }

    for (uint8_t k = 0; k < arraySize32; k++)
    {
      EEPROM.write(eepromWifiPassAddress + k, wifiPass[k]);
    }

    if (EEPROM.commit())
    {

      Serial.println("=================================");
      Serial.println("SSID was saved");
      Serial.println("=================================");
    }
    else
    {
      Serial.println("=================================");
      Serial.println("SSID was NOT saved");
      Serial.println("=================================");
    }
  }
  else if (WiFi.waitForConnectResult() == WL_NO_SSID_AVAIL)
  {
    wifiConnRes = 1;
  }
  else if (WiFi.waitForConnectResult() == WL_CONNECT_FAILED)
  {
    wifiConnRes = 2;
  }
  else if (WiFi.waitForConnectResult() == WL_IDLE_STATUS)
  {
    wifiConnRes = 3;
  }
  else if (WiFi.waitForConnectResult() == WL_DISCONNECTED)
  {
    wifiConnRes = 4;
  }
  else if (WiFi.waitForConnectResult() == -1)
  {
    wifiConnRes = 5;
  }
  Serial.println(wifiConnRes);
  Serial.println("=================================");
  Serial.println("Connect back");
  Serial.println("=================================");
  WiFi.disconnect(0);
  if (isWifiInStationMode)
  {
    WiFi.begin(ssid, WiFiPass);

    while (WiFi.status() != WL_CONNECTED)
    {
      delay(500);
      Serial.println(WiFi.status());
    }
    Serial.println("Restarting...");
    delay(100);
    ESP.restart();
  }
  else
  {
    WiFi.softAPConfig(hostLocalIp, hostSubnetMask, hostSubnetMask);
    WiFi.softAP(apSsid);
  }
}

void WifiCheckResult(WiFiClient webClient)
{

  webClient.println(F("HTTP/1.1 200 OK"));
  webClient.println(F("Content-Type: text/html"));
  webClient.println(F("Cache-Control: no-Cache"));
  webClient.println(F("Connection: close"));
  webClient.println();

  webClient.println(F("<!DOCTYPE HTML>"));
  webClient.println(F("<html><head><link rel=\"shortcut icon\" href=\"#\" /><meta charset=\"utf-8\"><title>SSID list</title></head><body>"));
  // CSS begin here
  webClient.println(F("<style type=\"text/css\">"));
  webClient.println(F("body { background: #FE938C; font-family: Georgia, serif; }"));
  webClient.println(F("</style>"));
  // CSS ends here
  Serial.print("Wifi status: ");
  Serial.print(wifiConnRes);
  Serial.print("\n");
  if (wifiConnRes == 0)
  {
    webClient.print(F("Connect to "));
    // webClient.print(wifiSsid);
    webClient.print(F(" successful. SSID and password stored. <a href=\"http://"));
    webClient.print(WiFi.localIP());
    webClient.print(F("/setup\">Check IP</a> and reboot device.</body></html>"));
  }
  else if (wifiConnRes == 1)
  {
    webClient.print(F("Connect to "));
    // webClient.print(wifiSsid);
    webClient.print(F("fail. SSID cannot be reached. <a href=\"http://"));
    webClient.print(WiFi.localIP());
    webClient.print(F("/wifiscan\">Try again.</a></body></html>"));
  }
  else if (wifiConnRes == 2)
  {
    webClient.print(F("Connect to "));
    // webClient.print(wifiSsid);
    webClient.print(F("fail. Password is incorrect. <a href=\"http://"));
    webClient.print(WiFi.localIP());
    webClient.print(F("/wifiscan\">Try again.</a></body></html>"));
  }
  else if (wifiConnRes == 3)
  {
    webClient.print(F("Connect to "));
    // webClient.print(wifiSsid);
    webClient.print(F("fail. Wi-Fi is in process of changing between statuses. <a href=\"http://"));
    webClient.print(WiFi.localIP());
    webClient.print(F("/wifiscan\">Try again.</a></body></html>"));
  }
  else if (wifiConnRes == 4)
  {
    webClient.print(F("Connect to "));
    // webClient.print(wifiSsid);
    webClient.print(F("fail. Module is not configured in station mode. <a href=\"http://"));
    webClient.print(WiFi.localIP());
    webClient.print(F("/wifiscan\">Try again.</a></body></html>"));
  }
  else if (wifiConnRes == 5)
  {
    webClient.print(F("Connect to "));
    // webClient.print(wifiSsid);
    webClient.print(F("fail. Timeout. <a href=\"http://"));
    webClient.print(WiFi.localIP());
    webClient.print(F("/wifiscan\">Try again.</a></body></html>"));
  }
  else
  {
    webClient.print(F("Something went wrong. <a href=\"http://"));
    webClient.print(WiFi.localIP());
    webClient.print(F("/wifiscan\">Try again.</a></body></html>"));
  }
}

void HandleClient(WiFiClient webClient)
{
}

void loop()
{
  unsigned long currentMillis = millis();

  if (currentMillis - previousMillis > tachoInterval)
  {
    currentRpm1 = tachoCounter1 / 2 * 60;
    currentRpm2 = tachoCounter2 / 2 * 60;
    // currentRpm1 = tachoCounter1 / 2;
    // currentRpm2 = tachoCounter2 / 2;
    Serial.print(currentRpm1);
    Serial.print("/");
    Serial.println(currentRpm2);
    tachoCounter1 = 0;
    tachoCounter2 = 0;
    previousMillis = currentMillis;
    // Устанавлваем dutyCycle. 0...255

    analogWrite(PWM_PIN, pwmValue);
  }
  yield();
  if (isDSPresent)
  {
    if (currentMillis - prevousTempMillis > tempMeasureInterval)
    {
      currentTemperature = MeasureTemperature();
      Serial.print("current temperature: ");
      Serial.println(currentTemperature);
      //температура включения 25 градусов, 10 шагов
      if (!isManualRegulation)
      {
        pwmValue = (35 - currentTemperature) * 26;
        if (pwmValue > 254)
        {
          pwmValue = 255;
        }
        else if (pwmValue < 1)
        {
          pwmValue = 0;
        }
      }
      Serial.print("PWM: ");
      Serial.println(pwmValue);
      prevousTempMillis = currentMillis;
    }
  }
  WiFiClient client = webserver.available(); // Listen for incoming clients

  if (client)
  {
    Serial.println("New Client.");

    char httpGetRequest[arraySize128] = {0};
    bool isPostRequest = false;
    uint8_t postRquestLength = 0;
    uint8_t httpReqCursor = 0;
    bool isFirstCharCr = false;
    bool isSecondCharIsLf = false;

    const uint8_t posKvpDim1Size = 24;
    char destinatopnPage[arraySize32] = {0};
    char httpPostBody[arraySize128] = {0};
    uint8_t httpPostBodyCursor = 0;

    char postKvpArray[posKvpDim1Size][arraySize32] = {{0}, {0}};

    while (client.connected())
    {
      if (client.available())
      {
        char c = client.read();

        if (c > 31)
        {
          httpGetRequest[httpReqCursor] = c;
        }
        if (httpReqCursor == 0 && c == '\r')
        {
          isFirstCharCr = true;
          Serial.print("isFirstCharCr ");
          Serial.println(isFirstCharCr);
        }

        if (httpReqCursor == 1 && c == '\n')
        {
          isSecondCharIsLf = true;
          Serial.print("isSecondCharIsLf ");
          Serial.println(isSecondCharIsLf);
        }

        httpReqCursor++;

        if (c == '\n' && !isFirstCharCr)
        {
          Serial.print("httpGetRequest: ");
          Serial.println(httpGetRequest);
          char *requestToken = strtok(httpGetRequest, " ");
          if (strcmp(requestToken, "POST") == 0)
          {
            isPostRequest = true;
            requestToken = strtok(NULL, " ");
            strcpy(destinatopnPage, requestToken);
          }
          if (strcmp(requestToken, "GET") == 0)
          {
            requestToken = strtok(NULL, " ");
            strcpy(destinatopnPage, requestToken);
          }
          if (isPostRequest)
          {
            if (strcmp(requestToken, "Content-Length:") == 0)
            {
              requestToken = strtok(NULL, " ");
              postRquestLength = atoi(requestToken);
            }
          }
          memset(httpGetRequest, 0, arraySize128);
          httpReqCursor = 0;
        }

        if (isFirstCharCr && isSecondCharIsLf && !isPostRequest)
        {
          Serial.println(destinatopnPage);
          if (strcmp(destinatopnPage, "/") == 0)
          {
            RootWebPage(client);
            return;
          }
          else if (strcmp(destinatopnPage, "/json") == 0)
          {
            JsonWebPage(client);
            return;
          }
          else if (strcmp(destinatopnPage, "/setup") == 0)
          {
            SetupPage(client);
            return;
          }
          else if (strcmp(destinatopnPage, "/wifiscan") == 0)
          {
            WifiScanPage(client);
            return;
          }
          else if (strcmp(destinatopnPage, "/wificheckresult") == 0)
          {
            WifiCheckResult(client);
            return;
          }
          else
          {
            if (isManualRegulation)
            {
              char addressToken[arraySize32] = {0};
              for (uint8_t i = 0; i < arraySize32 - 1; i++)
              {
                addressToken[i] = destinatopnPage[i + 1];
              }
              /*
              Serial.print("addressToken: ");
              Serial.print(addressToken);
              Serial.println();
              */
              uint8_t newPwmValue = atoi(addressToken);

              if (newPwmValue > 0)
              {

                pwmValue = newPwmValue;
              }
            }
            DummyPage(client);
            return;
          }
        }

        if (isPostRequest && isFirstCharCr && isSecondCharIsLf)
        {
          // POST body handle
          if (httpPostBodyCursor < postRquestLength - 1)
          {
            if (c > 31)
            {
              httpPostBody[httpPostBodyCursor] = c;
              httpPostBodyCursor++;
            }
          }
          else
          {
            httpPostBody[httpPostBodyCursor] = c;
            Serial.println("end of POST receive");
            // char postKvpArray[arraySize4][arraySize32] = {0};

            Serial.println(httpPostBody);

            char *httpPostToken = strtok(httpPostBody, "&=");

            strcpy(&postKvpArray[0][0], httpPostToken);

            for (uint8_t i = 1; i < posKvpDim1Size; i++)
            {
              if (httpPostToken != NULL)
              {

                httpPostToken = strtok(NULL, "&=");
                Serial.println(i);
                Serial.println(httpPostToken);
                if (httpPostToken != NULL)
                {
                  strcpy(&postKvpArray[i][0], httpPostToken);
                }
              }
            }

            for (uint8_t j = 0; j < posKvpDim1Size; j++)
            {
              for (uint8_t i = 0; i < arraySize32; i++)
              {
                Serial.printf("%02x", postKvpArray[j][i]);
              }
              Serial.println();
            }

            if (strcmp(postKvpArray[0], "ssid") == 0)
            {
              CheckWiFiConnect(client, &postKvpArray[1][0], &postKvpArray[3][0]);
            }
            else if (strcmp(postKvpArray[0], "pwm_value") == 0)
            {
              if (isManualRegulation)
              {
                pwmValue = atoi(postKvpArray[1]);
              }
              RootWebPage(client);
              return;
            }
            else if (strcmp(postKvpArray[0], "is_man_pwm") == 0)
            {
              isManualRegulation = !isManualRegulation;
              RootWebPage(client);
              return;
            }
            else if (strcmp(postKvpArray[0], "ip_a") == 0)
            {
              IPAddress newIp = {(uint8_t)atoi(postKvpArray[1]), (uint8_t)atoi(postKvpArray[3]), (uint8_t)atoi(postKvpArray[5]), (uint8_t)atoi(postKvpArray[7])};
              IPAddress newSubnet = {(uint8_t)atoi(postKvpArray[9]), (uint8_t)atoi(postKvpArray[11]), (uint8_t)atoi(postKvpArray[13]), (uint8_t)atoi(postKvpArray[15])};
              IPAddress newGw = {(uint8_t)atoi(postKvpArray[17]), (uint8_t)atoi(postKvpArray[19]), (uint8_t)atoi(postKvpArray[21]), (uint8_t)atoi(postKvpArray[23])};
              SaveIPToEEPROM(IPFromEEPROMIndex, newIp);
              SaveIPToEEPROM(subnetFromEEPROMIndex, newSubnet);
              SaveIPToEEPROM(gatewayFromEEPROMIndex, newGw);
              SetupPage(client);
              return;
            }
            else if (strcmp(postKvpArray[0], "reboot_sum") == 0)
            {
              Serial.println("Reboot by user");
              RootWebPage(client);
              delay(1000);
              ESP.restart();
            }
            else
            {
              RootWebPage(client);
              return;
            }

            return;
          }
        }
      }
    }
    client.stop();
    Serial.println("Client disconnected.");
    Serial.println("");
    client.flush();
  }
}
