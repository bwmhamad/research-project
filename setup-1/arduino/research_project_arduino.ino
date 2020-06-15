#include <SoftwareSerial.h>
#include <DHT.h>
#include <SD.h>

#define AMP_SENSOR A0
#define DHT_SENSOR 7
#define DHTTYPE DHT22
#define SERIAL_BAUD 115200
#define NODEMCU_BAUD 57600
#define STORE_LED 10
#define FILENAME "data.csv"
#define VPP 0.0048828125
#define SENSITIVITY 0.066
#define AMP_SHIFT 2.5

//dht
DHT dht(DHT_SENSOR, DHTTYPE);
float humidity;
float temperature;

//current
float current = 0;

//communication
SoftwareSerial NodeSerial(6, 5); // RX, TX
float voltage;
long bytes_in;
long bytes_out;
long packets_in;
long packets_out;
int users;

//storage
File storageFile;
int store_interval = 10000; //store every 10sec
int measure_interval = 1000;
long last_measure = 0;
long last_store = 0;
bool sd_did_start = false;
bool is_led_on = false;
int led_on_interval = 1000;

void setup()
{
  Serial.begin(SERIAL_BAUD);
  delay(1000);
  Serial.println("Data processor started. Lebanese University - Mhamad Saad.");
  pinMode(AMP_SENSOR,INPUT);
  NodeSerial.begin(NODEMCU_BAUD);
  dht.begin();
  Serial.println("DHT sensor started.");
  init_storage();
  pinMode(STORE_LED, OUTPUT);
  Serial.println("Setup complete. Starting reader...");
}

void loop()
{
   //get measurements
   if(millis() >= last_measure + measure_interval)
   {
      read_dht();
      read_current();
      last_measure = millis();
   }
   String router_data = read_node();
   //forward router logs to serial is no data was parsed
   if(!parse_router_data(router_data) && router_data.length() > 0) Serial.println(router_data);
   if(millis() >= last_store + store_interval)
   {
     store();
     last_store = millis();
   }

   handle_serial_commands();

   //turn led off
   if(is_led_on && millis() > last_store + led_on_interval)
   {
      is_led_on = false;
      digitalWrite(STORE_LED, LOW);
   }
}

void handle_serial_commands()
{
  if(!Serial.available()) return;
  char c,command = 0;
  while(Serial.available())
  {
    c = Serial.read();
    if( c >= 'a' && c <= 'z') command = c;
  }
  switch(command)
  {
    case 'd': display_storage(); break;
    case 'r': reset_storage(); break;
    case 't': Serial.println("Temparature:" + String(temperature)); break;
    case 'h': Serial.println("Humidity:" + String(humidity)); break;
    case 'a': Serial.println("Amperage:" + String(current)); break;
  }
}

void init_storage()
{ 
  if (!sd_did_start && !SD.begin(4)) 
  {
    Serial.println("Storage initialization failed! Please restart reader.");
    while (1);
  }

  sd_did_start = true;
 
  if(!SD.exists(FILENAME))
  {
    Serial.println("Creating storage file...");
    storageFile = SD.open(FILENAME, FILE_WRITE);
    if (storageFile) 
    {
      storageFile.println("timestamp,voltage,bytes_in,bytes_out,packets_in,packets_out,users,temperature,humidity,current");
      storageFile.close();
    }
    else Serial.println("error opening storage file.");
  } else Serial.println("Storage file already exists.");
}

void store()
{
  storageFile = SD.open(FILENAME, FILE_WRITE);

  String out =  String(millis()) + "," + 
                String(voltage) + "," + 
                String(bytes_in) + "," + 
                String(bytes_out) + "," + 
                String(packets_in) + "," + 
                String(packets_out) + "," + 
                String(users) + "," + 
                String(temperature) + "," + 
                String(humidity) + "," + 
                String(current);
  storageFile.println(out.c_str());
  storageFile.close();
  digitalWrite(STORE_LED, HIGH);
  is_led_on=true;
}

void reset_storage()
{
  if(!SD.remove(FILENAME)) Serial.println("Error deleting file.");
  init_storage();
}

void display_storage()
{
  storageFile = SD.open(FILENAME);
  if (storageFile) {
    while (storageFile.available()) {
      Serial.write(storageFile.read());
    }
    storageFile.close();
  } else {
    Serial.println("error opening storage file.");
  }
}

void read_dht()
{
  float hum = dht.readHumidity();
  float temp= dht.readTemperature();
  if(!isnan(hum)) humidity = hum;
  if(!isnan(temp)) temperature = temp;
}

void read_current()
{
  current = calculate_current(sample_sensor_value());
}

String read_node()
{
  String s = "";
  int i=0;
  while(NodeSerial.available() && i<100)
  {
    s += String(char(NodeSerial.read()));
    i++;
  }
  return s;
}

bool parse_router_data(String input)
{

  /*
  long ltimestamp;
  float lvoltage;
  long lbytes_in;
  long lbytes_out;
  long lpackets_in;
  long lpackets_out;
  int lusers;
  */
  
  if(input == "") return false;
  int volts;
  int matched = sscanf(input.c_str(), "%*ld,%d,%ld,%ld,%ld,%ld,%d", &volts, &bytes_in, &bytes_out, &packets_in, &packets_out, &users);
  if(matched != 6) return false;

  voltage = (volts/1000) + (volts%1000)*1.0/1000;
  /*
  bool correct_ts = ltimestamp > timestamp;
  if(lbytes_in    < bytes_in)       return false;
  if(lbytes_out   < bytes_out)      return false;
  if(lpackets_in  < packets_in)     return false;
  if(lpackets_out < packets_out)    return false;
  if(lvoltage < 1 || lvoltage > 6)  return false;
  if(lusers   < 0 || lusers   > 30) return false;
  */
  /*
  timestamp = ltimestamp;
  bytes_in = lbytes_in;
  bytes_out = lbytes_out;
  packets_in = lpackets_in;
  packets_out = lpackets_out;
  voltage = lvoltage;
  users = lusers;
  */
  return true;
}

float sample_sensor_value()
{
  long sum = 0;
  int i;
  for(i=0;i<1000;i++) sum += analogRead(AMP_SENSOR);
  return sum*1.0/i;
}

float calculate_current(float val)
{
  float voltage = val * VPP;
  voltage -= AMP_SHIFT;
  return voltage / SENSITIVITY;
}
