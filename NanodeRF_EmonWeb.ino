/*
                         __        __   _     
  ___ _ __ ___   ___  _ _\ \      / /__| |__  
 / _ \ '_ ` _ \ / _ \| '_ \ \ /\ / / _ \ '_ \ 
|  __/ | | | | | (_) | | | \ V  V /  __/ |_) |
 \___|_| |_| |_|\___/|_| |_|\_/\_/ \___|_.__/ 
                                              
                             _      
 _ __   __ _ _ __   ___   __| | ___ 
| '_ \ / _` | '_ \ / _ \ / _` |/ _ \
| | | | (_| | | | | (_) | (_| |  __/
|_| |_|\__,_|_| |_|\___/ \__,_|\___|
                                   
*/
//--------------------------------------------------------------------------------------
// Relay's data recieved by emontx up to emonWeb.
// Decodes reply from server to set software real time clock
// Relay's time data to emonglcd - and any other listening nodes.
// Looks for 'ok' reply from http request to verify data reached emonWeb

// emonBase Documentation: http://openenergymonitor.org/emon/emonbase

// Authors: Trystan Lea and Glyn Hudson
// Adjusted for emonWeb: Frank Oxener
// Part of the: openenergymonitor.org project
// Licenced under GNU GPL V3
//http://openenergymonitor.org/emon/license

// EtherCard Library by Jean-Claude Wippler and Andrew Lindsay
// JeeLib Library by Jean-Claude Wippler
//--------------------------------------------------------------------------------------

#define DEBUG     //comment out to disable serial printing to increase long term stability 
#define UNO       //anti crash wachdog reset only works with Uno (optiboot) bootloader, comment out the line if using delianuova

#include <Wire.h>
#include <RTClib.h>
RTC_Millis RTC;

#include <JeeLib.h>	         //https://github.com/jcw/jeelib
#include <avr/wdt.h>

#define MYNODE 15
#define freq RF12_433MHZ     // frequency
#define group 210            // network group 

//---------------------------------------------------
// Data structures for transfering data between units
//---------------------------------------------------
typedef struct { int powerA, powerB, powerC, temp; } PayloadTX;
PayloadTX emontx;    

typedef struct { int temperature; } PayloadGLCD;
PayloadGLCD emonglcd;

typedef struct { int hour, mins, sec; } PayloadBase;
PayloadBase emonbase;
//---------------------------------------------------

//---------------------------------------------------------------------
// The PacketBuffer class is used to generate the json string that is send via ethernet - JeeLabs
//---------------------------------------------------------------------
class PacketBuffer : public Print {
public:
    PacketBuffer () : fill (0) {}
    const char* buffer() { return buf; }
    byte length() { return fill; }
    void reset()
    { 
      memset(buf,NULL,sizeof(buf));
      fill = 0; 
    }
    virtual size_t write (uint8_t ch)
        { if (fill < sizeof buf) buf[fill++] = ch; }
    byte fill;
    char buf[150];
    private:
};
PacketBuffer str;

//--------------------------------------------------------------------------
// Ethernet
//--------------------------------------------------------------------------
#include <EtherCard.h>		//https://github.com/jcw/ethercard 

// ethernet interface mac address, must be unique on the LAN
byte mymac[6] = { 0x00,0x04,0xA3,0x21,0xC8,0x46};

//IP address of remote sever, only needed when posting to a server that has not got a dns domain name (staticIP e.g local server) 
byte Ethernet::buffer[700];
static uint32_t timer;

//Domain name of remote webserver - leave blank if posting to IP address 
char website[] PROGMEM = "emonweb.org";
//static byte hisip[] = { 192, 168, 2, 12};    // un-comment for posting to static IP server (no domain name) 

const int redLED = 6;                     // NanodeRF RED indicator LED
const int greenLED = 5;                   // NanodeRF GREEN indicator LED

int ethernet_error = 0;                   // Etherent (controller/DHCP) error flag
int rf_error = 0;                         // RF error flag - high when no data received 
int ethernet_requests = 0;                // count ethernet requests without reply

int dhcp_status = 0;
int dns_status = 0;

int emonglcd_rx = 0;                      // Used to indicate that emonglcd data is available
int data_ready=0;                         // Used to signal that emontx data is ready to be sent
unsigned long last_rf;                    // Used to check for regular emontx data - otherwise error

char line_buf[50];                        // Used to store line of http reply header

//**********************************************************************************************************************
// SETUP
//**********************************************************************************************************************
void setup () {

  //Nanode RF LED indictor  setup - green flashing means good - red on for a long time means bad! 
  //High means off since NanodeRF tri-state buffer inverts signal 
  pinMode(redLED, OUTPUT); digitalWrite(redLED,LOW);
  pinMode(greenLED, OUTPUT); digitalWrite(greenLED,LOW); 
  delay(100); digitalWrite(redLED,HIGH);                      // turn off redLED

  Serial.begin(9600);
  Serial.println("\n[webClient]");

  if (ether.begin(sizeof Ethernet::buffer, mymac) == 0) {
    Serial.println( "Failed to access Ethernet controller");
    ethernet_error = 1;  
  }

  dhcp_status = 0;
  dns_status = 0;
  ethernet_requests = 0;
  ethernet_error=0;
  rf_error=0;

  rf12_initialize(MYNODE, freq,group);
  last_rf = millis()-40000;                                       // setting lastRF back 40s is useful as it forces the ethernet code to run straight away

  digitalWrite(greenLED,HIGH);                                    // Green LED off - indicate that setup has finished 

  #ifdef UNO
  wdt_enable(WDTO_8S);
  #endif;
}

//**********************************************************************************************************************
// LOOP
//**********************************************************************************************************************
void loop () {

  #ifdef UNO
  wdt_reset();
  #endif

  dhcp_dns();   // handle dhcp and dns setup - see dhcp_dns tab

  // Display error states on status LED
  if (ethernet_error==1 || rf_error==1 || ethernet_requests > 0) digitalWrite(redLED,LOW);
    else digitalWrite(redLED,HIGH);

  //-----------------------------------------------------------------------------------------------------------------
  // 1) On RF recieve
  //-----------------------------------------------------------------------------------------------------------------
  if (rf12_recvDone()){
      if (rf12_crc == 0 && (rf12_hdr & RF12_HDR_CTL) == 0)
      {
        int node_id = (rf12_hdr & 0x1F);

        if (node_id == 10)                                               // EMONTX
        {
          emontx = *(PayloadTX*) rf12_data;                              // get emontx data
          Serial.println();                                              // print emontx data to serial
          Serial.print("1 emontx: "); 
          Serial.print(emontx.powerA);
          Serial.print(' ');
          Serial.print(emontx.temp);
          Serial.print(" | time: ");
          Serial.println(millis()-last_rf);
          last_rf = millis();                                            // reset lastRF timer
          
          delay(50);                                                     // make sure serial printing finished
          str.reset();                                                   // Reset string
          str.print("auth_token=qqbUzX2V8UKUDwxZyBhR");

          str.print("&rf_fail=0");                                       // RF recieved so no failure
          str.print("&powerA=");      str.print(emontx.powerA);          // Add power reading 
          str.print("&powerB=");      str.print(emontx.powerB);
          str.print("&powerC=");      str.print(emontx.powerC);
          str.print("&temp=");        str.print(emontx.temp);

          data_ready = 1;                                                // data is ready
          rf_error = 0;
        }

        if (node_id == 20)                                               // EMONGLCD 
        {
          emonglcd = *(PayloadGLCD*) rf12_data;                          // get emonglcd data
          Serial.print("5 emonglcd: ");                                  // print output
          Serial.println(emonglcd.temperature);
          emonglcd_rx = 1;
        }
      }
    }

  //-----------------------------------------------------------------------------------------------------------------
  // 2) If no data is recieved from rf12 module the server is updated every 30s with RFfail = 1 indicator for debugging
  //-----------------------------------------------------------------------------------------------------------------
  if ((millis()-last_rf)>30000)
  {
    last_rf = millis();                                                 // reset lastRF timer
    str.reset();                                                        // reset string
    str.print("auth_token=qqbUzX2V8UKUDwxZyBhR");
    str.print("&rf_fail=1");                                            // No RF received in 30 seconds so send failure 
    data_ready = 1;                                                     // Ok, data is ready
    rf_error=1;
  }

  //-----------------------------------------------------------------------------------------------------------------
  // 3) Send data via ethernet
  //-----------------------------------------------------------------------------------------------------------------
  ether.packetLoop(ether.packetReceive());

  if (data_ready) {

    // include temperature data from emonglcd if it has been recieved
    if (emonglcd_rx) {
      str.print(",temperature:");  
      str.print(emonglcd.temperature/100.0);
      emonglcd_rx = 0;
    }

    str.print("\0");                              // End of string

    Serial.print("2 "); Serial.println(str.buf);  // print to serial string

    ethernet_requests ++;
    ether.httpPost(PSTR("/api"), website,  PSTR("User-Agent: NanodeRF EmonWeb"), str.buf, my_callback);
    data_ready =0;
  }

  if (ethernet_requests > 10) delay(10000); // Reset the nanode if more than 10 request attempts have been tried without a reply

}
//**********************************************************************************************************************

//-----------------------------------------------------------------------------------
// Ethernet callback
// recieve reply and decode
//-----------------------------------------------------------------------------------
static void my_callback (byte status, word off, word len) {

  get_header_line(2,off);      // Get the date and time from the header
  Serial.print("3 ok | ");     // Print out the date and time
  Serial.println(line_buf);    // Print out the date and time

  // Decode date time string to get integers for hour, min, sec, day
  // We just search for the characters and hope they are in the right place
  char val[1];
  val[0] = line_buf[23]; val[1] = line_buf[24];
  int hour = atoi(val);
  val[0] = line_buf[26]; val[1] = line_buf[27];
  int mins = atoi(val);
  val[0] = line_buf[29]; val[1] = line_buf[30];
  int sec = atoi(val);
  val[0] = line_buf[11]; val[1] = line_buf[12];
  int day = atoi(val);

  RTC.adjust(DateTime(2012, 1, day, hour, mins, sec));

  DateTime now = RTC.now();
  emonbase.hour = now.hour();
  emonbase.mins = now.minute();
  emonbase.sec = now.second();

  delay(100);

  // Send time data
  int i = 0; while (!rf12_canSend() && i<10) {rf12_recvDone(); i++;}    // if can send - exit if it gets stuck, as it seems too
  rf12_sendStart(0, &emonbase, sizeof emonbase);                        // send payload
  rf12_sendWait(0);

  Serial.println("4 emonbase sent");

  get_reply_data(off);
  if (strcmp(line_buf,"ok")) {ethernet_requests = 0; ethernet_error = 0;}  // check for ok reply from emoncms to verify data post request
}
