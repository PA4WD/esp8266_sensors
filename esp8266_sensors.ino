#include <ESP8266WiFi.h>
#include <Ticker.h>
#include "DHT.h"
#include <Wire.h>
#include <Adafruit_BMP085.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_TSL2561_U.h>
#include <InfluxDb.h>
#include "credentials.h"

#define DHTPIN D1    // what digital pin we're connected to = D1
#define DHTTYPE DHT22   // DHT 22  (AM2302), AM2321

//#define DEBUG

DHT dht(DHTPIN, DHTTYPE);
Ticker pushTimer;
#define INTERVALTIME 300 //300sec between updates

//BMP085 SCl = D5
//BMP085 SDA = D6
Adafruit_BMP085 bmp;
Adafruit_TSL2561_Unified tsl = Adafruit_TSL2561_Unified(TSL2561_ADDR_FLOAT, 12345);

int timerFlag = 0;
const char ssid[] = WIFI_SSID;
const char password[] = WIFI_PASSWD;
String chipid;
Influxdb influx(INFLUXDB_HOST);

// connect to wifi network
void connectWifi() {
  // attempt to connect to Wifi network:
  WiFi.mode(WIFI_STA);
  Serial.print("Connecting to ");
  Serial.println(ssid);

  // Connect to WEP/WPA/WPA2 network:
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
    digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN));
  }

  Serial.println("");
  Serial.println("WiFi connected");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());
  digitalWrite(LED_BUILTIN, HIGH);
}

void configureSensor(void)
{
  /* You can also manually set the gain or enable auto-gain support */
  //tsl.setGain(TSL2561_GAIN_1X);      /* No gain ... use in bright light to avoid sensor saturation */
  //tsl.setGain(TSL2561_GAIN_16X);     /* 16x gain ... use in low light to boost sensitivity */
  tsl.enableAutoRange(true);            /* Auto-gain ... switches automatically between 1x and 16x */

  /* Changing the integration time gives you better sensor resolution (402ms = 16-bit data) */
  //tsl.setIntegrationTime(TSL2561_INTEGRATIONTIME_13MS);      /* fast but low resolution */
  //tsl.setIntegrationTime(TSL2561_INTEGRATIONTIME_101MS);  /* medium resolution and speed   */
  tsl.setIntegrationTime(TSL2561_INTEGRATIONTIME_402MS);  /* 16-bit data but slowest conversions */

  /* Update these values depending on what you've set above! */
  Serial.println("------------------------------------");
  Serial.print  ("Gain:         "); Serial.println("Auto");
  Serial.print  ("Timing:       "); Serial.println("402 ms");
  Serial.println("------------------------------------");
}


int influxDbUpdate() {
  //*********** Temp hum bar ******************************
  float humidity = dht.readHumidity();
  float pressure = bmp.readPressure() / 100.0;
  float bmpTemperature = bmp.readTemperature();
  float temperature = dht.readTemperature();

  if (isnan(humidity) || isnan(temperature) || isnan(pressure) || isnan(bmpTemperature)) {
    return -1;
  }

  InfluxData bmptempRow("temperature_BMP085");
  bmptempRow.addTag("device", chipid);
  bmptempRow.addValue("value", bmpTemperature);
  influx.prepare(bmptempRow);

  InfluxData tempRow("temperature");
  tempRow.addTag("device", chipid);
  tempRow.addValue("value", temperature);
  influx.prepare(tempRow);

  InfluxData humRow("humidity");
  humRow.addTag("device", chipid);
  humRow.addValue("value", humidity);
  influx.prepare(humRow);

  InfluxData pressRow("pressure");
  pressRow.addTag("device", chipid);
  pressRow.addValue("value", pressure);
  influx.prepare(pressRow);

  //*********** Light sensor ******************************
  /* Get a new sensor event */
  sensors_event_t event;
  tsl.getEvent(&event);

  InfluxData luxRow("light");
  luxRow.addTag("device", chipid);
  if (event.light) {
#ifdef DEBUG
    Serial.print(event.light);
    Serial.println(" lux");
#endif
    luxRow.addValue("value", event.light);
  }
  else {
#ifdef DEBUG
    Serial.println("Sensor overload");
#endif
    luxRow.addValue("value", 0);
  }
  influx.prepare(luxRow);

  bool ret = influx.write();
  if(ret == false){
    return -1;
  }
  return 0;
}

// ********* timer tick callback ******************
void pushTimerTick()
{
  timerFlag = 1;
}

//************ Start van het programma *************
void setup() {
  Serial.begin(115200);
  pinMode(LED_BUILTIN, OUTPUT);
  Serial.println();
  connectWifi();
  Serial.println();

  dht.begin();
  float t = dht.readTemperature();
  Serial.print("Temperature: ");
  Serial.print(t);
  Serial.println(" *C ");
  Serial.println();

  Wire.begin(D6, D5);

  while (!bmp.begin()) {
    Serial.println("Could not find a valid BMP085 sensor, check wiring!");
    delay(2000);
  }
  Serial.println("BMP085 sensor, OK");

  /* Initialise the sensor */
  if (!tsl.begin())
  {
    /* There was a problem detecting the TSL2561 ... check your connections */
    Serial.print("Ooops, no TSL2561 detected ... Check your wiring or I2C ADDR!");
    while (1);
  }
  /* Setup the sensor gain and integration time */
  configureSensor();

  chipid = String(ESP.getChipId()).c_str();
  Serial.print("chipid = ");
  Serial.println(chipid);

  influx.setDb(INFLUXDB_DATABASE);
  //influx.setDbAuth(INFLUXDB_DATABASE, INFLUXDB_USER, INFLUXDB_PASS);

  //Set de domoticz update timer
  pushTimer.attach(INTERVALTIME, pushTimerTick);
  timerFlag = 1; // stuur meteen de eerste update
}

void loop() {
  if (WiFi.status() == WL_CONNECTED) {
    if (timerFlag == 1) {
      digitalWrite(LED_BUILTIN, LOW);
      if ((influxDbUpdate() != 0)) {
        //error in server request
        delay(1000); //try after 1 sec
      }
      else {
        timerFlag = 0;
        digitalWrite(LED_BUILTIN, HIGH);
      }
    }
  }
  else {
    connectWifi();
  }
}
