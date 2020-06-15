#include <SPI.h>
#include <Ethernet.h>
#include <SD.h>
#include "DHT.h"

#define DHTTYPE DHT22
#define BUFFER_LEN 75
#define DHTPIN 7
#define SENSOR_PIN A0
#define FILENAME "dlink.csv"
#define VPP 0.0048828125
#define SENSITIVITY 0.066

//MARK: Global data variables
unsigned long packets_in, packets_out;
unsigned long last_packets_in = 0, last_packets_out = 0;
unsigned long users_last_update = 0, stats_last_update = 0, dht_last_update = 0, save_last_update = 0;
unsigned int users_update_interval = 6000, stats_update_interval = 4000, dht_update_interval = 1000, save_update_interval = 10000;
unsigned long sum, sum_total;
short users;
float temperature, humidity, current = 0.0;

//MARK: Global objects
EthernetClient web_client;
DHT dht(DHTPIN, DHTTYPE);
File storageFile;

//MARK: HTTP
short i, success; //success: -1 unauth, 1: okay, 0: error
char buffer[BUFFER_LEN], c;
short retries, state = 0;//state:0: initial, 1: found row, 2: got received

//parsers (callbacks)
bool parse_users(char* buffer) {
  char *start = strstr(buffer, "NUMBER of WIRELESS CLIENTS : ");
  if(start){
    start += 28;
    char* ending = strstr(start, "</h2>");
    if(!ending) return false;
    ending[0] = '\0';
    users = atoi(start);
    return true;
  }
  return false;
}

bool parse_wireless(char* buffer) {
  if(state == 0 && strstr(buffer, "WIRELESS 11n")) state = 1;
  else if(state > 0) {
    char* start = strstr(buffer, ">");
    if(!start) return false;
    start++;
    char* ending = strstr(start, " Packets");
    if(!ending) return false;
    ending[0] = '\0';
    if(state == 1) {
      packets_in = atol(start);
      state = 2;
    } else if(state == 2) {
      packets_out = atol(start);
      state = 0;
      return true;
    }
  }
  return false;
}

bool parse_login(char* buffer) {
  if(strstr(buffer, "url=index.php")) return true;
  return false;
}

String ipToString(IPAddress address)
{
 return String(address[0]) + "." + 
        String(address[1]) + "." + 
        String(address[2]) + "." + 
        String(address[3]);
}

int http_request(int command) {
  
  if(!web_client.connect(Ethernet.gatewayIP(), 80)) return 0;
  if(!web_client.connected()) return 0;
  
  //send request
  if(command == 1) web_client.println(F("GET /st_wlan.php HTTP/1.1"));
  else if(command == 2) web_client.println(F("GET /st_stats.php HTTP/1.1"));
  else if(command == 3) {
    web_client.println(F("POST /login.php HTTP/1.1"));
    web_client.println(F("Content-Type: application/x-www-form-urlencoded"));
    web_client.println(F("Content-Length: 48"));
  }
  web_client.println(F("Connection: keep-alive"));
  web_client.println("Host: " + ipToString(Ethernet.localIP()));
  web_client.println();

  if(command == 3) {
    web_client.println(F("ACTION_POST=LOGIN&LOGIN_USER=admin&LOGIN_PASSWD="));
    web_client.println();
  }

  retries = 0;
  while(!web_client.available() && retries++ < 10) delay(100);

  //receive response
  i = 0;
  success = 0;
  while(web_client.connected() && web_client.available()) {
    c = web_client.read();
    if(c == '\r' || c == ',' || c =='"') continue;
    if(c == '\n') {
      buffer[i % BUFFER_LEN] = '\0';
      i = 0;
      if(command != 3) {
        if(strstr(buffer, "<h1>Login</h1>")) {
          success = -1;
          break;
        }
        if(command == 1) {
          if(parse_users(buffer) == true) {
            success = 1;
            break;
          }
        } else if(command == 2) {
          if(parse_wireless(buffer) == true) {
            success = 1;
            break;
          }
        } 
      } else {
        if(parse_login(buffer) == true) {
          success = 1;
          break;
        }
      }
      delay(3);
    } else { buffer[ i++ % BUFFER_LEN ] = c;  }
  }
  web_client.stop();
  return success;
}

void update_users() {
  if(http_request(1) == -1) {
     retries = 3;
     while(http_request(3) == 0 && retries-- > 0) delay(500);
     delay(500);
     http_request(1);
  }
}

void update_stats() {
  if(http_request(2) == -1) {
     retries = 3;
     while(http_request(3) == 0 && retries-- > 0) delay(500);
     delay(500);
     http_request(2);
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
  if(sum_total == 0) return current;
  float voltage = (sum*1.0/sum_total) * VPP;
  voltage -= 2.25;
  return voltage / SENSITIVITY;
}

//MARK: SD
void store()
{
  storageFile = SD.open(FILENAME, FILE_WRITE);
  if(!storageFile) return;
  if(last_packets_in > packets_in) last_packets_in = 0;
  if(last_packets_out > packets_out) last_packets_out = 0;
  unsigned int pck_in = last_packets_in > 0 ? packets_in - last_packets_in : 0;
  unsigned int pck_out = last_packets_out > 0 ? packets_out - last_packets_out : 0;
  String out =  String(millis()) + ","  + String(pck_in) + "," + String(pck_out) + "," + 
                String(users) + "," + 
                String(temperature) + "," + 
                String(humidity) + "," + 
                String(current);
  storageFile.println(out.c_str());
  storageFile.close();
  last_packets_in = packets_in;
  last_packets_out = packets_out;
}

void setup_ethernet() {
  byte mac[] = { 0x00, 0xAA, 0xBB, 0xCC, 0xDE, 0x02 };
  while(Ethernet.begin(mac) == 0);
}

void init_storage()
{ 
  while(!SD.begin(4));
  if(!SD.exists(FILENAME)) {
    storageFile = SD.open(FILENAME, FILE_WRITE);
    if(storageFile) {
      storageFile.println(F("ts,pkts_in,pkts_out,user,tmp,hum,curr"));
      storageFile.close();
    }
  }
}

//MARK: Setup
void setup() {
  //Serial.begin(115200);
  delay(30000);
  init_storage();
  setup_ethernet();
  dht.begin();
}

//MARK: Main
void loop() {
  unsigned long ms = millis();
  if(ms - users_last_update > users_update_interval) {
    update_users();
    users_last_update = millis();
  } 
  if(ms - stats_last_update > stats_update_interval) {
    update_stats();
    stats_last_update = millis();
  }
  if(millis() - dht_last_update > dht_update_interval) {
    update_dht_data();
    dht_last_update = millis();
  }
  if(millis() - save_last_update > save_update_interval) {
    current = update_sensor_value();
    sum = 0;
    sum_total = 0;
    store();
    save_last_update = millis();
  }

  sum += analogRead(SENSOR_PIN);
  sum_total++;
  delay(100);
}


//MARK: Errors
/*
1: error opening storage file.
2: error opening storage file while saving.
3: bad response code.
4: request failed after retries.
5: ethernet start failed.
 */
