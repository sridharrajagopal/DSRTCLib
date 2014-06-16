/*

 Arduino RTC Library for Maxim Integrated's DS1337 & DS1339 RTC
 by Sridhar Rajagopal (sridhar.rajagopal at google's mail)

 This is a fork of
 Yet-Another DS1337 RTC Library
 By Tim Gipson (drmn4ea at google's mail)
 
 which is 
 Based loosely on mattt and xSmurf's RTC library at (http://www.arduino.cc/cgi-bin/yabb2/YaBB.pl?num=1191209057/0)
 epoch_seconds_to_date() function based on this Dallas/Maxim application note: http://www.maxim-ic.com/app-notes/index.mvp/id/517
 
 This library is written for use with any Arduino board with no/minimal changes 
 (modifying the alarm functions to work without interrupts if needed).

 The DS1337 library is suited for Mosquino, and you have to make some
 modifications to make it work elsewhere. I saw that the DS1337 and DS1339 
 both have similar functionality and data format (the DS1339 has an extra 
 trickle charger in case you want to use a super cap instead of a coin cell 
 battery. I've modified the library to allow for the configuration settings
 to be specified by the caller
 
*/

extern "C" {

	#include <avr/power.h>
	#include <avr/sleep.h>

	// Dummy "interrupt handler" for sleep to wake up to on alarm interrupt
	void _dummy_int_handler(void)
	{

	}
	
}

//#include <Arduino.h>
#include "DSRTCLib.h"
#include <Wire.h>

// NOTE: To keep the math from getting even more lengthy/annoying than it already is, the following constraints are imposed:
//   1) All times are in 24-hour format (military time)
//   2) DayOfWeek field is not used internally or checked for validity. Alarm functions may optionally set alarms repeating on DayOfWeek, but this feature has not been tested yet.
//   3) This library's buffer stores all times in raw BCD format, just as it is sent from the RTC.
//      It is not converted to/from 'real' (binary) values until needed via get...() and set...() functions.
//      In other words, don't go hacking around and reading from the rtc_bcd[] buffer directly, unless you want the raw BCD results.


// Cumulative number of days elapsed at the start of each month, assuming a normal (non-leap) year.
const unsigned int monthdays[] = {0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334};

DSRTCLib::DSRTCLib() : _rtc_int_pin(2), _rtc_int_number(0)
{
  init();
}

DSRTCLib::DSRTCLib(int int_pin, int int_number) : _rtc_int_pin(int_pin), _rtc_int_number(int_number)
{
  init();
}

void DSRTCLib::init() {
	Wire.begin();
	pinMode(_rtc_int_pin, INPUT);
	digitalWrite(_rtc_int_pin, HIGH);	// enable software pullup resistor on RTC interrupt pin
}

// Aquire data from the RTC chip in BCD format
// refresh the buffer
void DSRTCLib::readTime(void)
{
// use the Wire lib to connect to tho rtc
// reset the register pointer to zero
	Wire.beginTransmission(DSRTCLib_CTRL_ID);
	I2C_WRITE((uint8_t)0x00); // Explicit cast is to hack around http://code.google.com/p/arduino/issues/detail?id=527
	Wire.endTransmission();

// request the 7 bytes of data    (secs, min, hr, dow, date. mth, yr)
	Wire.requestFrom(DSRTCLib_CTRL_ID, 7);
	for(int i=0; i<7; i++)
	{
	// store data in raw bcd format
		if (Wire.available())
			rtc_bcd[i]=I2C_READ();
	}
}

// Read the current alarm value. Note that the repeat flags and DY/DT are removed from the result.
void DSRTCLib::readAlarm(void)
{
        //alarm_repeat = 0;
        byte temp;
// use the Wire lib to connect to tho rtc
// point to start of Alarm1 registers
	Wire.beginTransmission(DSRTCLib_CTRL_ID);
	I2C_WRITE((uint8_t)DSRTCLib_ARLM1);
	Wire.endTransmission();

// request the *4* bytes of data (secs, min, hr, dow/date). Note the format is nearly identical, except for the choice of dayOfWeek vs. date,
// and that the topmost bit of each helps determine if/how the alarm repeats.
	Wire.requestFrom(DSRTCLib_CTRL_ID, 4);
	for(int i=0; i<4; i++)
	{
                // store data in raw bcd format
		if (Wire.available())
		{
			temp = I2C_READ();
			rtc_bcd[i] = temp & B01111111;
		}
	}

	// 4th byte read may contain either a date or DayOfWeek, depending on the value of the DY/DT flag.
	// For laziness sake we read it into the DayOfWeek field regardless (rtc_bcd[3]). Correct as needed...
        if(rtc_bcd[3] & B01000000) // DY/DT set: DayOfWeek
        {
           rtc_bcd[3] &= B10111111; // clear DY/DT flag
           rtc_bcd[4] = 0; // alarm *date* undefined
        }
        else
        {
            rtc_bcd[4] = rtc_bcd[3];
            rtc_bcd[3] = 0; // alarm dayOfWeek undefined
        }
}

// update the data on the IC from the bcd formatted data in the buffer

void DSRTCLib::writeTime(void)
{
	Wire.beginTransmission(DSRTCLib_CTRL_ID);
	I2C_WRITE((uint8_t)0x00); // reset register pointer
	for(int i=0; i<7; i++)
	{
		I2C_WRITE(rtc_bcd[i]);
	}
	Wire.endTransmission();

	// clear the Oscillator Stop Flag
        setRegister(DSRTCLib_STATUS, getRegister(DSRTCLib_STATUS) & !DSRTCLib_STATUS_OSF);
}

void DSRTCLib::writeTime(unsigned long sse)
{
        epoch_seconds_to_date(sse);
        writeTime();
}

// FIXME: automatically set alarm interrupt after writing new alarm? Nah...

// Write the BCD alarm value in the buffer to the alarm registers.
// If an alarm repeat mode has been specified, poke those bytes into the buffer before sending.
void DSRTCLib::writeAlarm(void)
{
	Wire.beginTransmission(DSRTCLib_CTRL_ID);
	I2C_WRITE((uint8_t)DSRTCLib_ARLM1); // set register pointer

        I2C_WRITE(rtc_bcd[DSRTCLib_SEC] | ((alarm_repeat & B00000001 ) << 7)); // A1M1
        I2C_WRITE(rtc_bcd[DSRTCLib_MIN] | ((alarm_repeat & B00000010 ) << 6)); // A1M2
        I2C_WRITE(rtc_bcd[DSRTCLib_HR] | ((alarm_repeat & B00000100 ) << 5)); // A1M3

        // Check if we are using date or DayOfWeek and send the appropriate value
        if(alarm_repeat & B00001000) // DayOfWeek
        {
            // send DOW as 4th alarm reg byte
            I2C_WRITE(rtc_bcd[DSRTCLib_DOW] | ((alarm_repeat & B00011000 ) << 3)); // A1M4 and DY/DT
        }
        else // date
        {
            // send date as 4th alarm reg byte
            I2C_WRITE(rtc_bcd[DSRTCLib_DATE] | ((alarm_repeat & B00011000 ) << 3)); // A1M4 and DY/DT
        }

	Wire.endTransmission();
}


void DSRTCLib::writeAlarm(unsigned long sse)
{
        epoch_seconds_to_date(sse);
        writeAlarm();
}

void DSRTCLib::setAlarmRepeat(byte repeat)
{
        alarm_repeat = repeat;
}


unsigned char DSRTCLib::getRegister(unsigned char registerNumber)
{
	Wire.beginTransmission(DSRTCLib_CTRL_ID);
	I2C_WRITE(registerNumber);
	Wire.endTransmission();

	Wire.requestFrom(DSRTCLib_CTRL_ID, 1);

	return I2C_READ();
}

void DSRTCLib::setRegister(unsigned char registerNumber, unsigned char value)
{
	Wire.beginTransmission(DSRTCLib_CTRL_ID);
	I2C_WRITE(registerNumber); // set register pointer

	I2C_WRITE(value);

	Wire.endTransmission();
}

unsigned char DSRTCLib::time_is_set()
{
  // Return TRUE if Oscillator Stop Flag is clear (osc. not stopped since last time setting), FALSE otherwise
  byte asdf = ((getRegister(DSRTCLib_STATUS) & DSRTCLib_STATUS_OSF) == 0);
  return asdf;
}
unsigned char DSRTCLib::alarm_is_set()
{
  // Return TRUE if the alarm interrupt flag is enabled.
  byte asdf = (getRegister(DSRTCLib_SP) & DSRTCLib_SP_A1IE);
  return asdf;
}

void DSRTCLib::enable_interrupt()
{
   clear_interrupt();
   setRegister(DSRTCLib_SP, getRegister(DSRTCLib_SP) | DSRTCLib_SP_INTCN | DSRTCLib_SP_A1IE); // map alarm interrupt to INT1 and enable interrupt
}

void DSRTCLib::disable_interrupt()
{
   setRegister(DSRTCLib_SP, getRegister(DSRTCLib_SP) & !DSRTCLib_SP_A1IE);
}

void DSRTCLib::clear_interrupt()
{
   setRegister(DSRTCLib_STATUS, getRegister(DSRTCLib_STATUS) & !DSRTCLib_STATUS_A1F);
}

unsigned char DSRTCLib::getSeconds()
{
    return bcd2bin(rtc_bcd[DSRTCLib_SEC]);
}

unsigned char DSRTCLib::getMinutes()
{
    return bcd2bin(rtc_bcd[DSRTCLib_MIN]);
}
unsigned char DSRTCLib::getHours()
{
    return bcd2bin(rtc_bcd[DSRTCLib_HR]);
}
unsigned char DSRTCLib::getDays()
{
    return bcd2bin(rtc_bcd[DSRTCLib_DATE]);
}
unsigned char DSRTCLib::getDayOfWeek()
{
    return bcd2bin(rtc_bcd[DSRTCLib_DOW]);
}
unsigned char DSRTCLib::getMonths()
{
    return bcd2bin(rtc_bcd[DSRTCLib_MTH]);
}
unsigned int DSRTCLib::getYears()
{
    return 2000 + bcd2bin(rtc_bcd[DSRTCLib_YR]);
}


unsigned long DSRTCLib::date_to_epoch_seconds(unsigned int year, byte month, byte day, byte hour, byte minute, byte second)
{

  //gracefully handle 2- and 4-digit year formats
  if (year > 1999)
  {
     year -= 2000;
  }


// Between year 2000 and 2100, a leap year occurs in every year divisible by 4.

//   sse_y = (((unsigned long)year)*365*24*60*60);
//   sse_ly = ((((unsigned long)year+3)>>2) + ((unsigned long)year%4==0 && (unsigned long)month>2))*24*60*60;
//   sse_d = ((unsigned long)monthdays[month-1] + (unsigned long)day-1) *24*60*60;
//   sse_h = ((unsigned long)hour*60*60);
//   sse_m = ((unsigned long)minute*60);
//   sse_s = (unsigned long)second;
//
//   sse = sse_y + sse_ly + sse_d + sse_h + sse_m + sse_s;



// NB: The multiplication-by-constants below is intentionally left expanded for readability; GCC is smart and will optimize them to single constants during compilation.


  //         Whole year seconds                      Cumulative total of seconds contributed by elapsed leap year days
  unsigned long sse = (((unsigned long)year)*365*24*60*60)   +   ((((unsigned long)year+3)>>2) + ((unsigned long)year%4==0 && (unsigned long)month>2))*24*60*60   +   \
         ((unsigned long)monthdays[month-1] + (unsigned long)day-1) *24*60*60   +   ((unsigned long)hour*60*60)   +   ((unsigned long)minute*60)   + (unsigned long)second;
         // Seconds in days since start of year                      hours                      minutes           sec
  sse += 946684800; // correct for difference between DSRTCLib epoch and UNIX epoch
  return sse;
}


unsigned long DSRTCLib::date_to_epoch_seconds()
{
     unsigned long asdf = date_to_epoch_seconds(int(bcd2bin(rtc_bcd[DSRTCLib_YR])), bcd2bin(rtc_bcd[DSRTCLib_MTH]), bcd2bin(rtc_bcd[DSRTCLib_DATE]), bcd2bin(rtc_bcd[DSRTCLib_HR]), bcd2bin(rtc_bcd[DSRTCLib_MIN]), bcd2bin(rtc_bcd[DSRTCLib_SEC]));
     return asdf;
}



void DSRTCLib::epoch_seconds_to_date(unsigned long seconds_left)
{
   // This routine taken from Dallas/Maxim application note 517
   // http://www.maxim-ic.com/app-notes/index.mvp/id/517
   // Arn't the fastest thing, but it produces correct results.

   // NOTE: The earliest date that can be represented by the DS1337 & DS1339 is 1/1/2000 (946684800 in Unix epoch seconds).
   // Passing an earlier Unix time stamp will fail quietly here (produce a date of 0/0/00), 
   // which will probably make your application angry.

   // ALSO NOTE: This has been optimized some to minimize redundant variables, with the side-effect
   // of making it much harder to understand. Please refer to the original appnote above
   // if you are trying to learn from it :-)


   //unsigned long hour;
   //unsigned long day;
   //unsigned long minute;
   //unsigned long second;
   unsigned long month;
   //unsigned long year;

	unsigned long seconds_left_2;
   //unsigned long whole_minutes;
   //unsigned long whole_hours;
   //unsigned long whole_days;
   //unsigned long whole_days_since_1968;
   unsigned long leap_year_periods;
   unsigned long days_since_current_lyear;
   //unsigned long whole_years;
   unsigned long days_since_first_of_year;
   unsigned long days_to_month;
   //unsigned long day_of_week;

   if(seconds_left >= 946684800)
   {
	   seconds_left -= 946684800; // correct for difference between DS1337/DS1339 and UNIX epochs.

	   seconds_left_2 = seconds_left / 60; // seconds_left_2 = "whole_minutes"
	   rtc_bcd[DSRTCLib_SEC] = bin2bcd(seconds_left - (60 * seconds_left_2));                 // leftover seconds

	   seconds_left = seconds_left_2 / 60; // seconds_left = "whole_hours"
	   rtc_bcd[DSRTCLib_MIN] = bin2bcd(seconds_left_2 - (60 * seconds_left));            // leftover minutes

	   seconds_left_2 = seconds_left / 24; //seconds_left_2 = "whole_days"
	   rtc_bcd[DSRTCLib_HR] = bin2bcd(seconds_left - (24 * seconds_left_2));         // leftover hours

	   //whole_days_since_1968 = whole_days;// + 365 + 366;	// seconds_left_2 = "whole_days" = "whole_days_since_1968"
	   leap_year_periods = seconds_left_2 / ((4 * 365) + 1);

	   days_since_current_lyear = seconds_left_2 % ((4 * 365) + 1);

	   // if days are after a current leap year then add a leap year period
	   if ((days_since_current_lyear >= (31 + 29))) {
		  leap_year_periods++;
	   }
	   seconds_left = (seconds_left_2 - leap_year_periods) / 365; // seconds_left = "whole_years"
	   days_since_first_of_year = seconds_left_2 - (seconds_left * 365) - leap_year_periods;

	   if ((days_since_current_lyear <= 365) && (days_since_current_lyear >= 60)) {
		  days_since_first_of_year++;
	   }
	   //year = seconds_left; // + 68;


		// seconds_left = "year"
		//seconds_left_2 = "month"
	   // walk across monthdays[] to find what month it is based on how many days have passed
	   //   within the current year
	   month = 13;
	   days_to_month = 366;
	   while (days_since_first_of_year < days_to_month) {
		   month--;
		   days_to_month = monthdays[month-1];
		   if ((month > 2) && ((seconds_left % 4) == 0)) {
			   days_to_month++;
			}
	   }
	   
	   rtc_bcd[DSRTCLib_DATE] = bin2bcd( days_since_first_of_year - days_to_month + 1);

	   rtc_bcd[DSRTCLib_DOW] = bin2bcd((seconds_left_2  + 4) % 7);


	   //rtc_bcd[DSRTCLib_SEC] = bin2bcd(second);
	   //rtc_bcd[DSRTCLib_MIN] = bin2bcd(minute);
	   //rtc_bcd[DSRTCLib_HR] = bin2bcd(hour);
	   //rtc_bcd[DSRTCLib_DATE] = bin2bcd(day);
	   //rtc_bcd[DSRTCLib_DOW] = bin2bcd(day_of_week);
	   rtc_bcd[DSRTCLib_MTH] = bin2bcd(month);
	   rtc_bcd[DSRTCLib_YR] = bin2bcd(seconds_left);
   }
	else
	{
	// else: "invalid" (< year 2000) epoch format.
	// 'Best' way to handle this is to zero out the returned date. 
	
	   rtc_bcd[DSRTCLib_SEC] = 0; //0x00 binary = 0x00 BCD
	   rtc_bcd[DSRTCLib_MIN] = 0;
	   rtc_bcd[DSRTCLib_HR] = 0;
	   rtc_bcd[DSRTCLib_DATE] = 0;
	   rtc_bcd[DSRTCLib_DOW] = 0;
	   rtc_bcd[DSRTCLib_MTH] = 0;
	   rtc_bcd[DSRTCLib_YR] = 0;
	}

}





void DSRTCLib::snooze(unsigned long secondsToSnooze)
{ 
  // Given a value in secondsToSnooze, set an alarm for that many seconds into the future and go to sleep.
  // The alarm can be set for a maximum of 28-31 days into the future - it doesn't have settings for months or years.
  
  uint8_t sleep_reg_temp;
  
  readTime(); // update RTC library's buffers to contain the current time.
                  // Remember most functions (including epoch seconds stuff) work on what's in the buffer, not what's in the chip.

  
  setAlarmRepeat(EVERY_MONTH); // There is no DSRTCLib setting for 'alarm once' - once in a month is the most restrictive it gets.

  writeAlarm(date_to_epoch_seconds() + secondsToSnooze);
 
  attachInterrupt(_rtc_int_number, _dummy_int_handler, FALLING);  
  enable_interrupt();
  
  // the default snooze behavior is to put the CPU all the way to sleep. In case the user has previously set a different sleep mode,
  // save the entry sleep mode and restore it after sleeping. NOTE, set_sleep_mode() in avr/sleep.h is actually a giant device-specific mess
  // (making trying to implement a 'get_sleep_mode'-type function that works for all devices an equally nasty mess), but this should cover MOST
  // of the ones likely to be used with Arduino. For those others, user will have to (re)set the desired sleep mode by hand.
  
  #if defined(_SLEEP_CONTROL_REG)
  sleep_reg_temp = _SLEEP_CONTROL_REG;
  #endif
  set_sleep_mode(SLEEP_MODE_PWR_DOWN);
  
  // enable deep sleeping
  sleep_enable();
  
  sleep_cpu(); // sleep. Will we waked by next alarm interrupt
 
  #if defined(_SLEEP_CONTROL_REG)
  _SLEEP_CONTROL_REG = sleep_reg_temp;
  #endif
  
  clear_interrupt(); // tell RTC to clear its interrupt flag and drop the INT line
  disable_interrupt(); // ensure we stop receiving interrupts
  detachInterrupt(_rtc_int_number); // disconnect INT2 from the current interrupt handler.
  
}

void DSRTCLib::custom_snooze(unsigned long secondsToSnooze)
{ 
  // Same as snooze(), but do not change the current sleep mode. Use to sleep at a custom sleep mode other than ...PWR_DOWN.
  // Intentional use of a lighter sleep mode means the user is probably expecting/handling other interrupts - note of course that
  // most interrupts will wake the CPU from sleep mode, so the snooze may be shorter than specified in this case.
   
  readTime(); // update RTC library's buffers to contain the current time.
                  // Remember most functions (including epoch seconds stuff) work on what's in the buffer, not what's in the chip.

  
  setAlarmRepeat(EVERY_MONTH); // There is no DS1337 setting for 'alarm once' - once in a month is the most restrictive it gets.

  writeAlarm(date_to_epoch_seconds() + secondsToSnooze);
 
  attachInterrupt(_rtc_int_number, _dummy_int_handler, FALLING);  
  enable_interrupt();
  
  // enable deep sleeping
  sleep_enable();
  
  sleep_cpu(); // sleep. Will we waked by next alarm interrupt
 
  clear_interrupt(); // tell RTC to clear its interrupt flag and drop the INT line
  disable_interrupt(); // ensure we stop receiving interrupts
  detachInterrupt(_rtc_int_number); // disconnect INT2 from the current interrupt handler.
  
}

void DSRTCLib::setSeconds(unsigned char v)
{
    rtc_bcd[DSRTCLib_SEC] = bin2bcd(v);

}
void DSRTCLib::setMinutes(unsigned char v)
{
    rtc_bcd[DSRTCLib_MIN] = bin2bcd(v);

}
void DSRTCLib::setHours(unsigned char v)
{
    rtc_bcd[DSRTCLib_HR] = bin2bcd(v);

}
void DSRTCLib::setDays(unsigned char v)
{
    rtc_bcd[DSRTCLib_DATE] = bin2bcd(v);

}
void DSRTCLib::setDayOfWeek(unsigned char v)
{
    rtc_bcd[DSRTCLib_DOW] = bin2bcd(v);

}
void DSRTCLib::setMonths(unsigned char v)
{
    rtc_bcd[DSRTCLib_MTH] = bin2bcd(v);

}
void DSRTCLib::setYears(unsigned int v)
{
    if (v>1999)
    {
        v -= 2000;
    }
    rtc_bcd[DSRTCLib_YR] = bin2bcd(v);

}

byte DSRTCLib::bcd2bin(byte v)
{
   return (v&0x0F) + ((v>>4)*10);
}

byte DSRTCLib::bin2bcd(byte v)
{
   return ((v / 10)<<4) + (v % 10);
}

void DSRTCLib::stop(void)
{
	setRegister(DSRTCLib_SP, getRegister(DSRTCLib_SP) | DSRTCLib_SP_EOSC);
}

void DSRTCLib::start(void)
{
	setRegister(DSRTCLib_SP, getRegister(DSRTCLib_SP) & !DSRTCLib_SP_EOSC);
}

// class DS1339

DS1339::DS1339() : DSRTCLib(2, 0)
{
}

DS1339::DS1339(int int_pin, int int_number) : DSRTCLib(int_pin, int_number)
{
}

// class DS1337

DS1337::DS1337() : DSRTCLib(2, 0)
{
}

DS1337::DS1337(int int_pin, int int_number) : DSRTCLib(int_pin, int_number)
{
}
