// CONNECTIONS:
// DS1302 CLK/SCLK --> 5
// DS1302 DAT/IO --> 4
// DS1302 RST/CE --> 2
// DS1302 VCC --> 3.3v - 5v
// DS1302 GND --> GND

#include <RtcDS1302.h>

#include <MD_MAX72xx.h>    // include MajicDesigns MAX72xx LED matrix library
#include <SPI.h>           // include Arduino SPI library
#include "clockFont.h"

//----termo-------------------
#include <OneWire.h>
#include <DallasTemperature.h>

#define ONE_WIRE_BUS 9

#define TERM_LONG_TIMEOUT 5000
#define TERM_LOOP_DELAY 15000
#define TERM_SHORT_TIMEOUT 2000
#define BUTTON_LONG_PRESS_TIMEOUT 1000

#define SHOW_TERM_BUTTON 6
#define SET_HOUR_BUTTON 7
#define SET_MINUTE_BUTTON 8

#define UPDATE_SEC  1000
#define UPDATE_FAST  150

int currentScreenUpdateTime = UPDATE_SEC;
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);
//------------------------------------

#define HARDWARE_TYPE MD_MAX72XX::FC16_HW   // this line defines our dot matrix hardware type
#define MAX_DEVICES 4                       // define number of total cascaded modules

#define CS_PIN    10   // define CS (Chip Select) pin

ThreeWire myWire(4,5,2); // IO, SCLK, CE
RtcDS1302<ThreeWire> Rtc(myWire);

//MD_Parola display = MD_Parola(HARDWARE_TYPE, CS_PIN, MAX_DEVICES);
MD_MAX72XX display = MD_MAX72XX(HARDWARE_TYPE, CS_PIN, MAX_DEVICES);

bool tick = false;
unsigned long  screenUpdateTimer;
unsigned long termoButtonPressTimeStamp;
double termoLoopTimeStamp = 0;
unsigned long settingButtonPressTimeStamp;

bool term_shown = false;
int brightnessValue = 0;

bool set_hour_mode = false;
bool set_minute_mode = false;

int digi_blink_counter = 0;

void setup(void) 
{
    
    sensors.begin();
    // initialize the dot matrix display
    display.begin();
    display.setFont(mfont);
     // set the intensity (brightness) of the display (choose a number between 0 and 15)
    brightnessValue = map(analogRead(A0), 0, 950, 0, 15);
    
//    display.setIntensity(brightnessValue);
    display.control(MD_MAX72XX::INTENSITY, brightnessValue);
    // clear the whole display
    display.clear();
    pinMode(SHOW_TERM_BUTTON, INPUT_PULLUP);
    pinMode(SET_HOUR_BUTTON, INPUT_PULLUP);
    pinMode(SET_MINUTE_BUTTON, INPUT_PULLUP);
    screenUpdateTimer = millis();
    termoLoopTimeStamp = screenUpdateTimer + TERM_LOOP_DELAY;
    Rtc.Begin();
    
}

//---------------------RENDER--------------------------
uint16_t fontOffset(char c)
{
  if (c < 32 || c > 58)
    c = 32;  // fallback -> space

  uint8_t index = c - 32;
  uint16_t offset = 0;

  for (uint8_t i = 0; i < index; i++)
  {
    uint8_t w = pgm_read_byte(&mfont[offset]);
    offset += w + 1;
  }

  return offset;
}

uint8_t drawChar(uint8_t col, char c)
{
  uint16_t off = fontOffset(c);
  uint8_t w = pgm_read_byte(&mfont[off]);

  for (uint8_t i = 0; i < w; i++)
  {
    uint8_t colData = pgm_read_byte(&mfont[off + 1 + i]);
    uint8_t physCol = (MAX_DEVICES * 8 - 1) - (col + i);
    display.setColumn(physCol, colData);
  }

  uint8_t physCol = (MAX_DEVICES * 8 - 1) - (col + w);
  display.setColumn(physCol, 0);
  return w + 1;
}

void drawText(const char* text, uint8_t startCol = 0)
{
  uint8_t col = startCol;


  while (*text && col < MAX_DEVICES * 8)
  {
    col += drawChar(col, *text++);
  }

  while (col < MAX_DEVICES * 8)
  {
    uint8_t physCol = (MAX_DEVICES * 8 - 1) - col;
    display.setColumn(physCol, 0);
    col++;
  }
}

//-------------------------------------------------------------

void inc_time(int min_change = 0, int hour_change = 0)
{
    RtcDateTime now = Rtc.GetDateTime();
    int new_min = now.Minute() + min_change;
    int new_hours = now.Hour() + hour_change;
    
    if (new_min >= 60)
    {
      new_min = (new_min + 60) % 60;
      new_hours = (new_hours + 24) % 24;
    }
        
    if (new_hours >= 24)
      new_hours = 0;
        
    Rtc.SetDateTime(RtcDateTime(
      now.Year(), 
      now.Month(), 
      now.Day(), 
      new_hours, 
      new_min, 
      00));
}

void updateTime(bool no_blink = false)
{
    if (term_shown)
    {
        term_shown = false; 
        termoLoopTimeStamp = millis();
    }
    RtcDateTime now = Rtc.GetDateTime();
    char buffer[6];
    sprintf(buffer, "%02d:%02d", now.Hour(), now.Minute());
    if (!no_blink)
    {
        if (set_hour_mode)
        {
            digi_blink_counter++;
            if (digi_blink_counter % 2 == 0)
            sprintf(buffer, "//:%02d", now.Minute());
        }
        else if (set_minute_mode)
        {
            digi_blink_counter++;
            if (digi_blink_counter % 2 == 0)
            sprintf(buffer, "%02d://", now.Hour());
        }
        else if (tick)
            sprintf(buffer, "%02d %02d", now.Hour(), now.Minute());
    }
    String printChar = String(buffer);
    drawText(buffer);
    tick = !tick;
    
}

void updateTemp(void)
{
    sensors.requestTemperatures();
    char result[6]; 
    dtostrf(sensors.getTempCByIndex(0), 2, 1, result); 
    strcat(result, "!"); 
    drawText(result);
    term_shown = true;
}

void loop(void) 
{ 
      
    double currentMillis = millis();
    if ((set_hour_mode || set_minute_mode) 
          && currentMillis - settingButtonPressTimeStamp > TERM_LOOP_DELAY)
    {
        set_hour_mode = false;
        set_minute_mode = false;
    }
    bool setButtonJustPressed = false;
    if (!digitalRead(SET_HOUR_BUTTON))
    {
        delay(10);
        int long_press_counter = 0;
        while(!digitalRead(SET_HOUR_BUTTON))
        {
            delay(1);
            long_press_counter++;
            if (long_press_counter > BUTTON_LONG_PRESS_TIMEOUT)
            {
                inc_time(0,1);
                updateTime(true);
                delay(150);
            }
        }
        if (set_hour_mode)
            inc_time(0,1);
        set_hour_mode = true;
        set_minute_mode = false;
        setButtonJustPressed = true;
    }
    if (!digitalRead(SET_MINUTE_BUTTON))
    {
        delay(10);
        int long_press_counter = 0;
        while(!digitalRead(SET_MINUTE_BUTTON))
        {
            delay(1);
            long_press_counter++;
            if (long_press_counter > BUTTON_LONG_PRESS_TIMEOUT)
            {
                inc_time(1,0);
                updateTime(true);
                delay(150);
            }
        }
        if (set_minute_mode)
            inc_time(1,0); 
        set_minute_mode = true;
        set_hour_mode = false;
        setButtonJustPressed = true;
        
    }
    if (setButtonJustPressed)
    {
        digi_blink_counter = 0;
        termoLoopTimeStamp = millis();
        settingButtonPressTimeStamp = millis();
        
        termoLoopTimeStamp = millis();
    }

    if (set_hour_mode || set_minute_mode)
        currentScreenUpdateTime = UPDATE_FAST;
    else
        currentScreenUpdateTime = UPDATE_SEC;

    int currentBrightnessValue = map(analogRead(A0), 0, 950, 0, 15);
    if (abs(currentBrightnessValue - brightnessValue) > 5)
    {
        brightnessValue = currentBrightnessValue;
        display.control(MD_MAX72XX::INTENSITY, brightnessValue);
    }
    if (!digitalRead(SHOW_TERM_BUTTON))
    {
        char buffer[6];
        sprintf(buffer, "&");
        drawText(buffer, 0);
        
        delay(2);

        while(!digitalRead(SHOW_TERM_BUTTON))
            delay(1);
        termoButtonPressTimeStamp = millis();
        set_hour_mode = false;
        set_minute_mode = false;
    }
    if (currentMillis - screenUpdateTimer > currentScreenUpdateTime)
    {
        bool show_term = false;
        if (!(set_hour_mode || set_minute_mode))
        {
            if (currentMillis - termoButtonPressTimeStamp < TERM_LONG_TIMEOUT)
                show_term = true;
            double last_term_delta = currentMillis - termoLoopTimeStamp;
            if (last_term_delta > TERM_LOOP_DELAY 
                  && last_term_delta < (TERM_LOOP_DELAY + TERM_SHORT_TIMEOUT))
                
                show_term = true;
        }
        if (show_term)
            updateTemp();
        else 
            updateTime(false);
        screenUpdateTimer = currentMillis;
   }
   delay(10);
}
