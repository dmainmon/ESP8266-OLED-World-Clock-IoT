/*
  ESP8266-12E OLED CLOCK IoT
    by Damon Borgnino


  Uses NTP time server to get the current UTC datetime.
  Converts the UTC datetime to localized time with options for TimeZone, Daylight Savings, and 24 hour clock.
  Another option for the NTP update interval.
  Options are stored in an updateable properties file to allow saving defaults.

  The time and date are displayed on an OLED screen and HTML.
  When WIFI connected the OLED will display the ip address for the web browser.

  The HTML page accessible by the ip address shows the date and time which is updated every second
  using an AJAX call. This allows the time to update on the page without having to load the entire page
  which would take too long. This is just to show the use of AJAX to retrieve a variable. Typically you would
  not make a call every second, but this barely works due to the small amount of data being retrieved.

  The HTML page also uses a form post to update the options for TimeZone, Daylight Savings, 24 hour clock and
  the NTP update  interval.

*/

#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <WiFiUdp.h>
#include <Wire.h>
#include <FS.h> // FOR SPIFFS
#include <ctype.h> // for isNumber check
#include "Time.h"
#include "TimeLib.h"
#include "SSD1306.h"


const char ssid[] = "YourWiFiSSID";  //  your network SSID (name)
const char pass[] = "YourWiFiPassword";       // your network password
const String fName = "props.txt"; // properties file

float UTCoffset = 0;
unsigned long lastMillis = 0;
unsigned long currentMillis = 0;
unsigned long secsSince1900 = 0;
bool daylightSavings = false;
bool hourTime = false;
int interval = 30000; //
String timeStr = "";
String webMessage = "";
String dateStr = "";

unsigned int localPort = 2390;      // local port to listen for UDP packets

/* Don't hardwire the IP address or we won't get the benefits of the pool.
    Lookup the IP address for the host name instead */
//IPAddress timeServer(129, 6, 15, 28); // time.nist.gov NTP server
IPAddress timeServerIP; // time.nist.gov NTP server address
const char* ntpServerName = "time.nist.gov";

const int NTP_PACKET_SIZE = 48; // NTP time stamp is in the first 48 bytes of the message

byte packetBuffer[ NTP_PACKET_SIZE]; //buffer to hold incoming and outgoing packets

// A UDP instance to let us send and receive packets over UDP
WiFiUDP udp;

// Initialize the OLED display using Wire library
// the I2C address "0x3c" was determined using I2C scanner sketch I2C_Scanner.ino
SSD1306  display(0x3c, D3, D5);

ESP8266WebServer server(80);


///////////////////////////////////////////////////////////////////////////
// Time functions
////////////////////////////////////////////////////////////////////////////

// send an NTP request to the time server at the given address
unsigned long sendNTPpacket(IPAddress& address)
{
  Serial.println("sending NTP packet...");
  // set all bytes in the buffer to 0
  memset(packetBuffer, 0, NTP_PACKET_SIZE);
  // Initialize values needed to form NTP request
  // (see URL above for details on the packets)
  packetBuffer[0] = 0b11100011;   // LI, Version, Mode
  packetBuffer[1] = 0;     // Stratum, or type of clock
  packetBuffer[2] = 6;     // Polling Interval
  packetBuffer[3] = 0xEC;  // Peer Clock Precision
  // 8 bytes of zero for Root Delay & Root Dispersion
  packetBuffer[12]  = 49;
  packetBuffer[13]  = 0x4E;
  packetBuffer[14]  = 49;
  packetBuffer[15]  = 52;

  // all NTP fields have been given values, now
  // you can send a packet requesting a timestamp:
  udp.beginPacket(address, 123); //NTP requests are to port 123
  udp.write(packetBuffer, NTP_PACKET_SIZE);
  udp.endPacket();
}

/////////////////////////////////////////////////////////////////////////////////////


void setDateTime()
{

  unsigned long epoch;

  Serial.print(".....Updating Time.....");

  display.clear();
  display.drawString(0, 0,  timeStr);
  display.drawString(0, 19, dateStr);
  display.drawString(0, 40, "-Updating Time-");
  display.display();

  lastMillis = currentMillis;

  //get a random server from the pool
  WiFi.hostByName(ntpServerName, timeServerIP);

  sendNTPpacket(timeServerIP); // send an NTP packet to a time server
  // wait to see if a reply is available
  delay(1000);

  int cb = udp.parsePacket();
  if (!cb) {
    Serial.println("no packet yet");
  }
  else {
    Serial.print("packet received, length=");
    Serial.println(cb);
    // We've received a packet, read the data from it
    udp.read(packetBuffer, NTP_PACKET_SIZE); // read the packet into the buffer

    //the timestamp starts at byte 40 of the received packet and is four bytes,
    // or two words, long. First, esxtract the two words:

    unsigned long highWord = word(packetBuffer[40], packetBuffer[41]);
    unsigned long lowWord = word(packetBuffer[42], packetBuffer[43]);
    // combine the four bytes (two words) into a long integer
    // this is NTP time (seconds since Jan 1 1900):
    secsSince1900 = highWord << 16 | lowWord;

  }
  //Serial.print("secsSince1900: ");
  //Serial.println(secsSince1900);
  const unsigned long seventyYears = 2208988800UL;
  // subtract seventy years:
  epoch = secsSince1900 - seventyYears;

  epoch = epoch + (int)(UTCoffset * 3600);
  if (daylightSavings)
  {
    epoch = epoch + 3600;
  }

  setTime(epoch);

}

/////////////////////////////////////////
//        HTML functions
////////////////////////////////////////

String getDropDown()
{
  String webString = "";
  webString += "<select name=\"timezone\">\n";
  webString += "   <option value=\"-12\" ";
  if (UTCoffset == -12)
    webString += " selected=\"seleted\" ";
  webString += ">(GMT -12:00) Eniwetok, Kwajalein</option>\n";

  webString += "   <option value=\"-11\" ";
  if (UTCoffset == -11)
    webString += " selected=\"seleted\" ";
  webString += ">(GMT -11:00) Midway Island, Samoa</option>\n";

  webString += "   <option value=\"-10\" ";
  if (UTCoffset == -10)
    webString += " selected=\"seleted\" ";
  webString += ">(GMT -10:00) Hawaii</option>\n";

  webString += "   <option value=\"-9\" ";
  if (UTCoffset == -9)
    webString += " selected=\"seleted\" ";
  webString += ">(GMT -9:00) Alaska</option>\n";

  webString += "   <option value=\"-8\" ";
  if (UTCoffset == -8)
    webString += " selected=\"seleted\" ";
  webString += ">(GMT -8:00) Pacific Time (US &amp; Canada)</option>\n";

  webString += "   <option value=\"-7\" ";
  if (UTCoffset == -7)
    webString += " selected=\"seleted\" ";
  webString += ">(GMT -7:00) Mountain Time (US &amp; Canada)</option>\n";

  webString += "   <option value=\"-6\" ";
  if (UTCoffset == -6)
    webString += " selected=\"seleted\" ";
  webString += ">(GMT -6:00) Central Time (US &amp; Canada), Mexico City</option>\n";

  webString += "   <option value=\"-5\" ";
  if (UTCoffset == -5)
    webString += " selected=\"seleted\" ";
  webString += ">(GMT -5:00) Eastern Time (US &amp; Canada), Bogota, Lima</option>\n";

  webString += "   <option value=\"-4.5\" ";
  if (UTCoffset == -4.5)
    webString += " selected=\"seleted\" ";
  webString += ">(GMT -4:30) Caracas</option>\n";

  webString += "   <option value=\"-4\" ";
  if (UTCoffset == -4)
    webString += " selected=\"seleted\" ";
  webString += ">(GMT -4:00) Atlantic Time (Canada), La Paz, Santiago</option>\n";

  webString += "   <option value=\"-3.5\" ";
  if (UTCoffset == -3.5)
    webString += " selected=\"seleted\" ";
  webString += ">(GMT -3:30) Newfoundland</option>\n";

  webString += "   <option value=\"-3\" ";
  if (UTCoffset == -3)
    webString += " selected=\"seleted\" ";
  webString += ">(GMT -3:00) Brazil, Buenos Aires, Georgetown</option>\n";

  webString += "   <option value=\"-2\" ";
  if (UTCoffset == -2)
    webString += " selected=\"seleted\" ";
  webString += ">(GMT -2:00) Mid-Atlantic</option>\n";

  webString += "   <option value=\"-1\" ";
  if (UTCoffset == -1)
    webString += " selected=\"seleted\" ";
  webString += ">(GMT -1:00 hour) Azores, Cape Verde Islands</option>\n";

  webString += "   <option value=\"0\" ";
  if (UTCoffset == 0)
    webString += " selected=\"seleted\" ";
  webString += ">(GMT) Western Europe Time, London, Lisbon, Casablanca, Greenwich</option>\n";

  webString += "   <option value=\"1\" ";
  if (UTCoffset == 1)
    webString += " selected=\"seleted\" ";
  webString += ">(GMT +1:00 hour) Brussels, Copenhagen, Madrid, Paris</option>\n";

  webString += "   <option value=\"2\" ";
  if (UTCoffset == 2)
    webString += " selected=\"seleted\" ";
  webString += ">(GMT +2:00) Kaliningrad, South Africa, Cairo</option>\n";

  webString += "   <option value=\"3\" ";
  if (UTCoffset == 3)
    webString += " selected=\"seleted\" ";
  webString += ">(GMT +3:00) Baghdad, Riyadh, Moscow, St. Petersburg</option>\n";

  webString += "   <option value=\"3.5\" ";
  if (UTCoffset == 3.5)
    webString += " selected=\"seleted\" ";
  webString += ">(GMT +3:30) Tehran</option>\n";

  webString += "   <option value=\"4\" ";
  if (UTCoffset == 4)
    webString += " selected=\"seleted\" ";
  webString += ">(GMT +4:00) Abu Dhabi, Muscat, Yerevan, Baku, Tbilisi</option>\n";

  webString += "   <option value=\"4.5\" ";
  if (UTCoffset == 4.5)
    webString += " selected=\"seleted\" ";
  webString += ">(GMT +4:30) Kabul</option>\n";

  webString += "   <option value=\"5\" ";
  if (UTCoffset == 5)
    webString += " selected=\"seleted\" ";
  webString += ">(GMT +5:00) Ekaterinburg, Islamabad, Karachi, Tashkent</option>\n";

  webString += "   <option value=\"5.5\" ";
  if (UTCoffset == 5.5)
    webString += " selected=\"seleted\" ";
  webString += ">(GMT +5:30) Mumbai, Kolkata, Chennai, New Delhi</option>\n";

  webString += "   <option value=\"5.75\" ";
  if (UTCoffset == 5.75)
    webString += " selected=\"seleted\" ";
  webString += ">(GMT +5:45) Kathmandu</option>\n";

  webString += "   <option value=\"6\" ";
  if (UTCoffset == 6)
    webString += " selected=\"seleted\" ";
  webString += ">(GMT +6:00) Almaty, Dhaka, Colombo</option>\n";

  webString += "   <option value=\"6.5\" ";
  if (UTCoffset == 6.5)
    webString += " selected=\"seleted\" ";
  webString += ">(GMT +6:30) Yangon, Cocos Islands</option>\n";

  webString += "   <option value=\"7\" ";
  if (UTCoffset == 7)
    webString += " selected=\"seleted\" ";
  webString += ">(GMT +7:00) Bangkok, Hanoi, Jakarta</option>\n";

  webString += "   <option value=\"8\" ";
  if (UTCoffset == 8)
    webString += " selected=\"seleted\" ";
  webString += ">(GMT +8:00) Beijing, Perth, Singapore, Hong Kong</option>\n";

  webString += "   <option value=\"9\" ";
  if (UTCoffset == 9)
    webString += " selected=\"seleted\" ";
  webString += ">(GMT +9:00) Tokyo, Seoul, Osaka, Sapporo, Yakutsk</option>\n";

  webString += "   <option value=\"9.5\" ";
  if (UTCoffset == 9.5)
    webString += " selected=\"seleted\" ";
  webString += ">(GMT +9:30) Adelaide, Darwin</option>\n";

  webString += "   <option value=\"10\" ";
  if (UTCoffset == 10)
    webString += " selected=\"seleted\" ";
  webString += ">(GMT +10:00) Eastern Australia, Guam, Vladivostok</option>\n";

  webString += "   <option value=\"11\" ";
  if (UTCoffset == 11)
    webString += " selected=\"seleted\" ";
  webString += ">(GMT +11:00) Magadan, Solomon Islands, New Caledonia</option>\n";

  webString += "   <option value=\"12\" ";
  if (UTCoffset == 12)
    webString += " selected=\"seleted\" ";
  webString += ">(GMT +12:00) Auckland, Wellington, Fiji, Kamchatka</option>\n";
  webString += "  </select>\n";

  return webString;
}

////////////////////////////////////////////////////////////////////

String getAJAXcode()
{

  String webStr = "";
  webStr += "<script src=\"http://ajax.googleapis.com/ajax/libs/jquery/1.10.2/jquery.min.js\"></script> \n";
  webStr += "<script>\n";

  webStr += " function loadTime() { \n";
  webStr += " $(\"#timeDiv\").load(\"http://" + WiFi.localIP().toString() + "/time\"); \n";
  webStr += "} \n\n";

  webStr += " setInterval(loadTime, 1000); \n"; // every x milli seconds
  webStr += " loadTime(); \n";// on load

  webStr += " </script> \n";
  return webStr;
}

///////////////////////////////////////////////////////////////////////////////////////

String setHTML()
{
  String webString = "<html><head>\n";
  //  webString += "<meta http-equiv=\"refresh\" content=\"30;url=http://" + WiFi.localIP().toString() + "\"> \n";
  webString += getAJAXcode();
  webString += "</head><body>\n";
  webString += "<form action=\"http://" + WiFi.localIP().toString() + "/submit\" method=\"POST\">";
  webString += "<h1>ESP8266-12E OLED Clock IoT </h1>\n";
  webString += "<div style=\"color:red\">" + webMessage + "</div>\n";

  webString += "<div id=\"timeDiv\" style=\"color:blue; font-size:24px; font-family:'Comic Sans MS'\">" + dateStr + " &nbsp; &nbsp; &nbsp; " + timeStr + "</div>\n";

  webString += "<br><br>" + getDropDown();

  webString += "<br><table style=\"width:400px;\"><tr>";
  webString += "<td style=\"text-align:right\">";
  webString += "<div>Time Server Update Interval: </div>\n";
  webString += "<div>Daylight Savings: </div>\n";
  webString += "<div>24 hour: </div>\n";
  webString += "</td>";
  webString += "<td>";
  webString += " <input type='text' value='" + String(interval / 1000) + "' name='interval' maxlength='10' size='4'> (secs)<br>\n";

  if (daylightSavings)
    webString += " <input type='checkbox' checked name='daySave' value='" +  String(daylightSavings) + "'/>";
  else
    webString += " <input type='checkbox' name='daySave' value='" +  String(daylightSavings) + "'  />";


  if (hourTime)
    webString += "<br><input type='checkbox' checked name='24hour' value='" +  String(hourTime) + "'/>";
  else
    webString += "<br><input type='checkbox' name='24hour' value='" +  String(hourTime) + "'  />";

  webString += " <input type='submit' value='Submit' >\n";

  webString += "</td></tr></table>\n";

  webString += "<div><a href=\"/\">Refresh</a></div> \n";
  webString += "</form></body></html>\n";
  return webString;
}


/////////////////////////////////////////////////////////////
//   File functions
/////////////////////////////////////////////////////////////

void updateProperties()
{
  File f = SPIFFS.open(fName, "w");
  if (!f) {

    Serial.println("file open for properties failed");
  }
  else
  {
    Serial.println("====== Updating to properties file =========");
    display.clear();
    display.drawString(0, 0, " Updating");
    display.drawString(0, 19, "properties");
    display.drawString(0, 40, "file...");
    display.display();

    f.print(UTCoffset); f.print( ","); f.print(daylightSavings);
    f.print("~"); f.print(interval);
    f.print(":"); f.println(hourTime);

    Serial.println("Properties file updated");

    f.close();
  }
}

/////////////////////////////////////////

void initPropFile()
{

  SPIFFS.begin();
  delay(10);
  /////////////////////////////////////////////////////////
  // SPIFFS.format(); // uncomment to completely clear data
  // return;
  ///////////////////////////////////////////////////////////
  File f = SPIFFS.open(fName, "r");

  if (!f) {

    Serial.println("Please wait 30 secs for SPIFFS to be formatted");

    display.clear();
    display.drawString(0, 0,  "Formatting...");
    display.drawString(0, 19, "Please wait 30");
    display.drawString(0, 40, "seconds.");
    display.display();

    SPIFFS.format();

    Serial.println("Spiffs formatted");

    updateProperties();

  }
  else
  {
    Serial.println("Properties file exists. Reading.");

    while (f.available()) {

      //Lets read line by line from the file
      String str = f.readStringUntil('\n');

      String offsetStr = str.substring(0, str.indexOf(",")  );
      String dSavStr = str.substring(str.indexOf(",") + 1, str.indexOf("~") );
      String intervalStr = str.substring(str.indexOf("~") + 1, str.indexOf(":") );
      String hourStr = str.substring(str.indexOf(":") + 1 );

      UTCoffset = offsetStr.toFloat();
      daylightSavings = dSavStr.toInt();
      interval = intervalStr.toInt();
      hourTime = hourStr.toInt();

    }

    f.close();
  }

}

//////////////////////////////////////////////////////////
// used to error check the text box input
/////////////////////////////////////////////////////////

boolean isValidNumber(String str) {
  for (byte i = 0; i < str.length(); i++)
  {
    if (isDigit(str.charAt(i))) return true;
  }
  return false;
}

//////////////////////////////////////////////////////////////////////////////////////
/// client handlers
//////////////////////////////////////////////////////////////////////////////////////

void handle_submit() {

  webMessage = "";
  daylightSavings = false;
  hourTime = false;

  if (server.args() > 0 ) {
    for ( uint8_t i = 0; i < server.args(); i++ ) {

      // can be useful to determine the values from a form post.
      //  webMessage += "<br>Server arg " + server.arg(i);
      //  webMessage += "<br>Server argName " + server.argName(i);

      if (server.argName(i) == "daySave") {
        // checkbox is checked
        daylightSavings = true;
      }

      if (server.argName(i) == "24hour") {
        // checkbox is checked
        hourTime = true;
      }

      if (server.argName(i) == "interval") {

        if (isValidNumber(server.arg(i)) ) // error checking to make sure we have a number
        {
          interval = server.arg(i).toInt() * 1000;
        }
        else
        {
          webMessage = "Interval must be a valid number";
        }

      }

      if (server.argName(i) == "timezone") {

        UTCoffset = server.arg(i).toFloat();
      }

    }
  }

  if (webMessage == "")
  {
    updateProperties();
    webMessage = "Settings Updated";
  }

  setDateTime();

  String webString = setHTML();
  server.send(200, "text/html", webString);            // send to someones browser when asked

}


//////////////////////////////////////////////////////////////////////////////////////

void handle_time() // this function handles the AJAX call
{
  server.send(200, "text/html", dateStr + " &nbsp; &nbsp; &nbsp; " + timeStr);
}

////////////////////////////////////////////////////////////////////////////////////////

void handle_root() {

  webMessage = "";
  String webString = setHTML();
  server.send(200, "text/html", webString);            // send to someones browser when asked

}


////////////////////////////////////////////////////////////////////////////////////////

void setup() {

  Serial.begin(115200);
  Serial.println("Starting...");

  display.init();

  display.flipScreenVertically();
  display.setFont(ArialMT_Plain_16);
  display.setTextAlignment(TEXT_ALIGN_LEFT);


  // We start by connecting to a WiFi network
  Serial.print("Connecting to ");
  Serial.println(ssid);
  WiFi.begin(ssid, pass);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("");

  // create and/or read properties file
  initPropFile();

  Serial.println("WiFi connected");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());

  Serial.println("Starting UDP");
  udp.begin(localPort);
  Serial.print("Local port: ");
  Serial.println(udp.localPort());

  server.on("/", handle_root);

  server.on("/submit", handle_submit);

  server.on("/time", handle_time); // Used for AJAX call

  server.begin();

  delay(100);

  setDateTime();

  if (secsSince1900 == 0) // try again, NTP server may not have responded in time
    setDateTime();

}


//////////////////////////////////////////////////

void loop() {

  currentMillis = millis();

  String AmPm = "";

  // How much time has passed, accounting for rollover with subtraction!
  if ((unsigned long)(currentMillis - lastMillis) >= interval )
  {
    setDateTime();
  }

  // setup the time and dateString

  int thour = hour();

  if (!hourTime) // if not 24 hour time
  {
    AmPm = "AM";
    if (thour > 12)
    {
      thour = thour - 12;
      AmPm = "PM";
    }
    else
    {
      AmPm = "AM";
      if (thour == 12)
        AmPm = "PM";
    }
    if (thour == 0)
      thour = 12;
  }
  timeStr = thour;

  Serial.print("The time is ");
  Serial.print(hour());
  Serial.print(':');
  timeStr += ":";

  if ( minute() < 10 ) {
    // In the first 10 minutes of each hour, we'll want a leading '0'
    Serial.print('0');
    timeStr += "0";
  }

  Serial.print(minute());

  timeStr += minute();
  Serial.print(':');
  timeStr += ":";

  if ( second() < 10 ) {
    // In the first 10 seconds of each minute, we'll want a leading '0'
    Serial.print('0');
    timeStr += "0";
  }

  Serial.println(second());

  timeStr += second();
  timeStr += " " + AmPm;

  dateStr = month();
  dateStr += "." ;
  dateStr += day();
  dateStr += ".";
  dateStr += year();

  Serial.println(dateStr);

  display.clear();
  display.drawString(0, 0,  timeStr);
  display.drawString(0, 19, dateStr);
  display.drawString(0, 40, WiFi.localIP().toString());
  display.display();

  server.handleClient();

  delay(1000); // delay one second before OLED display update


}

