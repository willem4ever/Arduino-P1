#include <Time.h>
#include <SPI.h>
#include <EEPROM.h>
#include <Dhcp.h>
#include <Dns.h>
#include <Ethernet.h>
#include <EthernetClient.h>
#include <EthernetServer.h>
#include <EthernetUdp.h>
#include <util.h>
#include <PubSubClient.h>

struct obis {
  byte a;
  byte b;
  byte c;
  byte d;
  byte e;
  byte f;
};

byte mac[6] = {
  0x90, 0xA2, 0xDA, 0x0d, 0x5b, 0xec };  
IPAddress ip(192,168,223,24);
IPAddress gateway(192,168,223, 5);
IPAddress subnet(255, 255, 255, 128);

unsigned int localPort = 123;      // local port to listen for UDP packets

IPAddress dhcpNtp(192,168,223,9);  // default, will take ip from dhcp server

byte broker[] = {
  192,168,223,7};

const int NTP_PACKET_SIZE= 48; // NTP time stamp is in the first 48 bytes of the message
byte packetBuffer[ NTP_PACKET_SIZE]; //buffer to hold incoming and outgoing packets 
// An EthernetUDP instance to let us send and receive packets over UDP
EthernetUDP Ntp;
PubSubClient client(broker, 1883, callback);

unsigned long getNtpTime();
byte GetObis (char *p, struct obis *po);

time_t pminute = 0;             // once a minute
time_t psecond = 0;

const  long timeZoneOffset = 0L; // set this to the offset in seconds to your local time;
//const int  pir = 7;

//int pirstatus = 0;
//int lightstatus = 0;

char topic[64];
char Value[10][12];
char buffer[64];
byte bIndex;
long datagrams;

const char *message[] = {
  "powerusage1","powerusage2","powerdelivery1","powerdelivery2","powerrate","powerusagec","powerdeliveryc","gasusage"}; 

byte GetObis (char *p, struct obis *po) {
  // return index to delimeter hit
  byte i,a;
  po->a = po->b =  po->c =  po->d =  po->e  = po->f = 0xff; // clear obis struct
  i = a = 0;
  while (isdigit(p[i]) || p[i] == '-' || p[i] == ':' || p[i] == '.' || p[i] == '*') { // Escape on any non valid OBIS character
    if (isdigit(p[i])) {
      if (a) {
        a = a*10+p[i] - 0x30;
      }
      else
        a = p[i] - 0x30;
    }
    if (p[i] == '-') {
      po->a = a;
      a=0;
    }
    else if (p[i] == ':') {
      po->b = a;
      a=0;
    }
    else if (p[i] == '.' && po->c == 0xff) {
      po->c = a;
      a=0;
    }
    else if (p[i] == '.' && po->d == 0xff) {
      po->d = a;
      a=0;
    }
    else if (p[i] == '*' ) {
      po->e = a;
      a=0;
    }
    i++;
  }

  // We bumped into a non valid character, determine the last obis field (C & D are mandatory)
  if (po->d == 0xff) { 		// D
    po->d = a;
  }
  else if (po->e == 0xff) {	// E
    po->e = a;
  }
  else if (po->f == 0xff) { 	// F
    po->f = a;
  }
  return i;
}

byte CompareObis (obis *po, byte a,byte b,byte c,byte d, byte e,byte f) {
  return (po->a == a && po->b == b && po->c == c && po->d == d && po->e == e && po->f == f);
}

// Extracts string from a single line and return index to the next value (when available) or '0' on EOL
byte GetString (char *s,char *d) {
  byte i;
  i=0;
  while (s[i] != ')' && s[i] != 0) {
    if (s[i] != '(') {
      *d = s[i];
      d++;
    }
    *d=0;
    i++; // Advance to next
  }
  //
  return ( s[i] == ')' && s[i+1] == '(' ? i+1 : 0 ); // 0 or index to next value
}

// Extracts float from a single line and return index to the next value (when available) or '0' on EOL
byte GetFloat (char *s,char *d) {
  byte i;
  i=0;
  while (s[i] != ')' && s[i] != 0) {
    if (s[i] != '(') {
      if (s[i] == '*') 
        *d=0;			// Terminate on unit, but keep for reference
      else
        *d = s[i];
      d++;
    }
    *d=0;
    i++; // Advance to next
  }
  //
  return ( s[i] == ')' && s[i+1] == '(' ? i+1 : 0 ); // 0 or index to next value
}

void callback(char* topic, byte* payload,unsigned int length) {
  ;// handle message arrived
}

void publish(char *xtopic,char *payload) { 
  if(xtopic[0] == '/')
    sprintf (topic,"%s/%x%x%x",xtopic,mac[3],mac[4],mac[5]);
  else
    sprintf (topic,"/sensor/%x%x%x/%s",mac[3],mac[4],mac[5],xtopic);
  client.publish(topic, payload);

}

void setup() 
{
  int i;

  pinMode(2,INPUT);
  pinMode(A2,OUTPUT);
  digitalWrite(A2,1);
  
  Serial.begin(9600);
  UCSR0C = (UCSR0C & B11000001 ) | B00000100 | B00100000;     // 7 bit even parity

  //for (i=0;i<6;i++)
  //  EEPROM.write(i, mac[i]);

  for (i=0;i<6;i++)
    mac[i]=EEPROM.read(i);

  Serial.println(dhcpNtp);
  // start Ethernet and UDP
  if (Ethernet.begin(mac) == 0) {

    Serial.println("Failed to configure Ethernet using DHCP");
    Ethernet.begin(mac,ip,subnet);
  }

  if (Ethernet.ntpServerIP()) {
    dhcpNtp = Ethernet.ntpServerIP();
  }

  Serial.println(dhcpNtp);

  // Serial.print("My IP address: ");

  ip = Ethernet.localIP();

  Ntp.begin(localPort);

  setSyncProvider(getNtpTime);

  while(timeStatus()== timeNotSet)   
    ; // wait until the time is set by the sync provider

  i = client.connect("smarty-1");
  ltoa (now(),Value[0],10);
  publish("hello",Value[0]);
  bIndex = 0;
  datagrams = 0;
}

void loop()
{


  unsigned long t = now();
  int a;
  struct obis po;
  
  digitalWrite(A2,!digitalRead(2));
  
  // Check for serial input 
  if (Serial.available() > 0) {
    // read the incoming byte:
    a = Serial.read();
    if (a != 0x020 || a != '\r')
      buffer[bIndex] = a;
    if (a == '\n') {
      buffer[bIndex]=0;
      if (buffer[0] == '/') { // MeterID 
        ;
      } 
      else if (buffer[0] == '!') { // End of Message 
        // Publish our findings
        //long x = micros();
        int i = 0;

        for (i=0;i<8;i++) {
          publish((char*)message[i],Value[i]);
        }
        datagrams++;
        //x = micros() - x;
        //sprintf(buffer,"%ld us",x);
        //publish("Timer/Elapsed",buffer);

      }
      else if (bIndex > 2) { //ignore empty lines

        int i = GetObis(buffer, &po);
        // 1-0:1.8.1(00180.726*kWh) powerusage1
        if (CompareObis(&po,1,0,1,8,1,255)) {
          i = GetFloat(&buffer[i],Value[0]);
          //publish("Powerusage1",Value);
        }
        //1-0:1.8.2(00001.416*kWh) powerusage2
        else if (CompareObis(&po,1,0,1,8,2,255)) {
          i = GetFloat(&buffer[i],Value[1]);
          //publish("Powerusage2",Value);
        }
        // 1-0:2.8.1(00000.000*kWh)
        else if (CompareObis(&po,1,0,2,8,1,255)) {
          i = GetFloat(&buffer[i],Value[2]);
          //publish("Powerdelivery1",Value);
        }
        // 1-0:2.8.2(00000.000*kWh)
        else if (CompareObis(&po,1,0,2,8,2,255)) {
          i = GetFloat(&buffer[i],Value[3]);
          //publish("Powerdelivery2",Value);
        }
        // 0-0:96.14.0(0001)
        else if (CompareObis(&po,0,0,96,14,0,255)) {
          i = GetString(&buffer[i],Value[4]);
          //publish("PowerTariff",Value);
        }
        // 1-0:1.7.0(0000.42*kW)
        else if (CompareObis(&po,1,0,1,7,0,255)) {
          i = GetFloat(&buffer[i],Value[5]);
          //publish("Powerusage",Value);
        }
        // 1-0:2.7.0(0000.00*kW)
        else if (CompareObis(&po,1,0,2,7,0,255)) {
          i = GetFloat(&buffer[i],Value[6]);
          //publish("Powerdelivery",Value);
        }
        // Invalid Obis returns with po.d = 0 ...
        else if (CompareObis(&po,255,255,255,0,255,255)) {
          i = GetFloat(&buffer[i],Value[7]); 
          //publish("Gasusage",Value);
        }
      }
      bIndex=0;

    }
    else {
      if (bIndex < 64) bIndex++;  
    }
  } 

  if( t % 60 == 0  && t != pminute) { //update the display only if the time has changed
    pminute = t;
    psecond = t;
    ltoa (t,Value[0],10);
    publish("timestamp", Value[0]);
    ltoa (datagrams,Value[0],10);
    publish("datagrams", Value[0]);
    Ethernet.maintain();
  }

  client.loop();
}


unsigned long getNtpTime () 
{
  int iBytes;

  sendNTPpacket(dhcpNtp); // send an NTP packet to a time server
  // Serial.println("#1");

  // wait for a reply / timeout after 10 seconds
  iBytes = 0;
  while (Ntp.parsePacket() != 48 && iBytes < 2000) {
    iBytes++;
    delay(5);
  }
  // Serial.println("#2");

  if (Ntp.available() == 48)  {

    Serial.print("time synched in ");
    Serial.print(iBytes*5);
    Serial.println(" milliseconds");

    // Ntp.read(packetBuffer,8);                // dump header
    Ntp.read(packetBuffer,NTP_PACKET_SIZE);  // read the packet into the buffer

    // The timestamp starts at byte 40 of the received packet and is four bytes,
    // or two words, long. First, esxtract the two words:

    unsigned long highWord = word(packetBuffer[40], packetBuffer[41]);
    unsigned long lowWord = word(packetBuffer[42], packetBuffer[43]);  
    // combine the four bytes (two words) into a long integer
    // this is NTP time (seconds since Jan 1 1900):
    unsigned long secsSince1900 = highWord << 16 | lowWord;
    const unsigned long seventyYears = 2208988800UL - timeZoneOffset;  
    unsigned long epoch = secsSince1900 - seventyYears;
    return epoch; 
  }
  else {
    //Serial.print (Ntp.available());
    //Serial.println(" - No NTP packet received");
    return 0;
  }

}

// send an NTP request to the time server at the given address 
void sendNTPpacket(IPAddress ntpServer)
{
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
  Ntp.beginPacket(ntpServer, 123);		   
  Ntp.write( packetBuffer,NTP_PACKET_SIZE); 
  Ntp.endPacket();

}





















