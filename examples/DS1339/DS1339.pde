#include <DSRTCLib.h>
#include <Wire.h>
#include <avr/power.h>
#include <avr/sleep.h>

/*
  DS1339 RTC Example
  Tests and examples for common RTC library features.
  This shows the basic functions (reading and setting time/alarm values), converting back and forth between epoch seconds and calendar dates,
  and using alarm interrupts.

  Don't let the 'Binary sketch size' throw you; this example file and all its print statements add a lot of overhead.
 
 */


int ledPin =  13;    // LED connected to digital pin 13
int INT_PIN = 2; // INTerrupt pin from the RTC. On Arduino Uno, this should be mapped to digital pin 2 or pin 3, which support external interrupts
int int_number = 0; // On Arduino Uno, INT0 corresponds to pin 2, and INT1 to pin 3

DS1339 RTC = DS1339(INT_PIN, int_number);


void setup()   {                
  pinMode(INT_PIN, INPUT);
  digitalWrite(INT_PIN, HIGH);
  
  pinMode(ledPin, OUTPUT);    
  digitalWrite(ledPin, LOW);


  // enable deep sleeping
  set_sleep_mode(SLEEP_MODE_PWR_DOWN);
  sleep_enable();

  Serial.begin(38400);
  Serial.println ("DSRTCLib Tests");

  RTC.start(); // ensure RTC oscillator is running, if not already
  
  if(!RTC.time_is_set()) // set a time, if none set already...
  {
    Serial.print("Clock not set. ");
    set_time();
  }
  
  // If the oscillator is borked (or not really talking to the RTC), try to warn about it
  if(!RTC.time_is_set())
  {
    Serial.println("Clock did not set! Check that its oscillator is working.");
  }
}

int read_int(char sep)
{
  static byte c;
  static int i;

  i = 0;
  while (1)
  {
    while (!Serial.available())
    {;}
 
    c = Serial.read();
    // Serial.write(c);
  
    if (c == sep)
    {
      // Serial.print("Return value is");
      // Serial.println(i);
      return i;
    }
    if (isdigit(c))
    {
      i = i * 10 + c - '0';
    }
    else
    {
      Serial.print("\r\nERROR: \"");
      Serial.write(c);
      Serial.print("\" is not a digit\r\n");
      return -1;
    }
  }
}

int read_int(int numbytes)
{
  static byte c;
  static int i;
  int num = 0;

  i = 0;
  while (1)
  {
    while (!Serial.available())
    {;}
 
    c = Serial.read();
    num++;
    // Serial.write(c);
  
    if (isdigit(c))
    {
      i = i * 10 + c - '0';
    }
    else
    {
      Serial.print("\r\nERROR: \"");
      Serial.write(c);
      Serial.print("\" is not a digit\r\n");
      return -1;
    }
    if (num == numbytes)
    {
      // Serial.print("Return value is");
      // Serial.println(i);
      return i;
    }
  }
}

int read_date(int *year, int *month, int *day, int *hour, int* minute, int* second)
{

  *year = read_int(4);
  *month = read_int(2);
  *day = read_int(' ');
  *hour = read_int(':');
  *minute = read_int(':');
  *second = read_int(2);

  return 0;
}

void nap()
{
  // Dummy function. We don't actually want to do anything here, just use an interrupt to wake up.
  //RTC.clear_interrupt();
  // For some reason, sending commands to clear the interrupt on the RTC here does not work. Maybe Wire uses interrupts itself?
  Serial.print(".");

}

void loop()                     
{
  Serial.flush();
  Serial.println ("\nRTC Library Tests \n 1) Basic (read and write time) \n 2) Alarm interrupts/wakeup \n 3) date <--> epoch seconds validation \n 4) Read time \n 5) Set time \n");
  Serial.flush();


  while(!Serial.available()){}
  
  switch(Serial.read())
  {
    case '1':
      test_basic();
      break;
    case '2':
      test_interrupts();
      break;      
    case '3':
      test_epoch_seconds();
      break;  
    case '4':
      read_time();
      break;
    case '5':
      set_time();
    default:
      break;
    
  }

}

void set_time()
{
    Serial.println("Enter date and time (YYYYMMDD HH:MM:SS)");
    int year, month, day, hour, minute, second;
    int result = read_date(&year, &month, &day, &hour, &minute, &second);
    if (result != 0) {
      Serial.println("Date not in correct format!");
      return;
    } 
    
    // set initially to epoch
    RTC.setSeconds(second);
    RTC.setMinutes(minute);
    RTC.setHours(hour);
    RTC.setDays(day);
    RTC.setMonths(month);
    RTC.setYears(year);
    RTC.writeTime();
    read_time();
}

void read_time() 
{
  Serial.print ("The current time is ");
  RTC.readTime(); // update RTC library's buffers from chip
  printTime(0);
  Serial.println();

}

void test_basic()
{
  // Test basic functions (time read and write)
  Serial.print ("The current time is ");
  RTC.readTime(); // update RTC library's buffers from chip
  printTime(0);
  Serial.println("\nSetting times using direct method: 1/31/07 12:34:56");
  
    RTC.setSeconds(56);
    RTC.setMinutes(34);
    RTC.setHours(12);
    RTC.setDays(31);
    RTC.setMonths(1);
    RTC.setYears(2007); // 2-digit or 4-digit years are supported
    RTC.writeTime();
    delay(500);  // This is not needed; just making it more clear that we are reading a new result
    RTC.readTime();
    Serial.print("Read back: ");
    printTime(0);
    Serial.println("  (we'll never forget)");
    
    Serial.println("Setting time using epoch seconds: 2971468800 (midnight on 2/29/2064)");
    RTC.writeTime(2971468800u);
    delay(500);  
    RTC.readTime();    
    Serial.print("Read back: ");
    printTime(0);
    Serial.println("  (Happy 21st birthday Carlotta) ");    

    Serial.println("Writing alarm: 8:00am on the 15th of the month.");
    RTC.setSeconds(0);
    RTC.setMinutes(0);
    RTC.setHours(8);
    RTC.setDays(15);
    RTC.setAlarmRepeat(EVERY_MONTH); // There is no DS1339 setting for 'alarm once' - user must shut off the alarm after it goes off.
    RTC.writeAlarm();
    delay(500);
    RTC.readAlarm();
    Serial.print("Read back: ");
    printTime(1);    

    Serial.println("\nWriting alarm: 2:31:05 pm on the 3rd day of the week.");
    RTC.setSeconds(5);
    RTC.setMinutes(31);
    RTC.setHours(14);
    RTC.setDayOfWeek(3);
    RTC.setAlarmRepeat(EVERY_WEEK); // to alarm on matching day-of-week instead of date
    RTC.writeAlarm();
    delay(500);
    RTC.readAlarm();
    Serial.print("Read back: ");
    printTime(1);
    Serial.println("\n");
 }

void test_interrupts()
{
  Serial.println("Setting a 1Hz periodic alarm interrupt to sleep in between. Watchen das blinkenlights...");
  Serial.flush();
  
  // Steps to use an alarm interrupt:
  // 1) attach an interrupt handler (it can be blank if you just want to wake)
  // 2) enable alarm interrupt from RTC using RTC.enable_interrupt();
  // 3) set and write the alarm time
  // 4) sleep! ...zzz...
  // 5) clear the interrupt from RTC using RTC.clear_interrupt();
  // ...
  // 6) If no further alarms desired, disable the RTC alarm interrupt using RTC.disable_interrupt();
  
  attachInterrupt(int_number, nap, FALLING);
  RTC.enable_interrupt();
  RTC.setAlarmRepeat(EVERY_SECOND); // if alarming every second, time registers larger than 1 second (hour, etc.) are don't-care
  RTC.writeAlarm();

  for(byte i = 0; i<3; i++)
  {
    digitalWrite(ledPin, HIGH);

    delay(3); // wait >2 byte times for any pending Tx bytes to finish writing
  
    sleep_cpu(); // sleep. Will we waked by next alarm interrupt
    RTC.clear_interrupt();

    digitalWrite(ledPin, LOW);

    delay(3); // wait >2 byte times for any pending Tx bytes to finish writing
  
    sleep_cpu(); // sleep. Will we waked by next alarm interrupt
    RTC.clear_interrupt();
  }

  RTC.disable_interrupt(); // ensure we stop receiving interrupts
  
  detachInterrupt(int_number);
  
  Serial.println("Going to snooze for 10 seconds...");
  Serial.flush();
  read_time();
  Serial.flush();
  RTC.snooze(10);
  read_time();
  Serial.flush();
  Serial.println("...and wake up again.");  
}

void test_epoch_seconds()
{
  // Output the time calculated in epoch seconds at midnight for every day between 1/1/2000 and 12/31/2099.
  // Also, convert the result back to a date/time and make sure it matches the original value.
  
  // To ensure we are getting clean values to start with, no calculation results are written back to
  // the RTC. Instead, we use the alarm to wake up when it rolls over midnight, then advance the clock
  // to 23:59:59 of that same day. This way one 'day' passes per second, and a century's worth of tests
  // will complete in ~10 hours.

  unsigned char second;
  unsigned char minute;
  unsigned char hour;
  unsigned char month;
  unsigned char day;
  unsigned int year;
  
  unsigned long old_epoch_seconds = 946684800;
  unsigned long new_epoch_seconds = 946684800 + 1;
  
  Serial.println("Going to output and check epoch seconds at midnight on every day \n  from 1/1/2000 to 12/31/2099. This will take a long time! (overnight)\n  You probably want to capture the output to a file (e.g. hyperterminal). \n  Press SPACE to continue or any other key to skip.\n");
  Serial.flush();
  
  while(!Serial.available()){}
  if(Serial.read() == ' ')
  {
    Serial.println("Date, Seconds Since Epoch, Consistency Check Date, Consistency Check Result");


    RTC.writeTime(old_epoch_seconds); // reset time to epoch
    RTC.setAlarmRepeat(EVERY_DAY);
    RTC.writeAlarm(old_epoch_seconds); // ensure alarm starts at a valid value too

    RTC.enable_interrupt(); // make RTC generate a pulse every time one 'day' passes
    
    while(new_epoch_seconds > old_epoch_seconds) // keep going until date rolls over to 1/1/2000 again
    {
        // fastforward to the end of the day. The math for converting hours/min to seconds is trivial; 
        // this test is mainly concerned with ensuring stuff like days-in-a-month and leap years are handled correctly.
        RTC.readTime();  // restore known-good copy of date/time from chip to library's buffer
        RTC.setHours(23);
        RTC.setMinutes(59);
        RTC.setSeconds(59);
        RTC.writeTime(); // note: writing a new time resets the RTC's oscillator count ("milliseconds") to 0, so we have a full second until the next interrupt happens.
        
      while(digitalRead(INT_PIN) != 0) {}  // wait for 'day' to rollover. Pin 24 = INT2
  
        RTC.readTime();
  
        //store a copy of the original values
        second = RTC.getSeconds();
        minute = RTC.getMinutes();
        hour = RTC.getHours();        
        day = RTC.getDays();
        month = RTC.getMonths();        
        year = RTC.getYears();        

        printTime(0);
        
        old_epoch_seconds = new_epoch_seconds;
        new_epoch_seconds = RTC.date_to_epoch_seconds();

        Serial.print(" , ");
        Serial.print(new_epoch_seconds);
        Serial.print(" , ");
        
        // ensure that the result converted back to date matches the original value.
        // Remember that this function will update the contents of the RTC library's buffer, NOT on the chip.
        RTC.epoch_seconds_to_date(new_epoch_seconds);
        printTime(0);
        
        if( second == RTC.getSeconds() && minute == RTC.getMinutes() && hour == RTC.getHours() && day == RTC.getDays() && month == RTC.getMonths() && year == RTC.getYears() )
        {
          Serial.println(", Pass");
        }
        else
        {
          Serial.println(", FAIL!");
        }
        
    }
    Serial.println("\n\nDone!");
    RTC.disable_interrupt();
  }
}


void printTime(byte type)
{
  // Print a formatted string of the current date and time.
  // If 'type' is non-zero, print as an alarm value (seconds thru DOW/month only)
  // This function assumes the desired time values are already present in the RTC library buffer (e.g. readTime() has been called recently)

  if(!type)
  {
    Serial.print(int(RTC.getMonths()));
    Serial.print("/");  
    Serial.print(int(RTC.getDays()));
    Serial.print("/");  
    Serial.print(RTC.getYears());
  }
  else
  {
    //if(RTC.getDays() == 0) // Day-Of-Week repeating alarm will have DayOfWeek *instead* of date, so print that.
    {
      Serial.print(int(RTC.getDayOfWeek()));
      Serial.print("th day of week, ");
    }
    //else
    {
      Serial.print(int(RTC.getDays()));
      Serial.print("th day of month, ");      
    }
  }
  
  Serial.print("  ");
  Serial.print(int(RTC.getHours()));
  Serial.print(":");
  Serial.print(int(RTC.getMinutes()));
  Serial.print(":");
  Serial.print(int(RTC.getSeconds()));  
}


