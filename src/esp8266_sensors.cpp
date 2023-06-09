#include <ESP8266WiFi.h>
#include <Ticker.h>
#include "DHT.h"
//#include <Wire.h>
#include <Adafruit_BMP085.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_TSL2561_U.h>
#include <InfluxDb.h>  //https://github.com/tobiasschuerg/InfluxDB-Client-for-Arduino
#include "credentials.h"

#define DHTPIN D1      // what digital pin we're connected to = D1
#define DHTTYPE DHT22  // DHT 22  (AM2302), AM2321

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
  }

  Serial.println("");
  Serial.println("WiFi connected");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
}

void configureSensor(void) {
  /* You can also manually set the gain or enable auto-gain support */
  //tsl.setGain(TSL2561_GAIN_1X);      /* No gain ... use in bright light to avoid sensor saturation */
  //tsl.setGain(TSL2561_GAIN_16X);     /* 16x gain ... use in low light to boost sensitivity */
  tsl.enableAutoRange(true); /* Auto-gain ... switches automatically between 1x and 16x */

  /* Changing the integration time gives you better sensor resolution (402ms = 16-bit data) */
  //tsl.setIntegrationTime(TSL2561_INTEGRATIONTIME_13MS);      /* fast but low resolution */
  //tsl.setIntegrationTime(TSL2561_INTEGRATIONTIME_101MS);  /* medium resolution and speed   */
  tsl.setIntegrationTime(TSL2561_INTEGRATIONTIME_402MS); /* 16-bit data but slowest conversions */

  /* Update these values depending on what you've set above! */
  Serial.println("------------------------------------");
  Serial.print("Gain:         ");
  Serial.println("Auto");
  Serial.print("Timing:       ");
  Serial.println("402 ms");
  Serial.println("------------------------------------");
}


int influxDbUpdate() {
  //*********** Temp hum bar ******************************
  float humidity = dht.readHumidity();
  float temperature = dht.readTemperature();
  float pressure = bmp.readPressure() / 100.0;
  float temperature_BMP = bmp.readTemperature();


  if (isnan(humidity) || isnan(temperature) || isnan(pressure)) {
    return -1;
  }

#ifdef DEBUG
    Serial.println("=============");
    Serial.print(humidity);
    Serial.println(" %");
    Serial.print(temperature);
    Serial.println(" C");
    Serial.print(pressure);
    Serial.println(" Hpa");
#endif

  InfluxDBClient client(INFLUXDB_HOST, INFLUXDB_DATABASE);

  Point pointTemperature("temperature");
  pointTemperature.addTag("device", chipid);
  pointTemperature.addField("value", temperature);
  if (client.writePoint(pointTemperature) == false) {
    Serial.println("Influx Temperature write error");
    return -1;
  }

  Point pointHumidity("humidity");
  pointHumidity.addTag("device", chipid);
  pointHumidity.addField("value", humidity);
  if (client.writePoint(pointHumidity) == false) {
    Serial.println("Influx Humidity write error");
    return -1;
  }

  Point pointPressure("pressure");
  pointPressure.addTag("device", chipid);
  pointPressure.addField("value", pressure);
  if (client.writePoint(pointPressure) == false) {
    Serial.println("Influx Pressure write error");
    return -1;
  }

  Point pointTemperatureBMP("temperature_BMP085");
  pointTemperatureBMP.addTag("device", chipid);
  pointTemperatureBMP.addField("value", temperature_BMP);
  if (client.writePoint(pointTemperatureBMP) == false) {
    Serial.println("Influx Temperature BMP085 write error");
    return -1;
  }

  //*********** Light sensor ******************************
  sensors_event_t event;
  tsl.getEvent(&event);
  Point pointLight("light");
  pointLight.addTag("device", chipid);

  if (event.light) {
#ifdef DEBUG
    Serial.print(event.light);
    Serial.println(" lux");
#endif
    pointLight.addField("value", (int)event.light);
  } else {
#ifdef DEBUG
    Serial.println("Sensor overload");
#endif
    pointLight.addField("value", (int)0);
  }

  if (client.writePoint(pointLight) == false) {
    Serial.println("Influx Light write error");
    return -1;
  }

  return 0;
}

// ********* timer tick callback ******************
void pushTimerTick() {
  timerFlag = 1;
}

//************ Start van het programma *************
void setup() {
  Serial.begin(115200);
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
  if (!tsl.begin()) {
    /* There was a problem detecting the TSL2561 ... check your connections */
    Serial.print("Ooops, no TSL2561 detected ... Check your wiring or I2C ADDR!");
    while (1)
      ;
  }
  /* Setup the sensor gain and integration time */
  configureSensor();

  chipid = String(ESP.getChipId()).c_str();
  Serial.print("chipid = ");
  Serial.println(chipid);

  //Set update timer
  pushTimer.attach(INTERVALTIME, pushTimerTick);
  timerFlag = 1;  // stuur meteen de eerste update
}

void loop() {
  if (WiFi.status() == WL_CONNECTED) {
    if (timerFlag == 1) {
      if ((influxDbUpdate() != 0)) {
        //error in server request
        delay(4000);  //try after 4 sec
      } else {
        timerFlag = 0;
      }
    }
  } else {
    connectWifi();
  }
}
