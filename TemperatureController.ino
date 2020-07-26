#include <LiquidCrystal.h>
#include <Adafruit_MAX31855.h>
#include <EEPROM.h>
#include <SPI.h>

// EEPROM
int addrInitialized = 0;			// EEPROM address for storing the initialized parameter
int addrMaxTemp = 1;				// EEPROM address for storing the maxTemp value		
int addrMinTemp = 2;				// EEPROM address for storing the minTemp value
int addrFanSpeed = 3;				// EEPROM address for storing the fanSpeed value
int addrFanOnOffDelay = 4;			// EEPROM address for storing the fan on/off delay
const byte eepromInitialized = 1;	// constant for determining if the the EEPROM has been initialized

// LCD
LiquidCrystal lcd(8, 9, 4, 5, 6, 7);

// Pin 3 PWM at 25 kHz
#define TOP_OVERFLOW_VALUE 79	// Top overflow value

// panel and buttons
#define btnRIGHT  0		// Value of button RIGHT
#define btnUP     1		// Value of button UP
#define btnDOWN   2		// Value of button DOWN
#define btnLEFT   3		// Value of button lEFT
#define btnSELECT 4		// Value of button SELECT (Save)
#define btnNONE   5		// Value when no button was clicked

// pins
#define FanOnOffPin 13	// Fan pin
#define	HighLedPin 12	// LED pin for HIGH temp
#define	pwmPin 3		// PWM pin for controling the fan speed
#define MAXDO   0		// DO pin of Adafruit_MAX31855
#define MAXCS   2		// CS pin of Adafruit_MAX31855
#define MAXCLK  11		// CLK pin of Adafruit_MAX31855

// menu options
#define MAIN_MENU 0			// Main menu or overview
#define SET_MAX_MENU 1		// adjust maximum temperature
#define SET_MIN_MENU 2		// adjust minimum temperature
#define SET_FAN_MENU 3		// adjust fan speed
#define SET_DELAY_MENU 4	// fan on/off delay menu

// temperature status
#define IN_RANGE 0
#define TOO_HIGH 1
#define TOO_LOW 2

// Thermocouple
Adafruit_MAX31855 thermocouple(MAXCLK, MAXCS, MAXDO);

// Temperature, Fan, Menu and LED variables
bool fanIsOn = 0;						// 0 == off and 1 == on
byte maxTemp = 30;						// the maximum temperature
byte minTemp = 20;						// the minimum temperature
byte highTempLedPinState = LOW;			// for storing the high temp LED state (on/off)	
byte selectedMenuOption = MAIN_MENU;	// the currrent selected menu
byte fanDisplaySpeed = 50;				// fan speed value for the display (0-50)
byte fanDelay = 60;						// Fan on/off delay in seconds
double lastKnownTemp = 0;				// for storing the last known temperatur
int currentTemp;						// for storing the current temperature
byte pwmValue = 79;						// value 0-79 adjust fan duty cycle
unsigned long lastTempReadTime;			// for storing the time that the temperature was read
unsigned long lastBlinkTime;			// for storing the time that the LED state (on/off) changed
unsigned long lastSaveBtnClickTime;		// for storing the time that the save (SELECT) button was clicked
unsigned long lastFanOnOffTime;			// for storing the time that the fan was turned on or off

// intervals and other constants
const unsigned long tempReadInterval = 5000;	// 5 seconds
const unsigned long blinkInterval = 500;		// 0.5 seconds
const unsigned long saveTimeInterval = 5000;	// 5 seconds
const unsigned int pwmMinValue = 0;				// minimum value for pwmValue, must be between 0-79
const byte fanDisplaySpeedMax = 38;				// Max value for fan speed (max = 79)


/// <summary>
/// SETUP
/// </summary>
void setup()
{
	// Initialize the EEPROM if not initialized
	if (EEPROM.read(addrInitialized) != eepromInitialized)
	{
		EEPROM.update(addrInitialized, eepromInitialized);
		SaveSettings();
	}

	// Read min en max from the EEPROM
	maxTemp = EEPROM.read(addrMaxTemp);
	minTemp = EEPROM.read(addrMinTemp);
	fanDisplaySpeed = EEPROM.read(addrFanSpeed);
	fanDelay = EEPROM.read(addrFanOnOffDelay);
	// check if the stored fan display speed value is too large and correct if necessary
	if (fanDisplaySpeed > fanDisplaySpeedMax)
	{
		fanDisplaySpeed = fanDisplaySpeedMax;
		SaveSettings();
	}

	// Set pins
	pinMode(FanOnOffPin, OUTPUT);
	pinMode(HighLedPin, OUTPUT);
	pinMode(pwmPin, OUTPUT);

	// Initialize pin 3 PWM at 25kHz
	pwm25kHzBegin();

	// Initialize LCD
	lcd.begin(16, 2);

	// Wait for MAX chip to stabilize
	lcd.setCursor(0, 1);
	lcd.print("Initializing");
	lcd.setCursor(0, 0);
	lcd.print("sensor");
	delay(1000);

	// Initialize thermocouple
	if (!thermocouple.begin())
	{
		lcd.clear();
		lcd.setCursor(0, 0);
		lcd.print("SENSOR ERROR");
		while (1) delay(10);
	}

	// read temp and start show the menu
	ReadTemperature();
	lastKnownTemp = currentTemp;
	PrintCorrectMenu();
}

/// <summary>
/// MAIN LOOP
/// </summary>
void loop()
{
	switch (readButtons())
	{
	case btnRIGHT:	// goto to next menu
	{
		if (selectedMenuOption == SET_DELAY_MENU)
			selectedMenuOption = MAIN_MENU;
		else
			selectedMenuOption++;
		PrintCorrectMenu();
		delay(200);
		break;
	}
	case btnLEFT:	// goto to previous menu
	{
		if (selectedMenuOption == MAIN_MENU)
			selectedMenuOption = SET_DELAY_MENU;
		else
			selectedMenuOption--;
		PrintCorrectMenu();
		delay(200);
		break;
	}
	case btnUP:		// change selected parameter up
	{
		UpdateSetting(btnUP);
		PrintCorrectMenu();
		delay(200);
		break;
	}
	case btnDOWN:	// change selected parameter down
	{
		UpdateSetting(btnDOWN);
		PrintCorrectMenu();
		delay(200);
		break;
	}
	case btnSELECT:	// Save button
	{
		delay(200);
		PrintSaveSettingMenu();
		lastSaveBtnClickTime = millis();
		while (millis() - lastSaveBtnClickTime < saveTimeInterval)
		{
			if (readButtons() == btnSELECT)
			{
				SaveSettings();
				break;
			}
		}
		PrintCorrectMenu();
		delay(200);
		break;
	}
	}

	// read temperature once each 5000 milliseconds
	if (millis() - lastTempReadTime >= tempReadInterval)
	{
		ReadTemperature();
		if (currentTemp != lastKnownTemp)
		{
			lastKnownTemp = currentTemp;
			if (selectedMenuOption == MAIN_MENU)
				UpdateMainMenuCurrentTemp();
		}
	}

	switch (GetTemperatureStatus())
	{
	case TOO_HIGH:
		BlinkHighTempLed();
		break;
	case TOO_LOW:
		if (!fanIsOn && (millis() - lastFanOnOffTime >= (fanDelay * 1000)))
			TurnFanOn();
		StopBlinking();
		break;
	case IN_RANGE:
		if (fanIsOn && (millis() - lastFanOnOffTime >= (fanDelay * 1000)))
		{
			TurnFanOff();			
		}
		StopBlinking();
		break;
	}
}

byte GetTemperatureStatus()
{
	if (currentTemp > maxTemp)  return TOO_HIGH;
	if (currentTemp < minTemp)  return TOO_LOW;
	return IN_RANGE;
}

/// <summary>
/// Saves the minTemp, maxTemp and fanSpeed settings to the EEPROM
/// </summary>
void SaveSettings()
{
	EEPROM.update(addrMaxTemp, maxTemp);
	EEPROM.update(addrMinTemp, minTemp);
	EEPROM.update(addrFanSpeed, fanDisplaySpeed);
	EEPROM.update(addrFanOnOffDelay, fanDelay);
}

/// <summary>
/// Updates the values of the minTemp, maxTemp or fanSpeed setting
/// </summary>
/// <param name="btnPressed"></param>
void UpdateSetting(int btnPressed)
{
	switch (selectedMenuOption)
	{
	case SET_MAX_MENU:
	{
		if (btnPressed == btnDOWN)
			maxTemp--;
		if (btnPressed == btnUP)
			maxTemp++;
		break;
	}
	case SET_MIN_MENU:
	{
		if (btnPressed == btnDOWN)
			minTemp--;
		if (btnPressed == btnUP)
			minTemp++;
		break;
	}
	case SET_FAN_MENU:
	{
		if (btnPressed == btnDOWN)
		{
			if (fanDisplaySpeed == 0)
				fanDisplaySpeed = fanDisplaySpeedMax;
			else
				fanDisplaySpeed--;
		}
		if (btnPressed == btnUP)
		{
			if (fanDisplaySpeed == fanDisplaySpeedMax)
				fanDisplaySpeed = 0;
			else
				fanDisplaySpeed++;
		}
		SetFanSpeed();
		break;
	}
	case SET_DELAY_MENU:
	{
		if (btnPressed == btnDOWN)
			fanDelay--;
		if (btnPressed == btnUP)
			fanDelay++;
		break;
	}
	}
}

/// <summary>
/// Turns off all LEDs
/// </summary>
void StopBlinking()
{
	digitalWrite(HighLedPin, LOW);
	highTempLedPinState = LOW;
}

/// <summary>
/// Blinks the High Temperature LED on the set interval
/// </summary>
void BlinkHighTempLed()
{
	if (millis() - lastBlinkTime < blinkInterval)
		return;

	if (highTempLedPinState == LOW)
	{
		digitalWrite(HighLedPin, HIGH);
		highTempLedPinState = HIGH;
	}
	else
	{
		digitalWrite(HighLedPin, LOW);
		highTempLedPinState = LOW;
	}
	lastBlinkTime = millis();
}

/// <summary>
/// turns the fan on and updates the LCD
/// </summary>
void TurnFanOn()
{
	digitalWrite(FanOnOffPin, HIGH);
	SetFanSpeed();
	fanIsOn = true;
	if (selectedMenuOption == MAIN_MENU)
		UpdateMainMenuFanState();
	lastFanOnOffTime = millis();
}

/// <summary>
/// turns the fan off and updates the LCD
/// </summary>
void TurnFanOff()
{
	pwmDuty(0);
	digitalWrite(FanOnOffPin, LOW);
	fanIsOn = false;
	if (selectedMenuOption == MAIN_MENU)
		UpdateMainMenuFanState();
	lastFanOnOffTime = millis();
}

/// <summary>
/// Begin 25 kHz PWM at pint 3
/// </summary>
void pwm25kHzBegin() 
{
	TCCR2A = 0;                             // TC2 Control Register A
	TCCR2B = 0;                             // TC2 Control Register B
	TIMSK2 = 0;                             // TC2 Interrupt Mask Register
	TIFR2 = 0;                              // TC2 Interrupt Flag Register
	TCCR2A |= (1 << COM2B1) | (1 << WGM21) | (1 << WGM20);  // OC2B cleared/set on match when up/down counting, fast PWM
	TCCR2B |= (1 << WGM22) | (1 << CS21);	// prescaler 8
	OCR2A = TOP_OVERFLOW_VALUE;				// TOP overflow value (Hz)
	OCR2B = 0;
}

/// <summary>
/// Sets the fan speed (PWM duty cycle on pin 3) based on the fanDisplaySpeed 
/// </summary>
void SetFanSpeed()
{
	if (fanDisplaySpeed == 0)
		pwmValue = pwmMinValue;
	else if (fanDisplaySpeed == fanDisplaySpeedMax)
		pwmValue = TOP_OVERFLOW_VALUE;
	else
	{
		int pwmStepSize = (TOP_OVERFLOW_VALUE - pwmMinValue) / fanDisplaySpeedMax;
		pwmValue = fanDisplaySpeed * pwmStepSize;
	}

	if (pwmValue > TOP_OVERFLOW_VALUE)
		pwmValue = TOP_OVERFLOW_VALUE;

	// Set PWM Width (duty)
	pwmDuty(pwmValue);
}

/// <summary>
/// Set PWM Width (duty 0-79)
/// </summary>
/// <param name="ocrb"></param>
void pwmDuty(byte ocrb)
{
	OCR2B = ocrb;
}

/// <summary>
/// Reads and stores the current temperature
/// </summary>
void ReadTemperature()
{
	double c = thermocouple.readCelsius();
	currentTemp = round(c);
	lastTempReadTime = millis();
}

/// <summary>
/// Prints the correct based on the selectedMenuOption value
/// </summary>
void PrintCorrectMenu()
{
	switch (selectedMenuOption)
	{
	case MAIN_MENU:
	{
		PrintMainMenu();
		break;
	}
	case SET_MAX_MENU:
	{
		PrintMaxTempMenu();
		break;
	}
	case SET_MIN_MENU:
	{
		PrintMinTempMenu();
		break;
	}
	case SET_FAN_MENU:
	{
		PrintFanSpeedMenu();
		break;
	}
	case SET_DELAY_MENU:
	{
		PrintFanDelayMenu();
		break;
	}	
	}
}

/// <summary>
/// Prints the Main Menu to the LCD
/// </summary>
void PrintMainMenu()
{
	lcd.clear();
	// print max temp
	lcd.setCursor(0, 0);
	lcd.print("MaxT=");
	if (maxTemp < 100)
		lcd.print(" ");
	lcd.print(maxTemp);
	lcd.print("C");

	// print current temp
	lcd.setCursor(10, 0);
	lcd.print("T=");
	UpdateMainMenuCurrentTemp();
	lcd.print("C");

	// print min temp
	lcd.setCursor(0, 1);
	lcd.print("MinT=");
	if (minTemp < 100)
		lcd.print(" ");
	lcd.print(minTemp);
	lcd.print("C");

	// print fan state (on/off)
	lcd.setCursor(10, 1);
	lcd.print("F=");
	UpdateMainMenuFanState();
}

/// <summary>
/// Updates just the current temperature in the main menu
/// </summary>
void UpdateMainMenuCurrentTemp()
{
	lcd.setCursor(12, 0);
	if (currentTemp < 100)
		lcd.print(" ");
	lcd.print(currentTemp);
}

/// <summary>
/// Updates just the fan state (on/off) in the main menu
/// </summary>
void UpdateMainMenuFanState()
{
	lcd.setCursor(12, 1);
	if (fanIsOn)
	{
		lcd.print("ON");
		lcd.print(" ");
	}
	else
	{
		lcd.print("OFF");
	}
}

/// <summary>
/// prints the "Max temperature" menu to the LCD
/// </summary>
void PrintMaxTempMenu()
{
	lcd.clear();
	lcd.setCursor(0, 0);
	lcd.print("Max. temperature");
	lcd.setCursor(0, 1);
	lcd.print(maxTemp);
	lcd.print(" C");
	if (maxTemp < 100)
		lcd.print(" ");
}

/// <summary>
/// prints the "Min temperature" menu to the LCD
/// </summary>
void PrintMinTempMenu()
{
	lcd.clear();
	lcd.setCursor(0, 0);
	lcd.print("Min. temperature");
	lcd.setCursor(0, 1);
	lcd.print(minTemp);
	lcd.print(" C");
	if (minTemp < 100)
		lcd.print(" ");
}

/// <summary>
/// prints the "Fan Speed" menu to the LCD
/// </summary>
void PrintFanSpeedMenu()
{
	lcd.clear();
	lcd.setCursor(0, 0);
	lcd.print("Fan speed");
	lcd.setCursor(0, 1);
	lcd.print(fanDisplaySpeed);
	if (fanDisplaySpeed < 100)
		lcd.print(" ");
}

/// <summary>
/// prints the "Fan Speed" menu to the LCD
/// </summary>
void PrintFanDelayMenu()
{
	lcd.clear();
	lcd.setCursor(0, 0);
	lcd.print("Fan on/off delay");
	lcd.setCursor(0, 1);
	lcd.print(fanDelay);
	if (fanDelay < 100)
		lcd.print(" ");
}

/// <summary>
/// prints the "Save settings" menu to the LCD
/// </summary>
void PrintSaveSettingMenu()
{
	lcd.clear();
	lcd.setCursor(0, 0);
	lcd.print("Press again to");
	lcd.setCursor(0, 1);
	lcd.print("save");
}

/// <summary>
/// Reads the buttons and returns the pressed button
/// </summary>
/// <returns>button pressed</returns>
int readButtons()
{
	int input = analogRead(A0);

	if (input > 1000) return btnNONE;
	if (input < 50)   return btnRIGHT;
	if (input < 250)  return btnUP;
	if (input < 450)  return btnDOWN;
	if (input < 650)  return btnLEFT;
	if (input < 850)  return btnSELECT;
	return btnNONE;
}