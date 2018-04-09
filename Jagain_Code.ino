 #include <Arduino.h>
#include <Ticker.h>
#include <stdio.h>
#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <Adafruit_Sensor.h>
#include <DHT.h>
#include <DHT_U.h>

// const char* ssid = "The-EX";
// const char* password = "13456780";
// const char* ssid = "The-Ex 1";
// const char* password = "mugibarokah";
// const char* ssid = "Theex-HQ";
// const char* password = "JuraganPeceLyeye";
const char* ssid = "bapukpak";
const char* password = "bapuknih";
const char* mqtt_server = "ngehubx.online";
char msg[32];

WiFiClient espClient;
PubSubClient client(espClient);

#define DHTPIN            D5
#define DHTTYPE           DHT22

DHT_Unified dht(DHTPIN, DHTTYPE);

uint32_t delayMS;
const long intervalSuhu = 3000;
unsigned long previousMillis = 0;

// these variables are volatile because they are used during the interrupt service routine!
volatile int BPM;                   // used to hold the pulse rate
volatile int Signal;                // holds the incoming raw data
volatile int IBI = 600;             // holds the time between beats, must be seeded! 
volatile boolean Pulse = false;     // true when pulse wave is high, false when it's low
volatile boolean QS = false;        // becomes true when Arduoino finds a beat.

volatile int rate[10];                    // array to hold last ten IBI values
volatile unsigned long sampleCounter = 0; // used to determine pulse timing
volatile unsigned long lastBeatTime = 0;  // used to find IBI
volatile int P =512;                      // used to find peak in pulse wave, seeded
volatile int T = 512;                     // used to find trough in pulse wave, seeded
volatile int thresh = 512;                // used to find instant moment of heart beat, seeded
volatile int amp = 100;                   // used to hold amplitude of pulse waveform, seeded
volatile boolean firstBeat = true;        // used to seed rate array so we startup with reasonable BPM
volatile boolean secondBeat = false;      // used to seed rate array so we startup with reasonable BPM

Ticker flipper;

void sendDataToProcessing(char symbol, int data );
void ISRTr();

void setup_wifi() {  
  delay(10);
  // We start by connecting to a WiFi network
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(ssid);

  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("");
  Serial.println("WiFi connected!");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
}
  
void interruptSetup(){     
  // Initializes Ticker to have flipper run the ISR to sample every 2s as per original Sketch.
  flipper.setCallback(ISRTr);
  flipper.setInterval(2);
  flipper.start();
}
    
void callback(char* topic, byte* payload, unsigned int length);

void setup(){
  Serial.begin(115200);
  Serial.println("JAGAin turned ON");
  setup_wifi();
  client.setServer(mqtt_server, 1883);
  client.setCallback(callback);
  interruptSetup();                 // sets up to read Pulse Sensor signal every 2mS    
  dht.begin();
}

void reconnect() {
  // Loop until we're reconnected
  while (!client.connected()) {
    if (!WiFi.isConnected()){
      Serial.println("wifi not connected");
    } else {
      Serial.print("Client IP: ");
      Serial.println(WiFi.localIP());
    }

    Serial.println("Attempting MQTT connection...");
    // Attempt to connect
    if (client.connect("ESP8266Client1", "admintes", "admin123")) {
      Serial.println("connected");
      // Once connected, publish an announcement...
      // ... and resubscribe
      //client.subscribe("theex/smoq/isi");
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      // Wait 5 seconds before retrying
      //delay(5000);
    }
  }
}

void loop(){

  delay(delayMS);
  
  if (!client.connected()) {
    reconnect();
  }

//  flipper.update();
  if (QS == true){                       // Quantified Self flag is true when arduino finds a heartbeat
    // sendDataToProcessing('S', Signal);     // send Processing the raw Pulse Sensor data    
    sendDataToProcessing('B',BPM);   // send heart rate with a 'B' prefix
    sprintf_P(msg,"%d", BPM);
    client.publish("jagain/bpm",msg); 
    // sendDataToProcessing('Q',IBI);   // send time between beats with a 'Q' prefix
    QS = false;                      // reset the Quantified Self flag for next time    
  }

  unsigned long currentMillis = millis();
  
  if (previousMillis == 0){
    previousMillis = currentMillis;
  }

  if (currentMillis - previousMillis >= intervalSuhu) {
    // save the last time you blinked the LED
    previousMillis = currentMillis;
    
    sensors_event_t event;
    dht.temperature().getEvent(&event);

    if (isnan(event.temperature)) {
      Serial.println("Error reading temperature!");
      client.publish("jagain/suhu","!!!");
    } else {
      Serial.print("Temperature: ");
      Serial.print(event.temperature);
      Serial.println(" *C");
      dtostrf(event.temperature, 5, 1, msg);
      // sprintf_P(msg,"%d", (float)event.temperature);
      client.publish("jagain/suhu",msg);
    }
  }
}

void sendDataToProcessing(char symbol, int data ){
  Serial.print(symbol);                // symbol prefix tells Processing what type of data is coming
  Serial.println(data);                // the data to send culminating in a carriage return
}

void callback(char* topic, byte* payload, unsigned int length) {
  char inisi[80];

  Serial.print("payload: ");
  for (int i = 0; i < length; i++) {
    // Serial.print((char)payload[i]);
    inisi[i] = (char)payload[i];
  }
  Serial.println(inisi);
}

// THIS IS THE TICKER INTERRUPT SERVICE ROUTINE. 
// Ticker makes sure that we take a reading every 2 miliseconds
void ISRTr(){                         // triggered when flipper fires....
  cli();                               // disable interrupts while we do this
  Signal = analogRead(A0);              // read the Pulse Sensor 
  sampleCounter += 2;                         // keep track of the time in mS with this variable
  int N = sampleCounter - lastBeatTime;       // monitor the time since the last beat to avoid noise

  //  find the peak and trough of the pulse wave
  if(Signal < thresh && N > (IBI/5)*3){       // avoid dichrotic noise by waiting 3/5 of last IBI
    if (Signal < T){                        // T is the trough
      T = Signal;                         // keep track of lowest point in pulse wave 
    }
  }

  if(Signal > thresh && Signal > P){          // thresh condition helps avoid noise
    P = Signal;                             // P is the peak
  }                                        // keep track of highest point in pulse wave

  //  NOW IT'S TIME TO LOOK FOR THE HEART BEAT
  // signal surges up in value every time there is a pulse
  if (N > 250){                                   // avoid high frequency noise
    if ( (Signal > thresh) && (Pulse == false) && (N > (IBI/5)*3) ){        
      Pulse = true;                               // set the Pulse flag when we think there is a pulse
      // digitalWrite(blinkPin,HIGH);                // turn on pin 13 LED
      IBI = sampleCounter - lastBeatTime;         // measure time between beats in mS
      lastBeatTime = sampleCounter;               // keep track of time for next pulse

      if(secondBeat){                        // if this is the second beat, if secondBeat == TRUE
        secondBeat = false;                  // clear secondBeat flag
        Serial.println("secondBeat");        
        for(int i=0; i<=9; i++){             // seed the running total to get a realisitic BPM at startup
          rate[i] = IBI;                      
        }
      }

      if(firstBeat){                         // if it's the first time we found a beat, if firstBeat == TRUE
        firstBeat = false;                   // clear firstBeat flag
        secondBeat = true;                   // set the second beat flag
        Serial.println("firstBeat");
        sei();                               // enable interrupts again
        return;                              // IBI value is unreliable so discard it
      }

      // keep a running total of the last 10 IBI values
      word runningTotal = 0;                  // clear the runningTotal variable    

      for(int i=0; i<=8; i++){                // shift data in the rate array
        rate[i] = rate[i+1];                  // and drop the oldest IBI value 
        runningTotal += rate[i];              // add up the 9 oldest IBI values
      }

      rate[9] = IBI;                          // add the latest IBI to the rate array
      runningTotal += rate[9];                // add the latest IBI to runningTotal
      runningTotal /= 10;                     // average the last 10 IBI values 
      BPM = 60000/runningTotal;               // how many beats can fit into a minute? that's BPM!
      QS = true;                              // set Quantified Self flag 
      // QS FLAG IS NOT CLEARED INSIDE THIS ISR
    }                       
  }

  if (Signal < thresh && Pulse == true){   // when the values are going down, the beat is over
  //   digitalWrite(blinkPin,LOW);            // turn off pin 13 LED
    Pulse = false;                         // reset the Pulse flag so we can do it again
    amp = P - T;                           // get amplitude of the pulse wave
    thresh = amp/2 + T;                    // set thresh at 50% of the amplitude
    P = thresh;                            // reset these for next time
    T = thresh;
  }

  if (N > 2500){                           // if 2.5 seconds go by without a beat
    thresh = 512;                          // set thresh default
    P = 512;                               // set P default
    T = 512;                               // set T default
    lastBeatTime = sampleCounter;          // bring the lastBeatTime up to date        
    firstBeat = true;                      // set these to avoid noise
    secondBeat = false;                    // when we get the heartbeat back
  }

  sei();                                   // enable interrupts when youre done!
}// end isr
