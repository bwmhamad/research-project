#include <SPI.h>
#include <Ethernet.h>
#include <SD.h>
#include "DHT.h"

#define DHTTYPE DHT22
#define BUFFER_LEN 200
#define DHTPIN 7
#define SENSOR_PIN A0
#define FILENAME "shiro.csv"
#define VPP 0.0048828125
#define SENSITIVITY 0.066

//MARK: Global variables
//data
unsigned long packets_in, packets_out, users, kbytes;
float temperature, humidity, current;

//others
unsigned long l_packets_in, l_packets_out, l_packets, l_bytes;
bool shouldGetWireless = true;
short wirelessPageNum = 0;

//MARK: Global objects
//enum LogType { DEBUG, INFO, WARNING, ERR, CRITICAL, NONE };
EthernetClient web_client;
DHT dht(DHTPIN, DHTTYPE);
File storageFile;


//MARK: Logger
void print_log(int error) {
  if(Serial) Serial.println(error);
}


//MARK: HTTP
int get_response_code(char* buffer) {
  int code;
  if(sscanf(buffer, "%*8s %d %*s", &code) == 1) return code;
  return -1;
}

//setup
void setup_ethernet() {
  byte mac[] = { 0x00, 0xAA, 0xBB, 0xCC, 0xDE, 0x02 };
  while(Ethernet.begin(mac) == 0) print_log(5);
}

//parsers (callbacks)
void parse_users(char* buffer) {
  int success = sscanf(buffer, "%u %*s %*s %lu %lu", &users, &l_packets, &l_bytes);
  if(success != 3) return;
  kbytes += l_bytes/1024;
}

void parse_wireless(char* buffer) {
  int success = 0;
  success = sscanf(buffer, "%lu %lu %u %u %u", &l_packets_in, &l_packets_out);
  if(success == 5) {
    users = l_packets_in;
    if(l_packets_out < wirelessPageNum){
      shouldGetWireless = false;
      return;
    }
  } else {
    success = sscanf(buffer, "%*s %*d %lu %lu", &l_packets_in, &l_packets_out);
    if(success == 2) {
      packets_in  += l_packets_in;
      packets_out += l_packets_out;
    }
  }
}

void http_get(String server_path, void (*callback)(char* buffer)) {
  short i = 0;
  char buffer[BUFFER_LEN], c;
  short retries = 0;
  int response_code = 0;
  
  while(!web_client.connect(Ethernet.gatewayIP(), 80)) {
    if(retries++>4) {
      print_log(4);
      return;
    }
    delay(1000);
  }
  
  //send request
  web_client.println("GET " + server_path + " HTTP/1.1");
  web_client.println(F("Host: 192.168.1.100"));
  web_client.println(F("Authorization: Basic YWRtaW46YWRtaW4="));
  web_client.println();
  
  //wait for response
  retries = 0;
  while(!web_client.available() && retries++ < 10) delay(10);

  //receive response
  while(web_client.available()) {
    c = web_client.read();
    if(c == '\r' || c == ',' || c =='"') continue;
    if(c == '\n') {
      buffer[i % BUFFER_LEN] = '\0';
      i = 0;
      if(response_code == 0) {
        response_code = get_response_code(buffer);
        if(response_code != 200) {
          print_log(3);
          break;
        }
      } else callback(buffer);
    } else { buffer[ i++ % BUFFER_LEN ] = c;  }
  }
  web_client.stop();
  delay(200);
}

void get_http_data() {
  
  packets_in=0;
  packets_out=0;
  users=0;
  kbytes=0;
  shouldGetWireless = true;
  wirelessPageNum = 0;
  http_get(F("/userRpm/SystemStatisticRpm.htm"), &parse_users);
  while(shouldGetWireless) {
    wirelessPageNum++;
    http_get("/userRpm/WlanStationRpm.htm?Page=" + String(wirelessPageNum), &parse_wireless);
  }
}

//MARK: DHT
void update_dht_data() {
  float h = dht.readHumidity();
  float t = dht.readTemperature();

  if (!isnan(h) && !isnan(t)) {
    temperature = t;
    humidity = h;
  }
}

//MARK: ACS712
float update_sensor_value()
{
  
  long sum = 0;
  int i;
  for(i=0;i<1000;i++) sum += analogRead(SENSOR_PIN);
  float voltage = (sum*1.0/i) * VPP;
  voltage -= 2.5;
  return voltage / SENSITIVITY;
}

//MARK: SD
void store()
{
  storageFile = SD.open(FILENAME, FILE_WRITE);
  if(!storageFile) {
    print_log(2);
    return;
  }
  String out =  String(millis()) + "," + String(kbytes) + "," + String(packets_in) + "," + String(packets_out) + "," + 
                String(users) + "," + 
                String(temperature) + "," + 
                String(humidity) + "," + 
                String(current);
  storageFile.println(out.c_str());
  storageFile.close();
}

void init_storage()
{ 
  if (!SD.begin(4)) return;
 
  if(!SD.exists(FILENAME))
  {
    storageFile = SD.open(FILENAME, FILE_WRITE);
    if (storageFile) 
    {
      storageFile.println(F("ts,kbytes,pkts_in,pkts_out,user,tmp,hum,curr"));
      storageFile.close();
    } else print_log(1);
  }
}

//MARK: Setup
void setup() {
  Serial.begin(115200);
  init_storage();
  setup_ethernet();
  dht.begin();
}

//MARK: Main
void loop() {
  get_http_data();
  update_dht_data();
  current = update_sensor_value();
  //Serial.println(String(temperature) + "-" + String(humidity) + "-" + String(current));
  //Serial.println(String(packets_in) + "-" + String(packets_out) + "-" + String(users) + "-" + String(kbytes));
  store();
  delay(5000);
}


//MARK: Errors
/*
1: error opening storage file.
2: error opening storage file while saving.
3: bad response code.
4: request failed after retries.
5: ethernet start failed.
 */
