/********************************( Include )********************************/
#include <Ethernet.h>
#include <HCSR04.h>
#include "DHT.h"

/********************************( Defines )********************************/
//########### Inputs
#define DHTPIN 2 // TODO
#define DHTTYPE DHT22
#define PIN_HANDBETRIEB 8

//########### Outputs
#define PIN_STATUS_LED 3
#define PIN_K1_LUEFTER 4
#define PIN_K2_PUMPE 5
#define PIN_K3_VENTIL 6

//########### Netzwerk
#define BEFEHL_RECEIVE_PORT 1488
#define SEND_INFO_PORT 8240
#define BUFFERSIZE 20

//########### Ultraschallsensor
#define PIN_TRIGGER 3
#define PIN_ECHO 9
#define ANZAHL_MESSUNGEN 10 // Wie viele Messungen pro Fuellstandsmesswert. Gerade Anzahl später für Median
#define MIN_FUELLSTAND 85 // Entfernung Sensor bis Boden in cm fuer 0 %
#define MAX_FUELLSTAND 10 // Entfernung Sensor bis Max Fuellstand fuer 100 %
#define TROCKENLAUFSCHUTZ 10 // Angabe in % des Fuellstandes

/********************************( Deklaration )********************************/
//########### Netzwerk
// Ethernet Eigenschaften
byte mac[] = { 0xAD, 0xEE, 0xBB, 0xFF, 0xFA, 0xAD };
IPAddress ip(10,0,5,23);
IPAddress myDns(10, 0, 1, 1);
IPAddress gateway(10, 0, 0, 1);
IPAddress subnet(255, 255, 240, 0);
// NodeRed Server Adresse 
IPAddress serverIP(10,0,1,1);
// Port Variablen
unsigned int befehl_receive_port = BEFEHL_RECEIVE_PORT;
unsigned int send_info_port = SEND_INFO_PORT;
// UDP
EthernetUDP udp_client;
char packetBuffer[BUFFERSIZE];

// Füllstandssensor
HCSR04 fuellstand_sensor(PIN_TRIGGER, PIN_ECHO);
float fuellstand_messwerte[ANZAHL_MESSUNGEN];

//########### Variablen
bool luefter_running;
bool wasser_running;
bool handbetrieb;
float temperatur;
float luftfeuchtigkeit;
int fuellstand;
float m;
float b;

//########### DHT Sensor
DHT dht(DHTPIN, DHTTYPE);

/********************************( setup )********************************/
void setup() {
  // serielle Schnittstelle Arduino
  Serial.begin(9600);
  Serial.println("Starte Setup");

  // globale Variablen initalisieren
  luefter_running = false;
  wasser_running = false;
  handbetrieb = false;
  luftfeuchtigkeit = 0.0;
  temperatur = 0.0;
  fuellstand = 0;
  // Parameter lineare Gleichung fuer Fuellstandsensor
  m = (0-100)/(MIN_FUELLSTAND - MAX_FUELLSTAND);
  b = -1.0 * (m * MAX_FUELLSTAND - 100);

  // packetBuffer mit 0 beschreiben
  for(int i=0; i < BUFFERSIZE;i++){
    packetBuffer[i] = 0;
  }

  // Messwerte Array mit 0 beschreiben
    for(int i=0; i < ANZAHL_MESSUNGEN;i++){
    fuellstand_messwerte[i] = 0;
  }

  //GPIO inputs
  pinMode(PIN_HANDBETRIEB, INPUT_PULLUP);
  //pinMode(PIN_ECHO, INPUT);

  //GPIO outputs
  pinMode(PIN_STATUS_LED, OUTPUT);
  //pinMode(PIN_TRIGGER, OUTPUT);
  pinMode(PIN_K1_LUEFTER, OUTPUT);
  pinMode(PIN_K2_PUMPE, OUTPUT);
  pinMode(PIN_K3_VENTIL, OUTPUT);

  // Zur Sicherheit alles ausschalten am Anfang
  luefterAus();
  pumpeAus();
  ventilZu();

  // Ethernetverbindung initialisieren
  Ethernet.begin(mac, ip, myDns, gateway, subnet);
  // UDP listening starten
  udp_client.begin(befehl_receive_port);

  // DHT Sensor starten
  dht.begin();

  Serial.println("Setup beendet");
}


/********************************( Main )********************************/
void loop() {
  // Falls Handbetrieb angewählt war, aber nun ausgeschaltet, dann lüfter und Pumpe aus
  // TODO Geht bestimmt noch schöner
  if(handbetrieb == true){
    checkHandbetriebschalter();
    if(handbetrieb == false){
      luefter_running = false;
      wasser_running = false;
    }
  }else{
    checkHandbetriebschalter();
  }

  if(handbetrieb){
    Serial.println("Handbetrieb");
    luefter_running = true;
    wasser_running = true;
  }else{
    Serial.println("Automatik");
    // Lese ankommende UDP Pakete ein
    int packet_size = udp_client.parsePacket();
    if(packet_size > 0 && packet_size < BUFFERSIZE){
      // Datenpaket einlesen
      udp_client.read(packetBuffer,BUFFERSIZE);
      Serial.println(packetBuffer);
      Serial.println(packet_size);
      String packetBufferString = packetBuffer;
      // Befehle ausfuehren ACHTUNG STRING und kein INT
      // Erste Stelle im Array ist fuer den Luefter An/Aus
      if(packetBufferString.equals("LE")){
        luefter_running = true;
      }else if(packetBufferString.equals("LA")){
        luefter_running = false;
      }else if(packetBufferString.equals("WE")){
        wasser_running = true;
      }else if(packetBufferString.equals("WA")){
        wasser_running = false;
      }
      // Buffer wieder leeren
      for(int i=0; i < BUFFERSIZE;i++){
        packetBuffer[i] = 0;
      }
    }
  }
  
  // Luefter Ein / Ausschalten
  if(luefter_running){
    luefterEin();
  }else{
    luefterAus();
  }
  // Pumpe Ein / Ausschalten
  if(wasser_running && (fuellstand >= TROCKENLAUFSCHUTZ)){
    ventilAuf();
    pumpeEin();
  }else{
    ventilZu();
    pumpeAus();
  }

  // Temperatur und Luftfeuchtigkeit auslesen
  readDHTdata();
  
  // Fuellstand Wasserbehaehlter
  getFuellstand();
  
  // Status senden
  sendStatus();

  // 10 Sekunden warten
  delay(10000);
}


/********************************( Hilfsfunktionen )********************************/
// Function to send UDP packets
void sendStatus()
{
  String text = String(luefter_running) + ";" + String(wasser_running) + ";" + ((int)temperatur) + ";" + ((int)luftfeuchtigkeit) + ";" + String(handbetrieb) + ";" + fuellstand;
  Serial.println(text);
  udp_client.beginPacket(serverIP, SEND_INFO_PORT);
  udp_client.print(text);
  udp_client.endPacket();
}

// DHT Sensor auslesen
void readDHTdata(){
  temperatur = dht.readTemperature(); // Lesen der Temperatur in °C und speichern in die Variable
  luftfeuchtigkeit = dht.readHumidity(); // Lesen der Luftfeuchtigkeit und speichern in die Variable
}

// Fuellstand messen
int getFuellstand(){
  unsigned long entfernung;
  unsigned long summe_werte_entfernung = 0;

  // Anzahl an Messwerten aufnehmen
  for(int i=0; i<ANZAHL_MESSUNGEN; i++){
    fuellstand_messwerte[i] = fuellstand_sensor.dist();
  }

  // Messwerte Sortieren mit Bubblesort
  int temp;
  for (int i = 0; i < ANZAHL_MESSUNGEN - 1; i++) {
    // Letzte i Elemente sind bereits sortiert, daher nur bis n-1-i durchlaufen
    for (int j = 0; j < ANZAHL_MESSUNGEN - 1 - i; j++) {
      // Falls das aktuelle Element größer als das nächste Element ist, tauschen
      if (fuellstand_messwerte[j] > fuellstand_messwerte[j + 1]) {
        temp = fuellstand_messwerte[j];
        fuellstand_messwerte[j] = fuellstand_messwerte[j + 1];
        fuellstand_messwerte[j + 1] = temp;
      }
    }
  }

  // Ausgabe jeder einzelnen Messung für Debug
  //for(int i=0; i<ANZAHL_MESSUNGEN; i++){
  //  Serial.println(fuellstand_messwerte[i]);
  //}

  // Nehme den Median bei gerade Anzahl von Messungen
  entfernung = fuellstand_messwerte[ANZAHL_MESSUNGEN / 2];
  fuellstand = m * entfernung + b;

  return fuellstand;
}

// Handbetrieb Schalter auslesen
void checkHandbetriebschalter(){
  if(digitalRead(PIN_HANDBETRIEB)==LOW){
    //Interner Pull Up. also Wenn Low, dann geschaltet
    handbetrieb = true;
  }else{
    handbetrieb = false;
  }
}

// Aktorik schalten
void luefterEin(){
  digitalWrite(PIN_K1_LUEFTER, LOW);
}
void luefterAus(){
  digitalWrite(PIN_K1_LUEFTER, HIGH);
}
void pumpeEin(){
  digitalWrite(PIN_K2_PUMPE, LOW);
}
void pumpeAus(){
  digitalWrite(PIN_K2_PUMPE, HIGH);
}
void ventilAuf(){
  digitalWrite(PIN_K3_VENTIL, LOW);
}
void ventilZu(){
  digitalWrite(PIN_K3_VENTIL, HIGH);
}

void statusLedEin(){
  digitalWrite(PIN_STATUS_LED, HIGH);
}
void statusLedAus(){
  digitalWrite(PIN_STATUS_LED, LOW);
}