/*
 * CFile1.c
 *
 * Created: 9/26/2013 9:59:00 AM
 *  Author: Gabo Mix
 */ 

#include <util/delay.h>
#include <avr/io.h>
#include <avr/pgmspace.h>
#include <string.h>
#include <avr/interrupt.h>
#include <avr/sleep.h>
#include "main.h"
#include "utils.h"
#include "display.h"
#include "mygccdef.h"
#include "ffft.h"
#include "time.h"
#include "asmutil.h"

void face0(void);
void face1(void);
void face2(void);
int8_t Sine(int8_t angle);
int8_t Cosine(int8_t angle);
uint8_t firstdayofmonth(time_var *timeptr);
void findweekday(time_var *timeptr);

// 60 value sine table
const int8_t SIN60[] PROGMEM = {
    0,     13,  26,  39,  51,  63,  74,  84,  94, 102, 109, 116, 120, 124, 126,
    127,  126, 124, 120, 116, 109, 102,  94,  84,  74,  63,  51,  39,  26,  13,
    0,    -13, -26, -39, -51, -63, -74, -84, -94,-102,-109,-116,-120,-124,-126,
    -127,-126,-124,-120,-116,-109,-102, -94, -84, -74, -63, -51, -39, -26, -13
};

const uint8_t monthDays[] PROGMEM = { 31,28,31,30,31,30,31,31,30,31,30,31 };

const char months[][10] PROGMEM = {           // Months:
    "January",
    "February",
    "March",
    "April",
    "May",
    "June",
    "July",
    "August",
    "September",
    "October",
    "November",
    "December",
};

const char minimonths[][10] PROGMEM = {           // Months:
    "JAN", "FEB", "MAR", "APR", "MAY", "JUN", "JUL", "AUG", "SEP", "OCT", "NOV", "DEC",
};

const char days[][4] PROGMEM = {           // Days of the week, Jan-1-2000 was Saturday
    "Sat","Sun","Mon","Tue","Wed","Thu","Fri"
};

uint8_t Selected=0;   // Selected item to change
#define SECOND      1
#define MINUTE      2
#define HOUR        3
#define DAY         4
#define MONTH       5
#define YEAR        6

uint8_t options;    // Daylight saving time
                    // Military format
                    // Watch face


// Temperature compensation:
// f = fo (1-PPM(T-To))^2
// RTC will lose time if the temperature is increased or decreased from the room temperature value (25�C)
// Coefficient = ?T^2 x -0.036 ppm

//The firmware repeats the following steps once per minute to calculate and accumulate lost time.
// 1. The ADC is used to measure the die temperature from the on-chip temperature sensor.
// 2. The value measured by the ADC is then used to calculate the deviation in ppm, and the result is stored in memory.
// This indicates the number of microseconds that need to be compensated.
//
// At the end of a 24-hour period, the total accumulated error is added to the RTC time to complete the compensation
// process. The temperature is assumed to not vary widely within a one-minute period.



// RTC Compare
ISR(RTC_COMP_vect) {
    VPORT1.OUT = 0;         // Turn off LEDs
    RTC.INTCTRL = 0x01;     // Generate low level interrupts (Overflow)
}

// RTC Overflow, occurs every 500ms
ISR(RTC_OVF_vect) {
    uint16_t comp;
    SLEEP.CTRL = 0x00;
    EXTCOMM();              // LCD polarity inversion
    now.halfsec++;
    if(now.halfsec>=120) {                      // One minute
        now.halfsec=0;
        now.min++;
        if(now.min>=60) {                       // One hour
            now.min=0;
            now.hour++;
            if(now.hour>=24) {                  // One day
                now.hour=0;
                now.mday++;
                now.wday++;
                if(now.wday>=7) now.wday=0;
                uint8_t daysinmonth;
                daysinmonth = pgm_read_byte_near(monthDays+now.mon);
                if (LEAP_YEAR(now.year)) daysinmonth++;
                if(now.mday>=daysinmonth) {     // One month
                    now.mday=0;
                    now.mon++;
                    if(now.mon>=12) {           // One year
                        now.mon=0;
                        now.year++;
                    }
                }
                
            }
        }
    }
}

// 60 -> 2*PI
int8_t Sine(int8_t angle) {
//    if(angle<0) angle=60-angle;
    while(angle>=60) angle-=60;
    return (int8_t)pgm_read_byte_near(SIN60+angle);
}

// 60 -> 2*PI
int8_t Cosine(int8_t angle) {
//    if(angle<0) angle=-angle;
    angle+=15;
    while(angle>=60) angle-=60;
    return (int8_t)pgm_read_byte_near(SIN60+angle);
}

// Digital Watch Face
void face0(void) {
    uint8_t n,d=0;
    if(testbit(WOptions,seconds) || Selected) {
        if(!testbit(now.halfsec,0) || Selected!=SECOND ) {  // Flash when changing
            n=now.halfsec>>1;
            while (n>=10) { d++; n-=10; }
            bitmap(104,10,(int16_t)pgm_read_word(sDIGITS+d));
            bitmap(117,10,(int16_t)pgm_read_word(sDIGITS+n));
        }
        // Only draw seconds
//        if(now.halfsec!=0 && !testbit(WOptions,update)) return;
    }        
    bitmap(118,0,BELL);
    bitmap(2,0,BATTERY);
    lcd_goto(74,0);
    lcd_put5x8(PSTR("8:00 AM"));
    if(!testbit(now.halfsec,0) || Selected!=HOUR ) {  // Flash when changing
        n=now.hour; 
        if(n>=12) { n-=12; bitmap(117,8,PM); }
        else bitmap(105,8,AM);
        if(n==0) n=12;
        if(n>=10) { n-=10; bitmap(0,8,DIGI1); }
        bitmap(25,8,(int16_t)pgm_read_word(DIGITS+n));
    }
    bitmap(49,8,DOTS);
    if(!testbit(now.halfsec,0) || Selected!=MINUTE ) {  // Flash when changing
        n=now.min; d=0;
        while (n>=10) { d++; n-=10; }
        bitmap(55,8,(int16_t)pgm_read_word(DIGITS+d));
        bitmap(80,8,(int16_t)pgm_read_word(DIGITS+n));
    }
    // Date
    n=now.wday;
    if(!testbit(now.halfsec,0) || Selected!=YEAR ) {  // Flash when changing
        bitmap(3,3,(int16_t)pgm_read_word(mWEEK+n));
	}
    if(!testbit(now.halfsec,0) || Selected!=MONTH ) {  // Flash when changing
        n=now.mon+1;    // Month.       [0-11]
        if(n>=10) { n-=10; bitmap(49,3,mDIGI1); }
        bitmap(65,3,(int16_t)pgm_read_word(mDIGITS+n));
    }
    bitmap(80,3,mDIGIdash);
    if(!testbit(now.halfsec,0) || Selected!=DAY ) {  // Flash when changing
        n=now.mday+1; d=0; // Day.         [0-30]
        while (n>=10) { d++; n-=10; }
        bitmap(95,3,(int16_t)pgm_read_word(mDIGITS+d));
        bitmap(111,3,(int16_t)pgm_read_word(mDIGITS+n));
    }
}

// Analog Watch Face
void face1(void) {
    uint8_t s,m,h,i;    
    m=now.min; s=now.halfsec>>1;
    h=now.hour*5+m/12;
    // Background image
    memcpy(Disp_send.buffer,Disp_send.buffer3,DISPLAY_DATA_SIZE);
    // Date
    lcd_goto(50,10); lcd_putsp(minimonths[now.mon]); GLCD_Putchar(' '); printN(now.mday+1);
    // Hours
    fillTriangle(63+FMULS8R(Sine(h-5+60),8),63-FMULS8R(Cosine(h-5+60),8),
                 63+FMULS8R(Sine(h+5),8),   63-FMULS8R(Cosine(h+5),8),
                 63+FMULS8R(Sine(h),36),    63-FMULS8R(Cosine(h),36), 2);
    // Minutes
    fillTriangle(63+FMULS8R(Sine(m-4+60),8),63-FMULS8R(Cosine(m-4+60),8),
                 63+FMULS8R(Sine(m+4),8),63-FMULS8R(Cosine(m+4),8),
                 63+FMULS8R(Sine(m),50),  63-FMULS8R(Cosine(m),50), 2);
    // Seconds           
    if(testbit(WOptions,seconds)) {    
        lcd_line(63,63,63+FMULS8(Sine(s),54),63-FMULS8(Cosine(s),54));
    }        
}

// Oscilloscope Watch Face
void face2(void) {
    
}

void WATCH(void) {
    uint8_t *p;    
    uint8_t newface=0;
    uint8_t Faces=0;
    uint8_t Change_timeout;
    setbit(WOptions,update);
    do {
        // Refresh screen?
        if(  Selected ||                          // Changing the time
             testbit(WOptions,update) ||                            // Forced update
           (!testbit(WOptions,seconds) && now.halfsec==0) ||        // Every minute
           ( testbit(WOptions,seconds) && (!testbit(now.halfsec,0))) ) {   // Every second or minute in low power
                    
            if(testbit(WOptions,seconds)) dma_display();    // Don't use double buffer in low power (not displaying seconds)
            SwitchBuffer();
            clr_display();
            switch(Faces) {
                case 0: face0(); break;
                case 1: face1(); break;
				case 2: face2(); break;
            }
            if(!testbit(WOptions,seconds)) {   // Don't use double buffer in low power
                dma_display();
            }                
            clrbit(WOptions,update);
            WaitDisplay();
        }
        // Check user input
        if(testbit(Key,userinput)) {
            clrbit(Key, userinput);
            setbit(WOptions,update);
            if(testbit(Key,K1)) {
                if(Faces==0) togglebit(WOptions, seconds);
                newface=0;
            }
            if(testbit(Key,K2)) {
                if(Faces==1) togglebit(WOptions, seconds);
                newface=1;
            }
/*            if(testbit(Key,K3)) {
                if(Faces==2) togglebit(WOptions, seconds);
                newface=2;
            }*/
            if(testbit(Key,KM)) {
                Selected++;
                if(Selected>YEAR) Selected=0;
            }
            if(Selected) {
                Change_timeout = 120;   // 120 half seconds -> 1 Minute
                cli();  // Prevent the RTC interrupt from changing the time in this block
                switch(Selected) {
                    case SECOND:
                        if(testbit(Key,KI)) if(now.halfsec<119) now.halfsec++;
                        if(testbit(Key,KD)) if(now.halfsec) now.halfsec--;
                    break;
                    case MINUTE:
                        if(testbit(Key,KI)) if(now.min<59) now.min++;
                        if(testbit(Key,KD)) if(now.min) now.min--;
                    break;
                    case HOUR:
                        if(testbit(Key,KI)) if(now.hour<59) now.hour++;
                        if(testbit(Key,KD)) if(now.hour) now.hour--;
                    break;
                    case DAY:
                        if(testbit(Key,KI)) if(now.mday<pgm_read_byte_near(monthDays+now.mon)-1) now.mday++;
                        if(testbit(Key,KD)) if(now.mday) now.mday--;
                    break;
                    case MONTH:
                        if(testbit(Key,KI)) if(now.mon<11) now.mon++;
                        if(testbit(Key,KD)) if(now.mon) now.mon--;
                    break;
                    case YEAR:
                        if(testbit(Key,KI)) if(now.year<98) now.year++;
                        if(testbit(Key,KD)) if(now.year) now.year--;
                    break;
                }
                findweekday(&now);
                sei();
            }
        }
        if(Change_timeout==0) {
            Selected=0;
        }
        else Change_timeout--;
        if(Faces!=newface) {    // The watch face has changed
            Faces=newface;
            switch(Faces) {
                case 0: break;
                case 1:
                    // Pre calculate background image, save in buffer 3
                    Disp_send.buffer=Disp_send.buffer3;
                    p=Disp_send.buffer3;
                    for(uint8_t i=0; i<128; i++) {  // Solid background
                        for(uint8_t j=0; j<16; j++) {
                            *p++=0xFF;
                        }
                        p+=2;
                    }                                                    
                    circle_fill(63,63,75,0);
                    circle_fill(63,63,70,1);
                    circle_fill(63,63,65,0);
                    circle_fill(63,63,4,1);
                    // Little second markers
                    for(uint8_t i=0; i<60; i++) {
                        lcd_line(63+FMULS8R(Sine(i),60),63-FMULS8R(Cosine(i),60),
                        63+FMULS8R(Sine(i),63),63-FMULS8R(Cosine(i),63));
                    }
                    // Print big numbers
                    lcd_goto(54,2); GLCD_Bigchar(1); GLCD_Bigchar(2);   // 12
                    lcd_goto(108,8); GLCD_Bigchar(3);                   // 3
                    lcd_goto(58,14); GLCD_Bigchar(6);                   // 6
                    lcd_goto(8,8); GLCD_Bigchar(9);                     // 9
                    tiny_printp(40,5,PSTR("OSCILLOSCOPE"));
                    tiny_printp(54,6,PSTR("WATCH"));
                break;
                case 2: break;
            }
        }
        SLEEP.CTRL = SLEEP_SMODE_PSAVE_gc | SLEEP_SEN_bm;
        asm("sleep");
        asm("nop");
    } while(!testbit(Key,KB));
}

void CALENDAR(void) {
    uint8_t day=0xFF;
    time_var showdate;
    showdate = now;
    do {
        if(testbit(WOptions,update) || (day!=now.mday)) {
            uint8_t wday, mdays;
            day=now.mday;
            clrbit(WOptions,update);
            clr_display();
            lcd_goto(28,0);
            lcd_put5x8(months[showdate.mon]); lcd_put5x8(PSTR(" 20")); printN5x8(showdate.year);
            lcd_goto(4,2); lcd_put5x8(PSTR("Su Mo Tu We Th Fr Sa"));
            for(uint8_t i=27; i<=123; i+=16) {
                lcd_hline(1,126,i,1);
            }
            for(uint8_t i=1; i<128; i+=18) {
                lcd_line(i,27,i,123);
            }
            wday=firstdayofmonth(&showdate);
            mdays=pgm_read_byte_near(monthDays+showdate.mon);
            for(uint8_t i=0,j=4,d=wday; i<mdays; i++,d++) {
                if(d==7) { j+=2; d=0; } // Print next week line
                lcd_goto(d*18+5,j); printN5x8(i+1);
                if(i==now.mday && showdate.year==now.year && showdate.mon==now.mon) { // Highlight today
                    fillRectangle(d*18+2,j*8-4,d*18+18,j*8+10,2);
                }
                if(i==showdate.mday) {  // Highlight selected day
                    Rectangle(d*18+3,j*8-3,d*18+17,j*8+9,2);
                }
            }
            dma_display();
            WaitDisplay();
        }
        if(testbit(Key,userinput)) {
            clrbit(Key, userinput);
            setbit(WOptions,update);
            if(testbit(Key,K1)) {
                if(showdate.mday) showdate.mday--;
            }
            if(testbit(Key,K2)) {
            }
            if(testbit(Key,K3)) {
                if(showdate.mday<(pgm_read_byte_near(monthDays+showdate.mon)-1)) showdate.mday++;
            }
            if(testbit(Key,KM)) {
                if(showdate.mon) showdate.mon--;
                else if(showdate.year) {
                    showdate.year--;
                    showdate.mon=11;
                }
            }
            if(testbit(Key,KD)) {
                if(showdate.mon<11) showdate.mon++;
                else if(showdate.year<100) {
                    showdate.mon=0;
                    showdate.year++;
                }
            }                
        }
        SLEEP.CTRL = SLEEP_SMODE_PSAVE_gc | SLEEP_SEN_bm;
        asm("sleep");
        asm("nop");
    } while(!testbit(Key,KB));
}

/* convert calendar time (seconds since 2000) to broken-time
   This only works for dates between 01-01-1970 00:00:00 and 
   19-01-2038 03:14:07 */
void gettime(time_var *timep) {
    uint16_t second,days;
    second=RTC.CNT; days=TCF0.CNT;    // Capture timer values
    timep->year=0;
    timep->mon=0;
    timep->mday=0;
    timep->hour=0;
    timep->min=0;
    
    if(testbit(lobyte(days),0)) timep->hour+=12;
    days/=2;
    while(second>=60*60) {      // Get hours
        second-=60*60;
        timep->hour++;
    }        
    while(second>=60) {         // Get minutes
        second-=60;
        timep->min++;
    }        
    timep->halfsec = seconds*2;    // Get seconds
    timep->wday= days%7;     // Day of the week
    
    uint16_t daysinyear;
    daysinyear = 366;   // First year, 2000, is leap year
    while(days >= daysinyear) {  // Get years
        timep->year++;
        days -= daysinyear;
        daysinyear = LEAP_YEAR(timep->year) ? 366 : 365;
    }
  
    uint16_t daysinmonth=31;  // January
    while(days>=daysinmonth) {
        timep->mon++;
        days -= daysinmonth;
        daysinmonth=pgm_read_byte_near(monthDays+timep->mon);
        if(LEAP_YEAR(timep->year) && timep->mon==1) daysinmonth+=1;
    }
    timep->mday=days;
}

// Set timers
// RTC  will count seconds up to half a day (43200 seconds)
// TCF0 will count half days (max count about 89 years)
void settime(time_var *timeptr) {
    uint16_t second, halfdays;
    
    // halfdays since 2000 1 jan 00:00:00
    halfdays = (timeptr->year)*(2*365);

    // add extra days for leap years
    for (uint8_t i=0; i<timeptr->year; i++) {
        if (LEAP_YEAR(i)) halfdays += 2;
    }

    // add days for this year
    for (uint8_t i=0; i<timeptr->mon; i++) {
        halfdays += 2*pgm_read_byte_near(monthDays+i);
        if (i==1 && LEAP_YEAR(timeptr->year)) halfdays += 2;
    }
    // add remaining days in month
    halfdays += 2*timeptr->mday;
    // half day passed?
    if((timeptr->hour)>=12) {
        halfdays++;
        second = (timeptr->hour-12)*60*60;
    }
    else second = timeptr->hour*60*60;
    second+= timeptr->min*60;
    second+= timeptr->halfsec>>1;

    TCF0.CNT =  halfdays;
    while(RTC.STATUS & RTC_SYNCBUSY_bm);  // Wait for RTC / Main clock sync
    RTC.CNT = second;
}

// Returns the column number for the calendar display, last column is Saturday = 6
uint8_t firstdayofmonth(time_var *timeptr) {
    static uint16_t days;  // Days since 2000-01-01
    
    days = (timeptr->year)*365;
    // add extra days for leap years
    for (uint8_t i=0; i<timeptr->year; i++) {
        if (LEAP_YEAR(i)) days++;
    }
    // add days for this year
    for (uint8_t i=0; i<timeptr->mon; i++) {
        days += pgm_read_byte_near(monthDays+i);
        if (i==1 && LEAP_YEAR(timeptr->year)) days++;
    }
    return ((days+6)%7);    // 2000-01-01 was Saturday ==> Add 6
}

// Finds the correct day of the week for a given date, Saturday = 0;
void findweekday(time_var *timeptr) {
    static uint16_t days;  // Days since 2000-01-01
    
    days = (timeptr->year)*365;
    // add extra days for leap years
    for (uint8_t i=0; i<timeptr->year; i++) {
        if (LEAP_YEAR(i)) days++;
    }
    // add days for this year
    for (uint8_t i=0; i<timeptr->mon; i++) {
        days += pgm_read_byte_near(monthDays+i);
        if (i==1 && LEAP_YEAR(timeptr->year)) days++;
    }
    // add remaining days in month
    days += timeptr->mday;
    timeptr->wday = (days%7);    // 2000-01-01 was Saturday = 0
}
