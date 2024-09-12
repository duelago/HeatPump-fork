#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <ESP8266WebServer.h>
#include <DNSServer.h>
#include <HeatPump.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <EEPROM.h>
#include <ElegantOTA.h>

// NTP Client setup
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "se.pool.ntp.org", 3600, 60000); // Timezone offset of 3600 seconds (UTC+1), update every 60 seconds

// HTML content
const char* html = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
<meta name='viewport' content='width=device-width, initial-scale=1'/>
<style>
  body {
    font-family: Arial, sans-serif;
    text-align: center;
    margin: 0;
    padding: 0;
  }
  h3 {
    background-color: #4CAF50;
    color: white;
    padding: 10px 0;
    margin: 0 0 20px 0;
  }
  table {
    margin: 0 auto;
    padding: 10px;
    border-collapse: collapse;
  }
  td {
    padding: 8px 12px;
  }
  input[type='submit'] {
    padding: 10px 20px;
    background-color: #4CAF50;
    color: white;
    border: none;
    cursor: pointer;
  }
  input[type='submit']:hover {
    background-color: #45a049;
  }
  a {
    display: block;
    margin-top: 20px;
    color: #4CAF50;
    text-decoration: none;
  }
  a:hover {
    text-decoration: underline;
  }
</style>
<script>
  function updateTimerState(checkbox) {
    var xhr = new XMLHttpRequest();
    xhr.open("POST", "/", true);
    xhr.setRequestHeader('Content-Type', 'application/x-www-form-urlencoded');
    xhr.send("DAILY_TIMER=" + (checkbox.checked ? "1" : "0"));
  }
</script>
</head>
<body>
  <h3>Heat Pump</h3>
  <p>TEMP: _ROOMTEMP_&deg;C</p>
  <form autocomplete='off' method='post'>
    <table>
      <tr><td>Power:</td><td>_POWER_</td></tr>
      <tr><td>Mode:</td><td>_MODE_</td></tr>
      <tr><td>Temp:</td><td>_TEMP_</td></tr>
      <tr><td>Fan:</td><td>_FAN_</td></tr>
      <tr><td>Vane:</td><td>_VANE_</td></tr>
      <tr><td>WideVane:</td><td>_WVANE_</td></tr>
      <tr><td>Daily Timer:</td><td>_TIMER_STATUS_</td></tr>
    </table>
    <br/>
    <input type='submit' value='Change Settings'/>
  </form>
  <p></p>
  <form method='post'>
    <input type='checkbox' name='DAILY_TIMER' value='1' _TIMER_STATE_> Enable Daily Timer<br><br>
    <input type='submit' value='Save Settings'/>
  </form>
  <p><a href='/update'>Update Firmware</a></p>
</body>
</html>
)rawliteral";

ESP8266WebServer server(80);
HeatPump hp;
bool dailyTimerEnabled = false;

void setup() {
  Serial.begin(115200);

  EEPROM.begin(512);  // Allocate 512 bytes for the EEPROM
  dailyTimerEnabled = EEPROM.read(0) == 1; // Read the timer state from EEPROM

  hp.connect(&Serial);
  hp.setSettings({
    "ON",  /* ON/OFF */
    "FAN", /* HEAT/COOL/FAN/DRY/AUTO */
    20,    /* Between 16 and 31 */
    "4",   /* Fan speed: 1-4, AUTO, or QUIET */
    "SWING",   /* Air direction (vertical): 1-5, SWING, or AUTO */
    "SWING"    /* Air direction (horizontal): <<, <, |, >, >>, <>, or SWING */
  });

  // Hardcoded WiFi credentials
  const char* ssid = "SSID";
  const char* password = "PASSWORD";
  
  WiFi.begin(ssid, password);
  
  Serial.print("Connecting to ");
  Serial.print(ssid);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("Connected to WiFi!");

  timeClient.begin();

  server.on("/", handle_root);
  server.on("/generate_204", handle_root); // Captive portal
  server.onNotFound(handleNotFound);

  ElegantOTA.begin(&server);
  server.begin();
}

void loop() {
  server.handleClient();
  timeClient.update();
  hp.sync();

  if (dailyTimerEnabled) {
    checkTimer();
  }
}

void checkTimer() {
  String currentTime = timeClient.getFormattedTime();
  int currentHour = timeClient.getHours();
  int currentMinute = timeClient.getMinutes();

  if (currentHour == 11 && currentMinute == 0) {
    Serial.println("Rebooting ESP8266...");
    ESP.restart();  // Reboot the ESP8266
  }

  if (currentHour == 22 && currentMinute == 0) {
    hp.setPowerSetting("ON");
    hp.setModeSetting("HEAT");
    hp.setTemperature(20);
    hp.setFanSpeed("4");
    hp.setVaneSetting("SWING");
    hp.setWideVaneSetting("SWING");
    hp.update();
    Serial.println("Heat pump turned ON at 22:00");
  }

  if (currentHour == 5 && currentMinute == 59) {
    hp.setPowerSetting("OFF");
    hp.update();
    Serial.println("Heat pump turned OFF at 05:59");
  }
}

String encodeString(String toEncode) {
  toEncode.replace("<", "&lt;");
  toEncode.replace(">", "&gt;");
  toEncode.replace("|", "&vert;");
  return toEncode;
}

String createOptionSelector(String name, const String values[], int len, String value) {
  String str = "<select name='" + name + "'>\n";
  for (int i = 0; i < len; i++) {
    String encoded = encodeString(values[i]);
    str += "<option value='";
    str += values[i];
    str += "'";
    str += values[i] == value ? " selected" : "";
    str += ">";
    str += encoded;
    str += "</option>\n";
  }
  str += "</select>\n";
  return str;
}

void handleNotFound() {
  server.send(200, "text/plain", "URI Not Found");
}

void handle_root() {
  // Debugging: Print the dailyTimerEnabled state
  Serial.print("dailyTimerEnabled: ");
  Serial.println(dailyTimerEnabled ? "true" : "false");

  // Update the checkbox state before reading the value
  String toSend = html;
  toSend.replace("_TIMER_STATE_", dailyTimerEnabled ? "checked" : "");
  toSend.replace("_TIMER_STATUS_", dailyTimerEnabled ? "ON" : "OFF");

  // Replace other placeholders in the HTML
  int rate = change_states() ? 0 : 60;
  toSend.replace("_RATE_", String(rate));
  String power[2] = {"OFF", "ON"};
  toSend.replace("_POWER_", createOptionSelector("POWER", power, 2, hp.getPowerSetting()));
  String mode[5] = {"HEAT", "DRY", "COOL", "FAN", "AUTO"};
  toSend.replace("_MODE_", createOptionSelector("MODE", mode, 5, hp.getModeSetting()));
  String temp[16] = {"31", "30", "29", "28", "27", "26", "25", "24", "23", "22", "21", "20", "19", "18", "17", "16"};
  toSend.replace("_TEMP_", createOptionSelector("TEMP", temp, 16, String(hp.getTemperature()).substring(0, 2)));
  String fan[6] = {"AUTO", "QUIET", "1", "2", "3", "4"};
  toSend.replace("_FAN_", createOptionSelector("FAN", fan, 6, hp.getFanSpeed()));
  String vane[7] = {"AUTO", "1", "2", "3", "4", "5", "SWING"};
  toSend.replace("_VANE_", createOptionSelector("VANE", vane, 7, hp.getVaneSetting()));
  String widevane[7] = {"<<", "<", "|", ">", ">>", "<>", "SWING"};
  toSend.replace("_WVANE_", createOptionSelector("WIDEVANE", widevane, 7, hp.getWideVaneSetting()));
  toSend.replace("_ROOMTEMP_", String(hp.getRoomTemperature()));

  server.send(200, "text/html", toSend);
}

bool change_states() {
  bool updated = false;

  // Handle other settings (power, mode, temperature, fan, vane, widevane)
  if (server.hasArg("POWER")) {
    hp.setPowerSetting(server.arg("POWER").c_str());
    updated = true;
  }
  if (server.hasArg("MODE")) {
    hp.setModeSetting(server.arg("MODE").c_str());
    updated = true;
  }
  if (server.hasArg("TEMP")) {
    hp.setTemperature(server.arg("TEMP").toFloat());
    updated = true;
  }
  if (server.hasArg("FAN")) {
    hp.setFanSpeed(server.arg("FAN").c_str());
    updated = true;
  }
  if (server.hasArg("VANE")) {
    hp.setVaneSetting(server.arg("VANE").c_str());
    updated = true;
  }
  if (server.hasArg("WIDEVANE")) {
    hp.setWideVaneSetting(server.arg("WIDEVANE").c_str());
    updated = true;
  }

  // Handle DAILY_TIMER checkbox
  if (server.hasArg("DAILY_TIMER")) {
    dailyTimerEnabled = true;
    EEPROM.write(0, 1);  // Write '1' to EEPROM if timer is enabled
  } else {
    dailyTimerEnabled = false;
    EEPROM.write(0, 0);  // Write '0' to EEPROM if timer is disabled
  }

  // Commit changes to EEPROM
  EEPROM.commit();

  // Update the heat pump settings if necessary
  if (updated) {
    hp.update();
  }

  return updated;
}
