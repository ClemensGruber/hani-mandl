/*
  Abfuellwaage Version 0.2.4
  --------------------------
  Copyright (C) 2018-2020 by Marc Vasterling, Marc Wetzel, Clemens Gruber, Marc Junker, Andreas Holzhammer
            
  2018-05 Marc Vasterling    | initial version, 
                               published in the Facebook group "Imkerei und Technik. Eigenbau",
                               Marc Vasterling: "meinen Code kann jeder frei verwenden, ändern und hochladen wo er will, solange er nicht seinen eigenen Namen drüber setzt."
  2018-06 Marc Vasterling    | improved version, 
                               published in the Facebook group also
  2019-01 Marc Wetzel        | Refakturierung und Dokumentation, 
                               published in the Facebook group also
  2019-02 Clemens Gruber     | code beautifying mit kleineren Umbenennungen bei Funktionen und Variablen
                               Anpssung fuer Heltec WiFi Kit 32 (ESP32 onboard OLED) 
                               - pins bei OLED-Initialisierung geaendert
                               - pins geaendert, um Konflikte mit hard wired pins des OLEDs zu vermeiden 
  2019-02 Clemens Gruber     | Aktivierung der internen pull downs für alle digitalen Eingaenge
  2019-02 Clemens Gruber     | "normale" pins zu Vcc / GND geaendert um die Verkabelung etwas einfacher und angenehmer zu machen
  2020-05 Andreas Holzhammer | Anpassungen an das veränderte ;-( pin-Layout der Version 2 des Heltec 
                               wird verkauft als "New Wifi Kit 32" oder "Wifi Kit 32 V2"
  2020-05 Marc Junker        | - Erweiterung von Poti auf Rotary Encoder 
                               - alle Serial.prints in #ifdef eingeschlossen
                               - "Glas" nun als Array mit individuellem Tara
                               - Korrekturwert und Auswahl der Füllmenge über Drücken & Drehen des Rotary einstellbar
  2020-05 Andreas Holzhammer | - Tara pro abzufüllendem Glas automatisch anpassen (Variable tara_glas)
                               - Code läuft auch ohne Waage
  2020-06 Andreas Holzhammer | - Code wahlweise mit Heltec V1 oder V2 nutzbar
                               - Code wahlweise mit Poti oder Rotary nutzbar
                               - Tara pro Glas einstellbar
                               - Öffnungswinkel für Maximale Öffnung und Feindosierung im Setup konfigurierbar
                               - Korrektur und Glasgröße im Automatikmodus per Rotary Encoder Button wählbar
                               - Preferences löschbar über Setup
                               - Gewicht blinkt bei Vollautomatik, wenn nicht vollständig gefüllt
                               - Nicht kalibrierte Waage anzeigen, fehlende Waage anzeigen
                               - Tara wird nur bei >20g gesetzt, verhindert den Autostart bei leerer Waage
                               - Tarieren der Waage bei jedem Start bis +-20g. Sonst Warnung
                               0.2.4
                               - SCALE_READS auf 2 setzen? ca. 100ms schneller als 3, schwankt aber um +-1g
                               - Reihenfolge der Boot-Meldungen optimiert, damit nur relevante Warnungen ausgegeben werden
                               - Autokorrektur implementiert
                               - LOGO!
 
  This code is in the public domain.
   
  Hinweise zur Hardware
  ---------------------
  - bei allen digitalen Eingängen sind interne pull downs aktiviert, keine externen-Widerständen nötig! 
*/

#include <Arduino.h>
#include <Wire.h>
#include <U8g2lib.h>      /* aus dem Bibliotheksverwalter */
#include <HX711.h>        /* aus dem Bibliotheksverwalter */
#include <ESP32_Servo.h>   /* https://github.com/jkb-git/ESP32Servo */
#include <Preferences.h>  /* aus dem BSP von expressif, wird verfügbar wenn das richtige Board ausgewählt ist */

//
// Hier den Code auf die verwendete Hardware einstellen
//
#define HARDWARE_LEVEL 2        // 1 = originales Layout mit Schalter auf Pin 19/22/21
                                // 2 = Layout für V2 mit Schalter auf Pin 23/19/22
//#define USE_ORIGINAL_SERVO_VARS // definieren, falls die Hardware mit dem alten Programmcode mit Poti aufgebaut wurde
                                  // Sonst bleibt der Servo in Stop-Position einige Grad offen! Nach dem Update erst prüfen!
#define ROTARY_SCALE 2          // in welchen Schritten springt unser Rotary Encoder. 
                                // Beispiele: KY-040 = 2, HW-040 = 1, für Poti-Betrieb auf 1 setzen
#define USE_ROTARY              // Rotary benutzen
#define USE_ROTARY_SW           // Taster des Rotary benutzen
//#define USE_POTI              // Poti benutzen -> ACHTUNG, im Normalfall auch USE_ROTARY_SW deaktivieren!
#define KULANZ_AUTOKORREKTUR 5  // gewollte Überfüllung im Autokorrekturmodus in Gramm
//
// Ende Benutzereinstellungen!
// 

//
// Ab hier nur verstellen wenn Du genau weisst, was Du tust!
//
#define isDebug 4        // serielle debug-Ausgabe aktivieren. Mit >3 wird jeder Messdurchlauf ausgegeben
#define AUTOKORREKTUR    // automatische Anpassung des Korrekturwerts
//#define POTISCALE        // Poti simuliert eine Wägezelle, nur für Testbetrieb!

// Ansteuerung der Waage
#define SCALE_READS 2      // Parameter für hx711 Library. Messwert wird aus der Anzahl gemittelt
#define SCALE_GETUNITS(n)  (waage_vorhanden ? scale.get_units(n) : simulate_scale(n) )

// Rotary Encoder Taster zieht Pegel auf Low, Start/Stop auf High!
#ifdef USE_ROTARY_SW
#define SELECT_SW outputSW
#define SELECT_PEGEL LOW
#else
#define SELECT_SW button_start_pin
#define SELECT_PEGEL HIGH
#endif

// Betriebsmodi 
#define MODE_SETUP       0
#define MODE_AUTOMATIK   1
#define MODE_HANDBETRIEB 2


// ** Definition der pins 
// ----------------------

// OLED fuer Heltec WiFi Kit 32 (ESP32 onboard OLED) 
U8G2_SSD1306_128X64_NONAME_F_SW_I2C u8g2(U8G2_R0, /* clock=*/ 15, /* data=*/ 4, /* reset=*/ 16);

// Rotary Encoder
const int outputA  = 33;
const int outputB  = 26;
const int outputSW = 32;

// Servo
const int servo_pin = 2;

// 3x Schalter Ein 1 - Aus - Ein 2
#if HARDWARE_LEVEL == 1
const int switch_betrieb_pin = 19;
const int switch_vcc_pin     = 22;     // <- Vcc 
const int switch_setup_pin   = 21;
#elif HARDWARE_LEVEL == 2
const int switch_betrieb_pin = 23;
const int switch_vcc_pin     = 19;     // <- Vcc 
const int switch_setup_pin   = 22;
const int vext_ctrl_pin      = 21;     // Vext control pin
#else
#error Hardware Level nicht definiert! Korrektes #define setzen!
#endif

// Taster 
const int button_start_vcc_pin = 13;  // <- Vcc 
const int button_start_pin     = 12;
const int button_stop_vcc_pin  = 14;  // <- Vcc 
const int button_stop_pin      = 27;

// Poti
const int poti_pin = 39;

// Wägezelle-IC 
const int hx711_sck_pin = 17;
const int hx711_dt_pin  = 5;

Servo servo;
HX711 scale;
Preferences preferences;

// Datenstrukturen für Rotary Encoder
struct rotary {                        
  int Value;
  int Minimum;
  int Maximum;
  int Step;
};
#define SW_WINKEL    0
#define SW_KORREKTUR 1
#define SW_MENU      2
struct rotary rotaries[3];         // Werden in setup() initialisiert
int rotary_select = SW_WINKEL;
static boolean rotating = false;   // debounce management für Rotary Encoder

// Füllmengen für 5 Gläser
struct glas { 
  int Gewicht;
  int Tara;
};
struct glas glaeser[5] = { {  125, -9999 },
                           {  250, -9999 },
                           {  375, -9999 },
                           {  500, -9999 },
                           { 1000, -9999 } };

// Allgemeine Variablen
int i;                          // allgemeine Zählvariable
int pos;                        // aktuelle Position des Poti bzw. Rotary 
int gewicht;                    // aktuelles Gewicht
int tara;                       // Tara für das ausgewählte Glas, für Automatikmodus
int tara_glas;                  // Tara für das aktuelle Glas, falls Glasgewicht abweicht
long gewicht_leer;              // Gewicht der leeren Waage
float faktor;                   // Skalierungsfaktor für Werte der Waage
int fmenge;                     // ausgewählte Füllmenge
int fmenge_index;               // Index in gläser[]
int korrektur;                  // Korrekturwert für Abfüllmenge
int autostart;                  // Vollautomatik ein/aus
int autokorrektur;              // Autokorrektur ein/aus
int winkel;                     // aktueller Servo-Winkel
int winkel_hard_min = 0;        // Hard-Limit für Servo
int winkel_hard_max = 155;      // Hard-Limit für Servo
int winkel_min = 0;             // nicht einstellbar, wird per Hardware angepasst!
int winkel_max = 85;            // konfigurierbar im Setup
int winkel_fein = 35;           // konfigurierbar im Setup
float fein_dosier_gewicht = 60; // float wegen Berechnung des Schliesswinkels
int servo_aktiv = 0;            // Servo aktivieren ja/nein
char ausgabe[30];               // Fontsize 12 = 13 Zeichen maximal in einer Zeile
int modus = -1;                 // Bei Modus-Wechsel den Servo auf Minimum fahren
int auto_aktiv = 0;             // Für Automatikmodus - System ein/aus?
int waage_vorhanden = 0;        // HX711 nicht ansprechen, wenn keine Waage angeschlossen ist, sonst Crash

// Simuliert die Dauer des Wägeprozess, wenn keine Waage angeschlossen ist. Wirkt sich auf die Blinkfrequenz im Automatikmodus aus.
long simulate_scale(int n) {
    long sim_gewicht = 9500;
    while (n-- >= 1) { 
      delay(40);    // empirisch ermittelt. n=2: 10, n=3: 40, n=4: 50
    }
#ifdef POTISCALE
    sim_gewicht = (map(analogRead(poti_pin), 0, 4095, 0, 700));
#endif   
    return sim_gewicht;
}

#ifdef USE_ROTARY_SW
// Rotary Taster. Der Interrupt kommt nur im Automatikmodus zum Tragen und nur wenn der Servo inaktiv ist.
// Der Taster schaltet in einen von drei Modi, in denen unterschiedliche Werte gezählt werden.
void IRAM_ATTR isr1() {
  static unsigned long last_interrupt_time = 0; 
  unsigned long interrupt_time = millis();

  if (interrupt_time - last_interrupt_time > 300) {      // If interrupts come faster than 300ms, assume it's a bounce and ignore
    if ( modus == MODE_AUTOMATIK && servo_aktiv == 0 ) { // nur im Automatik-Modus interessiert uns der Click
      rotary_select = (rotary_select + 1) % 3;
#ifdef isDebug
    Serial.print("Rotary Button changed to ");
    Serial.println(rotary_select);
#endif 
    }
    last_interrupt_time = interrupt_time;
  }
}
#endif

#ifdef USE_ROTARY
// Rotary Encoder. Zählt in eine von drei Datenstrukturen: 
// SW_WINKEL    = Einstellung des Servo-Winkels
// SW_KORREKTUR = Korrekturfaktor für Füllgewicht
// SW_MENU      = Zähler für Menuauswahlen  
void IRAM_ATTR isr2() {
  static int aState;
  static int aLastState = 2;  // reale Werte sind 0 und 1
  
  if ( rotating ) delay (1);  // wait a little until the bouncing is done
   
  aState = digitalRead(outputA); // Reads the "current" state of the outputA
    if (aState != aLastState) {     
      // If the outputB state is different to the outputA state, that means the encoder is rotating clockwise
      if (digitalRead(outputB) != aState) {
         rotaries[rotary_select].Value -= rotaries[rotary_select].Step;
      } else {    // counter-clockwise
         rotaries[rotary_select].Value += rotaries[rotary_select].Step;
      }
      if ( rotaries[rotary_select].Value < rotaries[rotary_select].Minimum ) { rotaries[rotary_select].Value = rotaries[rotary_select].Minimum; }
      if ( rotaries[rotary_select].Value > rotaries[rotary_select].Maximum ) { rotaries[rotary_select].Value = rotaries[rotary_select].Maximum; }

      rotating = false;
#ifdef isDebug
      Serial.print(" Rotary Value changed to ");
      Serial.println(getRotariesValue(rotary_select));
#endif 
    }
    aLastState = aState; // Updates the previous state of the outputA with the current state
}
#endif

//
// Skalierung des Rotaries für verschiedene Rotary Encoder oder Simulation über Poti
int getRotariesValue( int rotary_mode ) {
#ifdef USE_ROTARY
    return (rotaries[rotary_mode].Value / ROTARY_SCALE);
#elif defined USE_POTI
    int poti_min = (rotaries[rotary_mode].Minimum / ROTARY_SCALE);
    int poti_max = (rotaries[rotary_mode].Maximum / ROTARY_SCALE);
    if( rotaries[rotary_mode].Step > 0 ) {
       return (map(analogRead(poti_pin), 0, 4095, poti_min, poti_max));
    } else {
       return (map(analogRead(poti_pin), 0, 4095, poti_max, poti_min));
    }
#else
#error Weder Rotary noch Poti aktiviert!
#endif 
}
void setRotariesValue( int rotary_mode, int rotary_value ) {
    rotaries[rotary_mode].Value = rotary_value * ROTARY_SCALE;
}
void initRotaries( int rotary_mode, int rotary_value, int rotary_min, int rotary_max, int rotary_step ) {
    rotaries[rotary_mode].Value     = rotary_value * ROTARY_SCALE;
    rotaries[rotary_mode].Minimum   = rotary_min   * ROTARY_SCALE;
    rotaries[rotary_mode].Maximum   = rotary_max   * ROTARY_SCALE;
    rotaries[rotary_mode].Step      = rotary_step;

#ifdef isDebug
    Serial.print("Rotary Mode: ");   Serial.print(rotary_mode);
    Serial.print(" rotary_value: "); Serial.print(rotary_value);
    Serial.print(" Value: ");        Serial.print(rotaries[rotary_mode].Value);
    Serial.print(" Min: ");          Serial.print(rotaries[rotary_mode].Minimum);
    Serial.print(" Max: ");          Serial.print(rotaries[rotary_mode].Maximum);
    Serial.print(" Step: ");         Serial.print(rotaries[rotary_mode].Step);
    Serial.print(" Scale: ");        Serial.println(ROTARY_SCALE);
#endif
}
// Ende Funktionen für den Rotary Encoder
//


void getPreferences(void) {
    preferences.begin("EEPROM", false);            // Parameter aus dem EEPROM lesen

    faktor       = preferences.getFloat("faktor", 0.0);  // falls das nicht gesetzt ist -> Waage ist nicht kalibriert
    pos          = preferences.getUInt("pos", 0);
    gewicht_leer = preferences.getUInt("gewicht_leer", 0); 
    korrektur    = preferences.getUInt("korrektur", 0);
    autostart    = preferences.getUInt("autostart", 0);
    autokorrektur = preferences.getUInt("autokorrektur", 0);
    fmenge_index = preferences.getUInt("fmenge_index", 3);
    winkel_max   = preferences.getUInt("winkel_max", winkel_max);
    winkel_fein  = preferences.getUInt("winkel_fein", winkel_fein);

    i = 0;
    while( i < 5 ) {
      sprintf(ausgabe, "tara%d", i);
      glaeser[i].Tara= preferences.getInt(ausgabe, -9999);
      i++;
    }

    preferences.end();

#ifdef isDebug
    Serial.println("Preferences:");
    Serial.print("pos = ");          Serial.println(pos);
    Serial.print("faktor = ");       Serial.println(faktor);
    Serial.print("gewicht_leer = "); Serial.println(gewicht_leer);
    Serial.print("korrektur = ");    Serial.println(korrektur);
    Serial.print("autostart = ");    Serial.println(autostart);
    Serial.print("autokorrektur = ");Serial.println(autokorrektur);
    Serial.print("fmenge_index = "); Serial.println(fmenge_index);
    Serial.print("winkel_max = ");   Serial.println(winkel_max);
    Serial.print("winkel_fein = ");  Serial.println(winkel_fein);

    i = 0;
    while( i < 5 ) {
      sprintf(ausgabe, "tara%d = ", i);
      Serial.print(ausgabe);         Serial.println(glaeser[i].Tara);
      i++;
    }
#endif
}

void setPreferences(void) {
    int winkel = getRotariesValue(SW_WINKEL);
    
    preferences.begin("EEPROM", false);
    preferences.putFloat("faktor", faktor);
    preferences.putUInt("gewicht_leer", gewicht_leer);
    preferences.putUInt("pos", winkel);
    preferences.putUInt("korrektur", korrektur);
    preferences.putUInt("autostart", autostart);
    preferences.putUInt("autokorrektur", autokorrektur);
    preferences.putUInt("winkel_max", winkel_max);
    preferences.putUInt("winkel_fein", winkel_fein);
    preferences.putUInt("fmenge_index", fmenge_index);

    i = 0;
    while( i < 5 ) {
      sprintf(ausgabe, "tara%d", i);
      preferences.putInt(ausgabe, glaeser[i].Tara);
      i++;
    }
    preferences.end();

#ifdef isDebug
    Serial.println("Set Preferences:");
    Serial.print("pos = ");          Serial.println(winkel);
    Serial.print("faktor = ");       Serial.println(faktor);
    Serial.print("gewicht_leer = "); Serial.println(gewicht_leer);
    Serial.print("korrektur = ");    Serial.println(korrektur);
    Serial.print("autostart = ");    Serial.println(autostart);
    Serial.print("autokorrektur = ");Serial.println(autokorrektur);
    Serial.print("fmenge_index = "); Serial.println(fmenge_index);
    Serial.print("winkel_max = ");   Serial.println(winkel_max);
    Serial.print("winkel_fein = ");  Serial.println(winkel_fein);

    i = 0;
    while( i < 5 ) {
      sprintf(ausgabe, "tara%d = ", i);
      Serial.print(ausgabe);         Serial.println(glaeser[i].Tara);
      i++;
    }
#endif
}

void setupTara(void) {
    int j;
    tara = 0;

    initRotaries( SW_MENU, 0, 0, 4, -1 );   // Set Encoder to Menu Mode, four Selections, inverted count
      
    i = 0;
    while ( i == 0 ) {
      if ( digitalRead(SELECT_SW) == SELECT_PEGEL ) {
        tara = (int(SCALE_GETUNITS(10)));
        if ( tara > 20 ) {                  // Gläser müssen mindestens 20g haben
           glaeser[getRotariesValue(SW_MENU)].Tara = tara; 
        }
        i++;
      }
      
      u8g2.setFont(u8g2_font_courB10_tf);
      u8g2.clearBuffer();

      j = 0;
      while( j < 5  ) {
        u8g2.setCursor(10, 10+(j*13));   
        sprintf(ausgabe, "%4dg", glaeser[j].Gewicht); 
        u8g2.print(ausgabe);
        u8g2.setCursor(65, 10+(j*13));
        if ( glaeser[j].Tara > 0 ) { 
          sprintf(ausgabe, "%6dg", glaeser[j].Tara); 
          u8g2.print(ausgabe);
        } else {
          u8g2.print("  fehlt");
        }
        j++;
      }
      u8g2.setCursor(0, 10+(getRotariesValue(SW_MENU)*13) );    
      u8g2.print("*");
      u8g2.sendBuffer();
    }
    delay(2000);
}

void setupCalibration(void) {
    long gewicht_raw;
    
    u8g2.clearBuffer();
    u8g2.setCursor(0, 12);    u8g2.print("Bitte Waage");
    u8g2.setCursor(0, 28);    u8g2.print("leeren");
    u8g2.setCursor(0, 44);    u8g2.print("und mit OK");
    u8g2.setCursor(0, 60);    u8g2.print("bestaetigen");
    u8g2.sendBuffer();
    
    i = 1;
    while (i > 0) {
      if ((digitalRead(SELECT_SW)) == SELECT_PEGEL) {
         scale.set_scale();
         scale.tare();
         i = 0;
      }
    }
    
    u8g2.setFont(u8g2_font_courB12_tf);
    u8g2.clearBuffer();
    u8g2.setCursor(0, 12);    u8g2.print("Bitte 500g");
    u8g2.setCursor(0, 28);    u8g2.print("aufstellen");
    u8g2.setCursor(0, 44);    u8g2.print("und mit OK");
    u8g2.setCursor(0, 60);    u8g2.print("bestaetigen");
    u8g2.sendBuffer();
    
    i = 1;
    while (i > 0) {
      if ((digitalRead(SELECT_SW)) == SELECT_PEGEL) {
        gewicht_raw  = (int(SCALE_GETUNITS(10)));
        faktor       = gewicht_raw / 500.0;
        scale.set_scale(faktor);
        gewicht_leer = scale.get_offset();    // leergewicht der Waage speichern
#ifdef isDebug
        Serial.print("gewicht_leer = ");
        Serial.print(gewicht_leer);
        Serial.print(" Faktor = ");
        Serial.println(faktor);
#endif        
        delay(1000);
        i = 0;        
      }
    }
}



void setupKorrektur(void) {
    rotary_select = SW_KORREKTUR;

    i = 1;
    while (i > 0) {
      korrektur = getRotariesValue(SW_KORREKTUR);
      u8g2.setFont(u8g2_font_courB14_tf);
      u8g2.clearBuffer();
      u8g2.setCursor(10, 12);
      u8g2.print("Korrektur");
      u8g2.setCursor(40, 28);
      u8g2.print(korrektur);
      u8g2.sendBuffer();
      
      if ((digitalRead(SELECT_SW)) == SELECT_PEGEL) {
        u8g2.setCursor(100, 28);
        u8g2.print("OK");
        u8g2.sendBuffer();
        delay(1000);
        i = 0;
      }
    }
    rotary_select = SW_MENU;
}

void setupFuellmenge(void) {
    int j;
    initRotaries(SW_MENU, fmenge_index, 0, 4, -1);
      
    u8g2.setFont(u8g2_font_courB10_tf);
    i = 1;
    while (i > 0) {
      pos = getRotariesValue(SW_MENU);

      u8g2.clearBuffer();
      j = 0;
      while( j < 5  ) {
        u8g2.setCursor(10, 10+(j*13));    
        sprintf(ausgabe, "%4dg", glaeser[j].Gewicht);
        u8g2.print(ausgabe);
        j++;
      }
      u8g2.setCursor(0, 10+(getRotariesValue(SW_MENU)*13));    
      u8g2.print("*");
      u8g2.sendBuffer();

      if ( digitalRead(SELECT_SW) == SELECT_PEGEL ) {
        fmenge = glaeser[pos].Gewicht;
        tara   = glaeser[pos].Tara;
        fmenge_index = pos; 
        
        u8g2.setCursor(100, 10+(getRotariesValue(SW_MENU)*13));
        u8g2.print("OK");
        u8g2.sendBuffer();
        delay(1000);
        i = 0;
      }
    }
}

void setupAutostart(void) {
  initRotaries(SW_MENU, 1, 1, 2, -1);
  
  i = 1;
  while (i > 0) {
    pos = getRotariesValue(SW_MENU);
    u8g2.setFont(u8g2_font_courB14_tf);
    u8g2.clearBuffer();
    u8g2.setCursor(10, 12);    u8g2.print("Auto EIN");
    u8g2.setCursor(10, 28);    u8g2.print("Auto AUS");
    
    u8g2.setCursor(0, 12+((pos-1)*16));
    u8g2.print("*");
    u8g2.sendBuffer();
 
    if ((digitalRead(SELECT_SW)) == SELECT_PEGEL) {
      if (pos == 1) { autostart = 1; }
      if (pos == 2) { autostart = 2; }

      u8g2.setCursor(105, 12+((pos-1)*16));
      u8g2.print("OK");
      u8g2.sendBuffer();
      delay(1000);
      i = 0;
    }
  }
}

void setupAutokorrektur(void) {
  initRotaries(SW_MENU, autokorrektur, 0, 1, -1);
  
  i = 1;
  while (i > 0) {
    pos = getRotariesValue(SW_MENU);
    u8g2.setFont(u8g2_font_courB12_tf);
    u8g2.clearBuffer();
    u8g2.setCursor(10, 12);    u8g2.print("Autok. Aus");
    u8g2.setCursor(10, 28);    u8g2.print("Autok. Ein");
    sprintf(ausgabe,"Kulanz: %dg", KULANZ_AUTOKORREKTUR);
    u8g2.setCursor(10, 44);    u8g2.print(ausgabe);
    
    u8g2.setCursor(0, 12+(pos*16));
    u8g2.print("*");
    u8g2.sendBuffer();
 
    if ((digitalRead(SELECT_SW)) == SELECT_PEGEL) {
      if (pos == 0) { autokorrektur = 0; }
      if (pos == 1) { autokorrektur = 1; }

      u8g2.setFont(u8g2_font_open_iconic_check_2x_t);
      u8g2.drawGlyph(112, 14+(pos*16), 0x40);

//      u8g2.setCursor(110, 12+(pos*16));
//      u8g2.print("OK");
      u8g2.sendBuffer();
      delay(1000);
      i = 0;
    }
  }
}


// Funktion zum anpassen eines beliebigen Zahlwerts (Öffnungswinkel-Maximum und -Feindosierung) 
// Könnte auch für Korrektur genutzt werden, der Wert hat aber seine eigene Datenstruktur
void setupZahlwert(int *param, int min, int max, char *name) {
    initRotaries(SW_MENU, *param, min, max, 1);
          
    i = 1;
    while (i > 0) {
      pos = getRotariesValue(SW_MENU);
      u8g2.setFont(u8g2_font_courB12_tf);
      u8g2.clearBuffer();
      u8g2.setCursor(10, 12);
      u8g2.print(name);
      u8g2.setCursor(40, 28);
      u8g2.print(pos);
      u8g2.sendBuffer();
      
      if ((digitalRead(SELECT_SW)) == SELECT_PEGEL) {
        *param = pos;
        u8g2.setCursor(100, 28);
        u8g2.print("OK");
        u8g2.sendBuffer();
        delay(1000);
        i = 0;
      }
    }
}

void setupClearPrefs(void) {
  initRotaries(SW_MENU, 1, 0, 1, -1);
  
  i = 1;
  while (i > 0) {
    pos = getRotariesValue(SW_MENU);
    u8g2.setFont(u8g2_font_courB10_tf);
    u8g2.clearBuffer();
    u8g2.setCursor(10, 12);    u8g2.print("Loeschen");
    u8g2.setCursor(10, 28);    u8g2.print("Zurueck!");
    
    u8g2.setCursor(0, 12+((pos)*16));
    u8g2.print("*");
    u8g2.sendBuffer();
 
    if ((digitalRead(SELECT_SW)) == SELECT_PEGEL) {      
      u8g2.setCursor(105, 12+((pos)*16));
      u8g2.print("OK");
      u8g2.sendBuffer();
      if ( pos == 0) {
        preferences.begin("EEPROM", false);
        preferences.clear();
        preferences.end();
        // gelöschte Werte einlesen, sonst bleiben die Variablen erhalten
        getPreferences();
      }
      delay(1000);
      i = 0;
    }
  }
}   

void processSetup(void) {
  if ( modus != MODE_SETUP ) {
     modus = MODE_SETUP;
     winkel = winkel_min;          // Hahn schliessen
     servo_aktiv = 0;              // Servo-Betrieb aus
     servo.write(winkel);
     rotary_select = SW_MENU;
     initRotaries(SW_MENU, 0, 0, 8, -1);
  }
  pos = getRotariesValue(SW_MENU);

  u8g2.setFont(u8g2_font_courB10_tf);
  u8g2.clearBuffer();
  if( pos < 5 ) {
     u8g2.setCursor(10, 10);   u8g2.print("Tara");
     u8g2.setCursor(10, 23);   u8g2.print("Kalibrieren");
     u8g2.setCursor(10, 36);   u8g2.print("Korrektur");
     u8g2.setCursor(10, 49);   u8g2.print("Fuellmenge");
     u8g2.setCursor(10, 62);   u8g2.print("Autostart");
     u8g2.setFont(u8g2_font_open_iconic_arrow_2x_t);
     u8g2.drawGlyph(112, 64, 0x40);  
  } else {
     u8g2.setCursor(10, 10);   u8g2.print("Servo Max");
     u8g2.setCursor(10, 23);   u8g2.print("Servo Fein");
     u8g2.setCursor(10, 36);   u8g2.print("Autokorrektur");
     u8g2.setCursor(10, 49);   u8g2.print("Clear Pref's");
     u8g2.setFont(u8g2_font_open_iconic_arrow_2x_t);
     u8g2.drawGlyph(112, 16, 0x43);  
  }
  u8g2.setFont(u8g2_font_courB10_tf);
  u8g2.setCursor(0, 10 + (((pos)%5) * 13));
  u8g2.print("*");
  u8g2.sendBuffer();

  if ( digitalRead(SELECT_SW) == SELECT_PEGEL ) {
    // sollte verhindern, dass ein Tastendruck gleich einen Unterpunkt wählt
    delay(250);
    while( digitalRead(SELECT_SW) == SELECT_PEGEL ) {
    }
#ifdef isDebug 
    Serial.print("Setup Position: ");
    Serial.println(pos);
#endif

    int lastpos = pos;
    if (pos == 0)   setupTara();              // Tara 
    if (pos == 1)   setupCalibration();       // Kalibrieren 
    if (pos == 2)   setupKorrektur();         // Korrektur 
    if (pos == 3)   setupFuellmenge();        // Füllmenge 
    if (pos == 4)   setupAutostart();         // Autostart 
    if (pos == 5)   setupZahlwert(&winkel_max, winkel_fein, winkel_hard_max, "Servo Max" );  // Maximaler Öffnungswinkel
    if (pos == 6)   setupZahlwert(&winkel_fein, winkel_hard_min, winkel_max, "Servo Fein" ); // Minimaler Abfüllwinkel
    if (pos == 7)   setupAutokorrektur();     // Autokorrektur 
    setPreferences();

    if (pos == 8)   setupClearPrefs();        // EEPROM löschen
    initRotaries(SW_MENU, lastpos, 0, 8, -1); // Menu-Parameter könnten verstellt worden sein
  }
}

void processAutomatik(void)
{
  int zielgewicht;           // Glas + Korrektur
  long blinktime;
  static int autokorrektur_gr; 
  int erzwinge_servo_aktiv = 0;

  static int gewicht_vorher;    // Gewicht des vorher gefüllten Glases
  static long time_vorher;      // Messung der Durchlaufzeit
  static int kulanz_gr = KULANZ_AUTOKORREKTUR; // Feste Kulanzgröße
  static int sammler_num = 5;   // Anzahl identischer Messungen für Nachtropfen

  if ( modus != MODE_AUTOMATIK ) {
     modus = MODE_AUTOMATIK;
     winkel = winkel_min;          // Hahn schliessen
     servo_aktiv = 0;              // Servo-Betrieb aus
     servo.write(winkel);
     auto_aktiv = 0;               // automatische Füllung starten
     tara_glas = 0;
     rotary_select = SW_WINKEL;    // Einstellung für Winkel über Rotary
     initRotaries(SW_MENU, fmenge_index, 0, 4, 1);
     gewicht_vorher = glaeser[fmenge_index].Gewicht + korrektur;
     autokorrektur_gr = 0;
  }

  pos = getRotariesValue(SW_WINKEL);
  // nur bis winkel_fein regeln, oder über initRotaries lösen?
  if ( pos < ((winkel_fein*100)/winkel_max) ) {                      
    pos = ((winkel_fein*100)/winkel_max);
    setRotariesValue(SW_WINKEL, pos);
  }

#ifdef USE_ROTARY                                                    // TODO: kann das Poti hier überhaupt etwas ändern?
  korrektur    = getRotariesValue(SW_KORREKTUR);
  fmenge_index = getRotariesValue(SW_MENU);
#endif
  fmenge       = glaeser[fmenge_index].Gewicht;
  tara         = glaeser[fmenge_index].Tara;
  if ( tara <= 0 ) 
     auto_aktiv = 0;

  // wir starten nur, wenn das Tara für die Füllmenge gesetzt ist!
  // Ein erneuter Druck auf Start erzwingt die Aktivierung des Servo
  if (((digitalRead(button_start_pin)) == HIGH) && (tara > 0)) {
    if ( auto_aktiv == 1 ) {
      erzwinge_servo_aktiv = 1;
#ifdef isDebug
      Serial.println("erzwinge Servo aktiv");      
#endif
    }
    auto_aktiv    = 1;             // automatisches Füllen aktivieren
    rotary_select = SW_WINKEL;     // falls während der Parameter-Änderung auf Start gedrückt wurde    
    setPreferences();              // falls Parameter über den Rotary verändert wurden
  }
  
  if ((digitalRead(button_stop_pin)) == HIGH) {
    winkel      = winkel_min;
    servo_aktiv = 0;
    auto_aktiv  = 0;
    tara_glas   = 0;
    autokorrektur_gr = 0;  
  }

  // 
  gewicht = (int(SCALE_GETUNITS(SCALE_READS))) - tara;
  
  // Glas entfernt -> Servo schliessen
  if (gewicht < -20) {
    winkel      = winkel_min;
    servo_aktiv = 0;
    tara_glas   = 0;
    if ( autostart != 1 ) {  // Autostart nicht aktiv
      auto_aktiv  = 0;
    }
  }

  // Automatik ein, leeres Glas aufgesetzt, Servo aus -> Glas füllen
  if ((auto_aktiv == 1) && (gewicht <= 5) && (gewicht >= -5) && (servo_aktiv == 0)) {
    rotary_select = SW_WINKEL;     // falls während der Parameter-Änderung ein Glas aufgesetzt wird    
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_courB24_tf);
    u8g2.setCursor(15, 43);
    u8g2.print("START");
    u8g2.sendBuffer();
    // kurz warten und prüfen ob das Gewicht nicht nur eine zufällige Schwankung war 
    delay(1500);  
    gewicht = (int(SCALE_GETUNITS(SCALE_READS))) - tara;

    if ((gewicht <= 5) && (gewicht >= -5)) {
      tara_glas   = gewicht;
#ifdef isDebug 
      Serial.print("gewicht: ");            Serial.print(gewicht);
      Serial.print(" gewicht_vorher: ");    Serial.print(gewicht_vorher);
      Serial.print(" zielgewicht: ");       Serial.print(fmenge + korrektur + tara_glas + autokorrektur_gr);
      Serial.print(" Autokorrektur: ");     Serial.println(autokorrektur_gr);
#endif      
      servo_aktiv = 1;
      sammler_num = 0;
    }
  }
  zielgewicht = fmenge + korrektur + tara_glas + autokorrektur_gr;

  if ( autokorrektur == 1 )                               // anpassen des Autokorrektur-Werts
  {                                                       
    if ( (auto_aktiv == 1)                                // Automatik ist aktiviert
       && (servo_aktiv == 0 ) && (winkel == winkel_min)   // Hahn ist geschlossen
       && (gewicht >= zielgewicht )                       // Glas ist voll
       && (sammler_num <= 5)                              // tropfmenge noch nicht erfasst
       ) {                           
    if ( (gewicht == gewicht_vorher) && (sammler_num < 5) ) {   // wir wollen 5x das identische Gewicht sehen  
      sammler_num++;
    } else if ( gewicht != gewicht_vorher ) {             // sonst gewichtsänderung nachführen
      gewicht_vorher = gewicht;
      sammler_num = 0;
    } else if ( sammler_num == 5 ) {                      // gewicht ist 5x identisch, autokorrektur bestimmen
      autokorrektur_gr = (fmenge + kulanz_gr + tara_glas) - (gewicht - autokorrektur_gr);
      sammler_num++;                                      // Korrekturwert für diesen Durchlauf erreicht
    }
#ifdef isDebug
      Serial.print("Nachtropfen:");
      Serial.print(" gewicht: ");        Serial.print(gewicht);
      Serial.print(" gewicht_vorher: "); Serial.print(gewicht_vorher);
      Serial.print(" sammler_num: ");    Serial.print(sammler_num);
      Serial.print(" Korrektur: ");      Serial.println(autokorrektur_gr);
#endif
    }
  }

// Füll-Automatik ohne Autostart ist aktiviert, Glas ist teilweise gefüllt
// Füllvorgang fortsetzen
//  if ((auto_aktiv == 1) && (gewicht >= 0) && (autostart != 1)) {

  // Glas ist teilweise gefüllt. Start wird über Start-Taster erzwungen
  if ((auto_aktiv == 1) && (gewicht > 5) && (erzwinge_servo_aktiv == 1) ) {
    servo_aktiv = 1;
  }
  
  if (servo_aktiv == 1) {
    winkel = ((winkel_max * pos) / 100);
  }
  
  if ((servo_aktiv == 1) && (( zielgewicht - gewicht ) <= fein_dosier_gewicht)) {
    winkel = ( ((winkel_max * pos) / 100)
        * ( (zielgewicht - gewicht ) / fein_dosier_gewicht) );
  }
  
  if ((servo_aktiv == 1) && (winkel <= winkel_fein)) {
    winkel = winkel_fein;
  }
  
  // Glas ist voll
//  if ((servo_aktiv == 1) && ((gewicht - korrektur - tara_glas) >= fmenge)) {
  if ((servo_aktiv == 1) && (gewicht >= zielgewicht)) {
    winkel      = winkel_min;
    servo_aktiv = 0;
    if ( autostart != 1 )       // autostart ist nicht aktiv, kein weiterer Start
      auto_aktiv = 0;
    if ( autokorrektur == 1 )   // autokorrektur, gewicht merken
      gewicht_vorher = gewicht;
  }
  
  servo.write(winkel);
  
#ifdef isDebug
#if isDebug >= 4
    Serial.print("Automatik:");  
    Serial.print(" Gewicht: ");        Serial.print(gewicht);
    Serial.print(" Winkel: ");         Serial.print(winkel);
//    Serial.print(" Dauer ");           Serial.print(millis() - scaletime);
    Serial.print(" Füllmenge: ");      Serial.print(fmenge);
    Serial.print(" Korrektur: ");      Serial.print(korrektur);
    Serial.print(" Tara_glas:");       Serial.print(tara_glas);
    Serial.print(" Autokorrektur: ");  Serial.print(autokorrektur_gr);
    Serial.print(" Zielgewicht ");     Serial.print(zielgewicht);
    Serial.print(" Erzwinge Servo: "); Serial.print(erzwinge_servo_aktiv);
    Serial.print(" servo_aktiv ");     Serial.println(servo_aktiv);
#endif 
#endif
  time_vorher = millis();

  u8g2.clearBuffer();

  // Gewicht blinkt, falls unter der definierten Füllmenge
  // Korrekturfaktor und Füllmenge blinken, wenn sie über den Rotary verstellt werden
  blinktime = (millis()/10) % 5;

  // wenn kein Tara für unser Glas definiert ist, wird kein Gewicht sondern eine Warnung ausgegeben
  if ( tara > 0 ) {
     u8g2.setCursor(10, 42);
     u8g2.setFont(u8g2_font_courB24_tf);
   
     if( (autostart == 1) && (auto_aktiv == 1 ) && (servo_aktiv == 0) && (gewicht >= -5) && (gewicht - tara_glas < fmenge) && (blinktime < 2) ) {
       sprintf(ausgabe,"%5s", "     ");
     } else {
       sprintf(ausgabe,"%5dg", gewicht - tara_glas);
     }
  } else {
     u8g2.setCursor(42, 38);
     u8g2.setFont(u8g2_font_courB14_tf);
     sprintf(ausgabe,"%6s", "no tara!");
  }
  u8g2.print(ausgabe);

  // Play/Pause Icon, ob die Automatik aktiv ist
  u8g2.setFont(u8g2_font_open_iconic_play_2x_t);
  u8g2.drawGlyph(0, 40, (auto_aktiv==1)?0x45:0x44 );

  u8g2.setFont(u8g2_font_courB12_tf);
  // Zeile oben, Öffnungswinkel absolut und Prozent, Anzeige Autostart
  u8g2.setCursor(0, 11);
  sprintf(ausgabe,"W=%-3d %2s %3d%%", winkel, (autostart==1)?"AS":"  ", pos);
  u8g2.print(ausgabe);

  // Zeile unten, aktuell zu verstellende Werte blinken. 
  // Verstellung nur wenn Automatik inaktiv, gesteuert über Interrupt-Funktion 
  if( autokorrektur == 1 ){
    u8g2.setCursor( 0, 64);
    u8g2.print("a");
    u8g2.setCursor(10, 64);
  } else {
    u8g2.setCursor( 0, 64);    
  }
  if( rotary_select == SW_KORREKTUR && blinktime < 2 ) {
    sprintf(ausgabe,"k=   %s f=%4d", (autokorrektur==1)?"":" ", glaeser[fmenge_index].Gewicht );
  } else if ( rotary_select == SW_MENU && blinktime < 2 ) {
    sprintf(ausgabe,"k=%-3d%s f=" , korrektur + autokorrektur_gr, (autokorrektur==1)?"":" " );
  } else {
    sprintf(ausgabe,"k=%-3d%s f=%4d", korrektur + autokorrektur_gr, (autokorrektur==1)?"":" ", glaeser[fmenge_index].Gewicht );
  }
  u8g2.print(ausgabe);

  u8g2.sendBuffer();
}

void processHandbetrieb(void)
{
  static unsigned long scaletime;
  static unsigned long dauer;
  
  if ( modus != MODE_HANDBETRIEB ) {
     modus = MODE_HANDBETRIEB;
     winkel = winkel_min;          // Hahn schliessen
     servo_aktiv = 0;              // Servo-Betrieb aus
     servo.write(winkel);
     rotary_select = SW_WINKEL;
     tara = 0;
  }

  pos = getRotariesValue(SW_WINKEL);
  gewicht = SCALE_GETUNITS(SCALE_READS) - tara;

  if ((digitalRead(button_start_pin)) == HIGH) {
    servo_aktiv = 1;
  }
  
  if ((digitalRead(button_stop_pin)) == HIGH) {
    servo_aktiv = 0;
  }

#ifdef USE_ROTARY_SW
  if ( ((digitalRead(outputSW)) == LOW) /*&& (tara == 0) */ ) {  // sonst muss der Taster entprellt werden!
      tara = SCALE_GETUNITS(SCALE_READS);
  }
#endif

  if (servo_aktiv == 1) {
    winkel = ((winkel_max * pos) / 100);
  } else { 
    winkel = winkel_min;
  }
  servo.write(winkel);

#ifdef isDebug
#if isDebug >= 4
    Serial.print("Handbetrieb:");  
    Serial.print(" Gewicht ");     Serial.print(gewicht);
    Serial.print(" Winkel ");      Serial.print(winkel);
    Serial.print(" Dauer ");       Serial.print(millis() - scaletime);
    Serial.print(" servo_aktiv "); Serial.println(servo_aktiv);
#endif
#endif
  scaletime = millis();

  // Ausgabe OLED. Dauert ca. 170ms
  u8g2.clearBuffer();

  u8g2.setFont(u8g2_font_courB24_tf);
  u8g2.setCursor(10, 42);
  sprintf(ausgabe,"%5dg", gewicht);
  u8g2.print(ausgabe);

  u8g2.setFont(u8g2_font_open_iconic_play_2x_t);
  u8g2.drawGlyph(0, 40, (servo_aktiv==1)?0x45:0x44 );

  u8g2.setFont(u8g2_font_courB12_tf);
  u8g2.setCursor(0, 11);
  sprintf(ausgabe,"W=%-3d    %3d%%", winkel, pos);
  u8g2.print(ausgabe);
  u8g2.setCursor(0, 64);
  sprintf(ausgabe, "Manuell  %s", (tara>0?"Tara":"    "));
  u8g2.print(ausgabe);

  u8g2.sendBuffer();
}

void setup()
{
  // enable internal pull downs for digital inputs 
  pinMode(button_start_pin, INPUT_PULLDOWN);
  pinMode(button_stop_pin, INPUT_PULLDOWN);
  pinMode(switch_betrieb_pin, INPUT_PULLDOWN);
  pinMode(switch_setup_pin, INPUT_PULLDOWN);
#if HARDWARE_LEVEL == 2
  pinMode(vext_ctrl_pin, INPUT_PULLDOWN);
#endif
  pinMode(LED_BUILTIN, OUTPUT);

  Serial.begin(115200);
  while (!Serial) {
  }
#ifdef isDebug
    Serial.println("Hanimandl Start");
#endif
  
  // Rotary
#ifdef USE_ROTARY_SW
  pinMode(outputSW, INPUT_PULLUP);
  attachInterrupt(outputSW, isr1, FALLING);
#endif
#ifdef USE_ROTARY
  pinMode(outputA,INPUT);
  pinMode(outputB,INPUT);
  attachInterrupt(outputA, isr2, CHANGE);
#endif

// switch Vcc / GND on normal pins for convenient wiring
// output is 3.3V for VCC
  digitalWrite (switch_vcc_pin, HIGH); 
  digitalWrite (button_start_vcc_pin, HIGH); 
  digitalWrite (button_stop_vcc_pin, HIGH); 
  
//  pinMode (_GND, OUTPUT);     // turn on GND pin first (important!)
// turn on VCC power
  pinMode (switch_vcc_pin, OUTPUT);
  pinMode (button_start_vcc_pin, OUTPUT);
  pinMode (button_stop_vcc_pin, OUTPUT);
  // short delay to let chip power up
  delay (100); 

#ifdef USE_ORIGINAL_SERVO_VARS
  servo.attach(servo_pin, 750, 2500);  // originale Initialisierung, steuert nicht jeden Servo an
#else
  servo.attach(servo_pin);             // default Werte. Achtung, steuert den Nullpunkt weniger weit aus!  
#endif
  servo.write(winkel_min);

// Boot Screen
  u8g2.begin();
  u8g2.clearBuffer();
//  u8g2.setFont(u8g2_font_courB24_tf);
//  u8g2.setCursor(20, 43);    u8g2.print("BOOT");
  print_logo();
  delay(3000);

// Preferences aus dem EEPROM lesen
  getPreferences();

// Waage erkennen
  scale.begin(hx711_dt_pin, hx711_sck_pin);
  if (scale.wait_ready_timeout(1000)) {               // Waage angeschlossen?
    scale.power_up();
    waage_vorhanden = 1;

    if ( faktor == 0 ) {                              // Vorhanden aber nicht kalibriert
      u8g2.clearBuffer();
      u8g2.setFont(u8g2_font_courB18_tf);
      u8g2.setCursor( 24, 24); u8g2.print("Nicht");
      u8g2.setCursor( 10, 56); u8g2.print("kalibr.");
      u8g2.sendBuffer();
#ifdef isDebug
      Serial.println("Waage nicht kalibriert!");
#endif
      delay(2000);
    } else {                                          // Tara und Skalierung setzen
      scale.set_scale(faktor);
      scale.set_offset(long(gewicht_leer));
#ifdef isDebug
      Serial.println("Waage initialisiert");
#endif
    }
  } else {                                            // Keine Waage angeschlossen
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_courB24_tf);
    u8g2.setCursor( 14, 24); u8g2.print("Keine");
    u8g2.setCursor( 6, 56);  u8g2.print("Waage!");
    u8g2.sendBuffer();
#ifdef isDebug
    Serial.println("Keine Waage!");
#endif
    delay(2000);
  }
  
// initiale Kalibrierung des Leergewichts wegen Temperaturschwankungen
// Falls mehr als 20g Abweichung steht vermutlich etwas auf der Waage.
  if (waage_vorhanden == 1) {
    gewicht = SCALE_GETUNITS(SCALE_READS);
    if ( (gewicht > -20) && (gewicht < 20) ) {
      scale.tare(SCALE_READS);
#ifdef isDebug
      Serial.print("Tara angepasst um: ");
      Serial.println(gewicht);
#endif
    } else if (faktor != 0) {
      u8g2.clearBuffer();
      u8g2.setFont(u8g2_font_courB18_tf);
      u8g2.setCursor( 24, 24); u8g2.print("Waage");
      u8g2.setCursor( 10, 56); u8g2.print("leeren!");
      u8g2.sendBuffer();
      delay(5000);
    }
  }
  
// die drei Datenstrukturen des Rotaries initialisieren
  initRotaries(SW_WINKEL,    0,   0, 100, 5 );     // Winkel
  initRotaries(SW_KORREKTUR, 0, -20,  20, 1 );     // Korrektur
  initRotaries(SW_MENU,      0,   0,   7, 1 );     // Menuauswahlen

// Parameter aus den Preferences für den Rotary Encoder setzen
  setRotariesValue(SW_WINKEL,    pos);   
  setRotariesValue(SW_KORREKTUR, korrektur);
  setRotariesValue(SW_MENU,      fmenge_index);
}

void loop()
{
  rotating = true;     // debounce Management
  
  // Setup Menu 
  if ((digitalRead(switch_setup_pin)) == HIGH)
    processSetup();

  // Automatik-Betrieb 
  if ((digitalRead(switch_betrieb_pin)) == HIGH)
    processAutomatik();

  // Handbetrieb 
  if ((digitalRead(switch_betrieb_pin) == LOW)
      && (digitalRead(switch_setup_pin) == LOW))
    processHandbetrieb();
}

void print_logo() {
  const unsigned char logo_biene1[] = {
  0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x70, 0x00, 0xC0, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xF0, 
  0x00, 0xC0, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xE0, 0x01, 0xE0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xC0, 0x01, 0x60, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0x01, 0x60, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0xC1, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0xF8, 0x0F, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFC, 0x1F, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFE, 0x3F, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1F, 0x7C, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0F, 0x70, 0x00, 0xF0, 0xFF, 0x01, 
  0x00, 0x00, 0x00, 0x00, 0x03, 0xE0, 0x80, 0xFF, 0xFF, 0x0F, 0x00, 0xFF, 0xFF, 0x80, 0xF1, 0x47, 0xF0, 0x07, 0x00, 0x3E, 0xE0, 0xFF, 0xFF, 0x07, 
  0xF9, 0x07, 0x7E, 0x00, 0x00, 0x78, 0xF0, 0x03, 0xE0, 0x1F, 0xF8, 0x07, 0x1F, 0x00, 0x00, 0x70, 0x3C, 0x00, 0x00, 0xFE, 0x38, 0xC0, 0x03, 0x00, 
  0x00, 0xF0, 0x0E, 0x00, 0x00, 0xF8, 0x03, 0xF8, 0x00, 0x00, 0x00, 0xE0, 0x06, 0x00, 0x00, 0xC0, 0x0F, 0x7C, 0x00, 0x00, 0x00, 0xE0, 0x06, 0x00, 
  0x00, 0x00, 0x1F, 0x1F, 0x00, 0x00, 0x00, 0x70, 0x03, 0x00, 0x00, 0x00, 0xFC, 0x07, 0x00, 0x00, 0x00, 0x70, 0x03, 0x00, 0x00, 0x00, 0xF0, 0x03, 
  0x00, 0x00, 0x00, 0x38, 0x03, 0x00, 0x00, 0x00, 0xE0, 0x01, 0x00, 0x00, 0x00, 0x1C, 0x07, 0x00, 0x00, 0x00, 0xF0, 0xF0, 0x07, 0x00, 0x00, 0x0F, 
  0x0F, 0x00, 0x00, 0x78, 0x78, 0xE0, 0x3F, 0x00, 0xC0, 0x07, 0x3E, 0x00, 0x80, 0xFF, 0x3C, 0xC0, 0x7F, 0x00, 0xF0, 0x01, 0xFC, 0x00, 0xE0, 0xFF, 
  0x1C, 0x80, 0xFF, 0x01, 0x7E, 0x00, 0xF0, 0xFF, 0xFF, 0x3F, 0x0E, 0x00, 0xFE, 0xFF, 0x0F, 0x00, 0xC0, 0xFF, 0xFF, 0x07, 0x0F, 0x00, 0xC0, 0x1F, 
  0x00, 0x00, 0x00, 0xFC, 0x3F, 0x00, 0x07, 0x00, 0x00, 0x3C, 0x00, 0x00, 0x00, 0x80, 0x03, 0x80, 0x03, 0xE0, 0x00, 0x70, 0x00, 0x00, 0x00, 0xC0, 
  0x01, 0xC0, 0x03, 0xC0, 0x01, 0xE0, 0x00, 0x00, 0x00, 0xE0, 0x00, 0xE0, 0x81, 0xC3, 0x01, 0xC0, 0x01, 0x00, 0x00, 0x70, 0x00, 0xE0, 0xF1, 0x8F, 
  0x03, 0x80, 0x03, 0x00, 0x00, 0x38, 0x00, 0xF0, 0xFC, 0x9F, 0x07, 0x00, 0x07, 0x00, 0x00, 0x1C, 0x00, 0xF8, 0x1C, 0x1C, 0x0F, 0x00, 0x06, 0x00, 
  0x00, 0x1C, 0x00, 0xFE, 0x00, 0x00, 0x1F, 0x00, 0x0C, 0x00, 0x00, 0x0E, 0x00, 0xF7, 0x00, 0x00, 0x7F, 0x00, 0x0C, 0x00, 0x00, 0x06, 0x80, 0x73, 
  0x00, 0x00, 0xE6, 0x00, 0x0C, 0x00, 0x00, 0x07, 0xE0, 0x71, 0x00, 0x00, 0xC6, 0x03, 0x0C, 0x00, 0x00, 0x07, 0x70, 0x70, 0xF0, 0x0F, 0x86, 0x07, 
  0x0C, 0x00, 0x00, 0x03, 0x3C, 0x70, 0xFC, 0x3F, 0x06, 0x1F, 0x0E, 0x00, 0x00, 0x03, 0x1E, 0x70, 0xFE, 0x3F, 0x06, 0xFC, 0x07, 0x00, 0x00, 0x87, 
  0x0F, 0x70, 0x1E, 0x38, 0x06, 0xF0, 0x03, 0x00, 0x00, 0xFE, 0x03, 0xF0, 0x00, 0x00, 0x06, 0xC0, 0x00, 0x00, 0x00, 0xFC, 0x00, 0xF0, 0x00, 0x00, 
  0x07, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xE0, 0x80, 0x03, 0x07, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xE0, 0xE0, 0x0F, 0x07, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0xE0, 0xF1, 0x9F, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xC0, 0x3B, 0x9C, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xC0, 
  0x07, 0xC0, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0x07, 0xE0, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0F, 0xE0, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1F, 0x70, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x3E, 0x78, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x7C, 0x38, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xF8, 0x1C, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xF0, 0x0D, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xE0, 0x0F, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0x07, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x07, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
  };

  u8g2.clearBuffer();
  u8g2.drawXBM(0,0,80,64,logo_biene1);
  u8g2.setFont(u8g2_font_courB14_tf);
  u8g2.setCursor(85, 27);    u8g2.print("HANI");
  u8g2.setCursor(75, 43);    u8g2.print("MANDL");
  u8g2.setFont(u8g2_font_courB08_tf);
  u8g2.setCursor(85, 64);    u8g2.print("v.0.2.3");
  u8g2.sendBuffer();
}