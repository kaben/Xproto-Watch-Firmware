/*****************************************************************************

XMEGA Oscilloscope and Development Kit

Gabotronics
February 2012

Copyright 2012 Gabriel Anzziani

This file is proprietary software.

The customer has the right to:

    Download, read, and modify the source code
    Compile and make use of their own modifications
    Distribute their changes in a compiled form
    Take backups of the source code for their own use

The customer does not have the right to:

    Sell or distribute the source code
    Sell or distribute a modified version of the code
    Use the source code to create a competing product
    Receive any kind of support for their modifications (though questions are welcome)


www.gabotronics.com
email me at: gabriel@gabotronics.com

*****************************************************************************/

#include <avr/io.h>
#include <util/delay.h>
#include <avr/interrupt.h>
#include <avr/sleep.h>
#include "main.h"
#include "mso.h"
#include "logic.h"
#include "awg.h"
#include "interface.h"
#include "USB\usb_xmega.h"

// Function prototypes
void Reduce(void);
void RestorefromMeter(void);		// Restores srate and gains
void GoingtoMeter(void);			// Saves srate and gains
uint8_t fft_stuff(uint8_t *p1);
void AutoCursorV(void);             // Automatically set vertical the cursors
static inline void Measurements(void);  // Measurements for Meter Mode
static inline void ShowCursorV(void);   // Display Vertical Cursor
static inline void ShowCursorH(void);   // Display Horizontal Cursor
void CheckMax(void);                // Check variables
void CheckPost(void);               // Check Post Trigger
void Apply(void);                   // Apply oscilloscope settings

// Global variables
uint8_t  adjusting = 0;             // Auto setup adjusting step
uint16_t slowval;                   // Slow sampling rate time value
uint8_t old_s, old_g1, old_g2;      // sampling, gains before entering meter mode
uint8_t shortcuti  = 0;             // shortcut index

ACHANNEL CH1,CH2;                   // Analog Channel 1, Channel 2
DATA DC;                             // Data samples
uint8_t EEMEM EECHREF1[256] = {0};  // Reference waveform CH1
uint8_t EEMEM EECHREF2[256] = {0};  // Reference waveform CH2

// ADC system clock timers

// TCE0 controls Interrupt ADC, srate: 6    7    8    9    10
const uint16_t TCE0val[5] PROGMEM = { 249, 499, 999, 2499, 4999 };

// TCE0 timer is fixed for slow sampling rates (20mS/div and under)
// slowcnt controls the sampling rate.
const uint16_t slowcnt[11] PROGMEM = { 1,2,4,8,20,40,80,200,400,800,2000 };

/* Clock Timer table:
// TCE0 freq = fPER / (N(CCA+1)) = 32M / (2(CCA+1))
// Slow sampling: TCE0 = 640Hz, rate = 32*slowcnt/640
rate SCOPE SETTING  Min. F      Max.F     S/s  Oversample ADCCLK  PRESCALE TCE0 slowcnt  Method
0       8 uS/div   7812.50Hz     1 MHz     2M      x1     2 MHz     16     x     x       DMA ADC free run
1      16 uS/div   3906.25Hz   500 kHz     1M      x1     2 MHz     16     x     x       DMA ADC free run
2      32 uS/div  1953.125Hz   250 kHz   500k      x2     2 MHz     16     x     x       DMA ADC free run
3      64 uS/div   976.563Hz   125 kHz   250k      x2     1 MHz     32     x     x       DMA ADC free run
4     128 uS/div   488.281Hz  62.5 kHz   125k      x2   500 kHz     64     x     x       DMA ADC free run
5     256 uS/div   244.141Hz 31.25 kHz  62.5k      x2   250 kHz    128     x     x       DMA ADC free run
6     500 uS/div   125.000Hz    16 kHz    32k      x2   125 kHz     32    249    x       DMA w/Timer
7       1 mS/div    62.500Hz     8 kHz    16k      x2   125 kHz     32    499    x       DMA w/Timer
8       2 mS/div    31.250Hz     4 kHz     8k      x2   125 kHz     32    999    x       DMA w/Timer
9       5 mS/div    12.500Hz   1.6 kHz   3.2k      x2   125 kHz     32    2499   x       DMA w/Timer
10     10 mS/div     6.250Hz   800 Hz    1.6k      x2   125 kHz     32    4999   x       DMA w/Timer
11     20 mS/div     6.250Hz   800 Hz    1.6k      x1   125 kHz     32    4999   1       ISR TCE0 1600Hz
12     50 mS/div     2.500Hz   320 Hz     640      x2   125 kHz     32    6249   2       ISR TCE0 1280Hz
13    100 mS/div     1.250Hz   160 Hz     320      x4   125 kHz     32    6249   4       ISR TCE0 1280Hz
14    200 mS/div     0.625Hz    80 Hz     160      x8   125 kHz     32    6249   8       ISR TCE0 1280Hz
15    500 mS/div     0.250Hz    32 Hz      64     x20   125 kHz     32    6249   20      ISR TCE0 1280Hz
16      1  S/div     0.125Hz    16 Hz      32     x40   125 kHz     32    6249   40      ISR TCE0 1280Hz
17      2  S/div     0.063Hz     8 Hz      16     x80   125 kHz     32    6249   80      ISR TCE0 1280Hz
18      5  S/div     0.025Hz   3.2 Hz     6.4    x200   125 kHz     32    6249   200     ISR TCE0 1280Hz
19     10  S/div     0.013Hz   1.6 Hz     3.2    x400   125 kHz     32    6249   400     ISR TCE0 1280Hz
20     20  S/div     0.006Hz   0.8 Hz     1.6    x800   125 kHz     32    6249   800     ISR TCE0 1280Hz
21     50  S/div     0.003Hz  0.32 Hz    0.64   x2000   125 kHz     32    6249   2000    ISR TCE0 1280Hz */

// milivolts or volts per pixels * 100000 / 32
// Range                      5.12V 2.56V 1.28V 0.64V  320mV  160mV   80mV
const int16_t milivolts[7] = { 1000,  500,  250,  125, 62500, 31250, 15625 };

// Maximum Frequency * 100000
const uint32_t freqval[22] PROGMEM = {
    // Kilo Hertz
    200000000,100000000,50000000,25000000,12500000,6250000,3200000,
    // Hertz
    1600000000,800000000,320000000,160000000,
    // Slow sampling
    160000000,64000000,32000000,16000000,6400000,3200000,
    1600000,  640000,  320000,  160000,  64000 };

const uint16_t timeval[22] PROGMEM = {   // = Time division * 10000 / (16*250)
    /* micro Seconds */
    200,400,800,1600,3200,6400,12500,
    /* mili Seconds */
    25,50,125,250,500,1250,2500,5000,12500,
    /* Seconds */
    25,50,125,250,500,1250 };

const char menustxt[][35] PROGMEM = {           // Menus:
    " CH ON   \0      GAIN-    \0   GAIN+",     // 0  Channel
    "LOGIC ON \0     SELECT   \0 PROTOCOL",     // 1  Logic
    " NORMAL   \0    SINGLE   \0 CIRCULAR",     // 2  Sniffer mode
    " NORMAL   \0    SINGLE   \0    AUTO ",     // 3  Trigger Type
    " VCURSOR \0   HCUR CH1  \0  HCUR CH2",     // 4  Cursor
    " HAMMING   \0    HANN   \0  BLACKMAN",     // 5  Spectrum Analyzer Window
    "  CH1    \0       CH2     \0  LOGIC ",     // 6  Trigger Source
    " GRID   \0  FLIP DISPLAY \0  INVERT ",     // 7  Display
    "  VDC      \0   V P-P  \0 FREQUENCY ",     // 8  Meter mode
    "  SINE    \0    SQUARE  \0  TRIANGLE",     // 9  AWG
    "POSITION   \0   INVERT   \0    MATH ",     // 10 Channel options
    "POSITION   \0   INVERT  \0   THICK0 ",     // 11 Logic options 1
    " CH1 \0          CH2 \0        LOGIC",     // 12 Menu Select 1 - Channel
    "TRIGTYPE  \0   TRIGSRC   \0 MORETRIG",     // 13 Menu Select 2 - Trigger
    " SCOPE    \0    METER   \0      FFT ",     // 14 Menu Select 3 - Mode
    " IQ FFT   \0  FFT WINDOW  \0    LOG ",     // 15 Menu Select 4 - FFT
    "CURSORS \0     DISPLAY  \0       AWG",     // 16 Menu Select 5 - Misc
    "WAVE TYPE  \0   SWEEP   \0 FREQUENCY",     // 17 AWG Menu 2
    " WINDOW   \0    EDGE    \0    SLOPE ",     // 18 Trigger Mode
    "  AUTO     \0   TRACK   \0 REFERENCE",     // 19 More Cursor Options
    "  ALL     \0    BIT_0    \0    BIT_1",     // 20 Logic Channel Select
    " BIT_2    \0    BIT_3    \0    BIT_4",     // 21 Logic Channel Select
    " BIT_5    \0    BIT_6    \0    BIT_7",     // 22 Logic Channel Select
    " EXT TRIG \0    BIT_0    \0    BIT_1",     // 23 Logic Trigger Select
    " PARALLEL  \0   SERIAL  \0      PULL",     // 24 Logic options 2
    " I2C      \0     UART    \0     SPI ",     // 25 Protocol
    " NO PULL  \0   PULL UP  \0 PULL DOWN",     // 26 Logic Inputs Pull
    " PERSISTENT  \0  LINE   \0     SHOW ",     // 27 Display
    " CPOL     \0    CPHA    \0 INVERT SS",     // 28 SPI Clock polarity and phase
    " SUBTRACT \0  MULTIPLY  \0  AVERAGE ",     // 29 Channel math
    "AMPLITUDE \0  DUTY CYCLE \0   OFFSET",     // 30 AWG Menu 3
    " ROLL     \0   ELASTIC  \0  XY MODE ",     // 31 Scope options
    "TRIGMODE  \0  POSTTRIG \0   TRIGHOLD",     // 32 Menu Trigger 2
    " EXP      \0    NOISE   \0   CUSTOM ",     // 33 AWG Menu 4
    " SPEED    \0    MODE    \0    RANGE ",     // 34 AWG Menu 5
    "SW FREQ    \0  SW AMP   \0  SW DUTY ",     // 35 AWG Menu 6
    "  DOWN    \0  PINGPONG   \0  ACCEL\0",     // 36 Sweep Mode Menu
    " IRDA     \0   1 WIRE    \0    MIDI ",     // 37 More Sniffer protocols
//    " SWEEP    \0  CV/GATE  \0 POS. RANGE",     // 38 Advanced Sweep Settings
//    "CV/GATE   \0  CONTINUOUS \0   C1=1V ",     // 39 CV/Gate Menu
};

const char menupoint[] PROGMEM = {  // Menu text table
    0,  // MCH1 Channel 1
    0,  // MCH2 Channel 2
    1,  // MCHD Logic
    2,  // MSNIFFER Sniffer mode
    3,  // MTRIGTYPE Trigger Type
    4,  // MCURSOR1 Cursor
    5,  // MWINDOW Spectrum Analyzer Window
    6,  // MSOURCE Trigger Source
    7,  // MDISPLAY1 Display
    8,  // MMETER Meter mode
    9,  // MAWG AWG
    10, // MCH1OPT Channel 1 options
    10, // MCH2OPT Channel 2 options
    11, // MCHDOPT1 Logic options 1
    12, // MMAIN1 Menu Select 1 - Channel
    13, // MMAIN2 Menu Select 2 - Trigger
    14, // MMAIN3 Menu Select 3 - Mode
    15, // MMAIN4 Menu Select 4 - FFT
    16, // MMAIN5 Menu Select 5 - Misc
    17, // MAWG2 AWG Menu 2
    33, // MAWG4 AWG Menu 4
    34, // MAWG5 AWG Menu 5
    35, // MAWG6 AWG Menu 6
//    37, // MAWG7 AWG Menu 7
//    38, // MCVG CV/Gate Menu
    31, // MSCOPEOPT Scope options
    32, // MTRIG2 Trigger menu 2
    18, // MTRIGMODE Trigger edge and mode
    19, // MCURSOR2 More Cursor Options
    20, // MCHDSEL1 Logic Channel Select
    21, // MCHDSEL2 Logic Channel Select
    22, // MCHDSEL3 Logic Channel Select
    23, // MTSEL1 Logic Trigger Select
    21, // MTSEL2 Logic Trigger Select
    22, // MTSEL3 Logic Trigger Select
    24, // MCHDOPT2 Logic options 2
    25, // MPROTOCOL Protocol
    37, // MPROTOCOL2 More Protocols
    26, // MCHDPULL Logic Inputs Pull
    27, // MDISPLAY2 Display
    28, // MSPI SPI Clock polarity and phase
    29, // MCH1MATH Channel 1 math
    29, // MCH2MATH Channel 2 math
    30, // MAWG3 AWG Menu 3
    36, // MSWMODE Sweep Mode Menu
};

const char Next[] PROGMEM = {  // Next Menu
//  Next:          Current:
    MMAIN1,     // Mdefault default
    MCH1OPT,    // MCH1 Channel 1
    MCH2OPT,    // MCH2 Channel 2
    MCHDOPT1,   // MCHD Logic
    Mdefault,   // MSNIFFER Sniffer mode
    MMAIN2,     // MTRIGTYPE Trigger Type
    MCURSOR2,   // MCURSOR1 Cursor
    MMAIN4,     // MWINDOW Spectrum Analyzer Window
    MMAIN2,     // MSOURCE Trigger Source
    Mdefault,   // MDISPLAY1 Display
    MMAIN3,     // MMETER Meter mode
    MAWG4,      // MAWG AWG
    Mdefault,   // MCH1OPT 1 Channel 1
    Mdefault,   // MCH2OPT 2 Channel 2
    MCHDOPT2,   // MCHDOPT1 3 Logic
    MMAIN2,     // MMAIN1 Menu Select 1 - Channel
    MMAIN3,     // MMAIN2 Menu Select 2 - Trigger
    MMAIN5,     // MMAIN3 Menu Select 3 - Mode
    MMAIN3,     // MMAIN4 Menu Select 4 - FFT
    Mdefault,   // MMAIN5 Menu Select 5 - Misc
    MAWG3,      // MAWG2 AWG Menu 2
    MAWG2,      // MAWG4 AWG Menu 4
    Mdefault,   // MAWG5 AWG Menu 5
    MAWG5,      // MAWG6 AWG Menu 6
//    Mdefault,   // MAWG7 AWG Menu 7
//    Mdefault,   // MCVG CV/Gate Menu
    MMAIN3,     // MSCOPEOPT Scope options
    Mdefault,   // MTRIG2 Trigger Menu 2
    MTRIG2,     // MTRIGMODE Trigger edge and mode
    Mdefault,   // MCURSOR2 More Cursor Options
    MCHDSEL2,   // MCHDSEL1 Logic Channel Select
    MCHDSEL3,   // MCHDSEL2 Logic Channel Select
    Mdefault,   // MCHDSEL3 Logic Channel Select
    MTSEL2,     // MTSEL1 Logic Trigger Select
    MTSEL3,     // MTSEL2 Logic Trigger Select
    Mdefault,   // MTSEL3 Logic Trigger Select
    Mdefault,   // MCHDOPT2 Decode
    MPROTOCOL2, // MPROTOCOL Protocol
    Mdefault,   // MPROTOCOL2 More Protocols
    Mdefault,   // MCHDPULL Logic Inputs Pull
    MDISPLAY1,  // MDISPLAY2 Display
    MSNIFFER,   // MSPI SPI Clock polarity and phase
    Mdefault,   // MCH1MATH Channel 1 math
    Mdefault,   // MCH2MATH Channel 2 math
    Mdefault,   // MAWG3 AWG Menu 3
    MAWG5,      // MSWMODE Sweep mode menu
    MSNIFFER,   // MUART UART Settings
    MTRIG2,     // MPOSTT Post Trigger
    MAWG2,      // MAWGFREQ Frequency
    MSOURCE,    // MTLEVEL Trigger Level
    MSOURCE,    // MTW1 Trigger Window 1
    MSOURCE,    // MTW2 Trigger Window 2
    MAWG5,      // MSW1 Sweep Start
    MAWG5,      // MSW2 Sweep End
    MMAIN1,     // MHPOS Run/Stop - Horizontal Scroll
    MAWG5,      // MSWSPEED Sweep Speed
    MAWG3,      // MAWGAMP Amplitude
    MAWG3,      // MAWGOFF Offset
    MAWG3,      // MAWGDUTY Duty Cycle
    MTRIG2,     // MTHOLD Trigger holdoff
    MCH1OPT,    // MCH1POS CH1 Channel position
    MCH2OPT,    // MCH2POS CH2 Channel position
    MCURSOR1,   // MVC1 V Cursor 1
    MCURSOR1,   // MVC2 V Cursor 2
    MCURSOR1,   // MCH1HC1 H Cursor 1 CH1
    MCURSOR1,   // MCH1HC2 H Cursor 2 CH1
    MCURSOR1,   // MCH2HC1 H Cursor 1 CH2
    MCURSOR1,   // MCH2HC2 H Cursor 2 CH2
};

const char gaintxt[][5] PROGMEM = {        // Gain Text with x1 probe
    "5.12", "2.56", "1.28", "0.64",             //  5.12,  2.56, 1.28, 0.64
    "0.32", "0.16", { '8', '0', 0x1A, 0x1B, 0 } //  0.32,  0.16,  80m, invalid
};

const char ratetxt[][5] PROGMEM = {
    { ' ', ' ', '8', 0x17, 0 },         // 8u
    { ' ', '1', '6', 0x17, 0 },         // 16u
    { ' ', '3', '2', 0x17, 0 },         // 32u
    { ' ', '6', '4', 0x17, 0 },         // 64u
    { '1', '2', '8', 0x17, 0 },         // 128u
    { '2', '5', '6', 0x17, 0 },         // 256u
    { '5', '0', '0', 0x17, 0 },         // 500u
    { ' ', '1',0x1A, 0x1B, 0 },         // 1m
    { ' ', '2',0x1A, 0x1B, 0 },         // 2m
    { ' ', '5',0x1A, 0x1B, 0 },         // 5m
    { '1', '0',0x1A, 0x1B, 0 },         // 10m
    { '2', '0',0x1A, 0x1B, 0 },         // 20m
    { '5', '0',0x1A, 0x1B, 0 },         // 50m
    " 0.1",                             // 0.1,
    " 0.2", " 0.5", "   1", "   2",     // 0.2, 0.5,    1,    2
    "   5", "  10", "  20", "  50"      //   5,  10,   20,   50
};

const char freqtxt[][5] PROGMEM = {
    "  1M", "500K", "250K", "125K",
    " 62K", " 31K", " 16K", "  8K",
    "  4K", "1.6K", " 800", " 800",
    " 320", " 160", "  80", "  32",
    "  16", "   8", " 3.2", " 1.6",
    " 0.8", "0.32"
};

const char baudtxt[][7] PROGMEM = {        // Baud text
    "1200  ", "2400  ", "4800  ", "9600  ",
    "19200 ", "38400 ", "57600 ", "115200",
};

const char shortcuts[][5] PROGMEM = {
    {   1,  16,  32,  48,   64 },   // MSWSPEED   1,     16,   32,   48,     64
    {   0, -32, -64, -96, -128 },   // MAWGAMP   0V,     1V,   2V,   3V,     4V
    { -64, -32,   0,  32,   64 },   // MAWGOFF  -1V,  -0.5V,   0V,   0.5V,   1V
    {   1,  64, 128, 192,  255 },   // MAWGDUTY  0.39%, 25%,   50%,  75%, 99.61%
    {   0,  64, 128, 192,  255 },   // MTHOLD    0ms,  64ms, 128ms, 192ms, 255ms
    {  64, 32, 0, -32, -64 },       // MCH1POS
    {  64, 32, 0, -32, -64 },   	// MCH2POS
};

const uint16_t movetable[] PROGMEM = {
    (uint16_t)&M.SWSpeed,
    (uint16_t)&M.AWGamp,
    (uint16_t)&M.AWGoffset,
    (uint16_t)&M.AWGduty,
    (uint16_t)&M.Thold,
    (uint16_t)&M.CH1pos,
    (uint16_t)&M.CH2pos,
};

static inline uint8_t average (uint8_t a, uint8_t b) {
    asm ("add  %0, %1"   "\n\t"
         "ror %0"
         : "+d" (a)
         : "r" (b));
    return a;
}

// Main MSO Application
void MSO(void) {
    uint8_t i,j,ypos;
    uint8_t temp1,temp2,temp3;
    uint16_t Tpost;
    int16_t AWGsweepi;              // AWG sweep counter
    uint8_t AWGspeed;               // AWG sweep increment
    uint8_t chdtrigpos;             // Digital channel trigger position
    const char *text;               // Pointer to constant text

    // Power reduction: Stop unused peripherals
    PR.PRGEN = 0x18;        // Stop: AES, EBI
    PR.PRPA  = 0x04;        // Stop: DAC
    PR.PRPB  = 0x01;        // Stop: AC
    PR.PRPC  = 0x7C;        // Stop: TWI, USART0, USART1, SPI, HIRES
    PR.PRPD  = 0x6C;        // Stop: TWI,       , USART1, SPI, HIRES
    PR.PRPE  = 0x6C;        // Stop: TWI,       , USART1, SPI, HIRES

    // Event to overflow RTC to timer F


    // Event System
    EVSYS.CH0MUX    = 0xE0;     // Event CH0 = TCE0 overflow used for ADC
    EVSYS.CH1MUX    = 0x20;     // Event CH1 = ADCA CH0 conversion complete
    //EVSYS.CH2MUX    = 0x5A;     // Event CH2 = PORTB Pin 2 (External Trigger)
    EVSYS.CH3MUX    = 0xD8;     // Event CH3 = TCD1 overflow used for DAC
    EVSYS.CH4MUX    = 0xC0;     // Event CH4 = TCC0 overflow used for freq. measuring
    EVSYS.CH5MUX    = 0xC8;     // Event CH5 = TCC1 overflow used for freq. measuring
    EVSYS.CH6MUX    = 0x8F;     // Event CH6 = CLKPER / 32768
    EVSYS.CH7MUX    = 0xD0;     // Event CH7 = TCD0 underflow

    // DAC
    DACB.CTRLB        = 0x22;   // CH1 auto triggered by an event
    DACB.CTRLC        = 0x11;   // Use AREFA (2.0V), data is left adjusted
    DACB.EVCTRL       = 0x03;   // Event CH3 triggers the DAC Conversion

    //DACB.CH0GAINCAL = eeprom_read_byte(&EEDACgain);      // Load DAC gain calibration
    //DACB.CH0OFFSETCAL = eeprom_read_byte(&EEDACoffset);  // Load DAC offset calibration
    DACB.CTRLA = 0x09;          // Enable DACB and CH1

    // DMA for DAC
    DMA.CH3.ADDRCTRL  = 0xD0;   // Reload after transaction, Increment source
    DMA.CH3.TRIGSRC   = 0x26;   // Trigger source is DACB CH1
    DMA.CH3.TRFCNT    = 256;    // AWG Buffer is 256 bytes
	DMA.CH3.SRCADDR0  = (((uint16_t) AWGBuffer)>>0*8) & 0xFF;
	DMA.CH3.SRCADDR1  = (((uint16_t) AWGBuffer)>>1*8) & 0xFF;
//	DMA.CH3.SRCADDR2  = 0;
	DMA.CH3.DESTADDR0 = (((uint16_t)(&DACB.CH1DATAH))>>0*8) & 0xFF;
	DMA.CH3.DESTADDR1 = (((uint16_t)(&DACB.CH1DATAH))>>1*8) & 0xFF;
//	DMA.CH3.DESTADDR2 = 0;
    DMA.CH3.CTRLA     = 0b10100100;     // Enable CH3, repeat mode, 1 byte burst, single
    DMA.CTRL          = 0x80;           // Enable DMA, single buffer, round robin

    old_s=Srate; old_g1=M.CH1gain; old_g2=M.CH2gain;
    Menu = Mdefault;                // Set default menu
    setbit(MStatus, update);        // Force second layer to update
    setbit(MStatus, updatemso);     // Apply settings
    setbit(MStatus, updateawg);     // Generate wave
    setbit(Key, userinput);         // To prevent the update bit to be cleared, on the first loop
    setbit(Misc, redraw);           // Clear logo
    Key=0;

    for(;;) {
		if(testbit(MStatus, updatemso)) Apply();
        if(testbit(MStatus, gosniffer)) {
            Key=0;  // Clear key before entering Sniffer
            clrbit(MStatus, stop);
            SaveEE();
            Sniff();
            Apply();    // Recover settings, particularly PORTC.PIN7CTRL
        }
        if(testbit(Misc,keyrep)) {  // Repeat key or long press
            if(testbit(Key,K1) && testbit(MStatus,stop)) AutoSet();       // Long press KA -> Autoset
            if ((Menu>=MPOSTT) && (testbit(Key,K2) || testbit(Key,K3))) {    // Repeat key
                setbit(Key, userinput);
            }
            if (testbit(Key,KM)) return;                    // Exit MSO
        }
///////////////////////////////////////////////////////////////////////////////
// Wait for trigger, start acquisition
        if(!testbit(MStatus, stop) &&      // MSO not stopped and
           !testbit(MStatus, triggered)) {    // Trigger not set already
			i=M.Thold;	// Trigger hold
            while(i!=0) {
                if(testbit(MStatus,update)) break;
                _delay_ms(1); i--;
            }
        	USARTE0.CTRLA = 0x00;           // Disable RX interrupt
            if(Srate<11) StartDMAs();
        	USARTE0.CTRLA = 0x20;           // Enable RX interrupt
            if(!(testbit(Trigger, normal) || testbit(Trigger, autotrg)) ||      // free trigger
            (testbit(Mcursors,roll) && Srate>=11)                               // roll mode in slow sampling
            || MFFT<0x20) {                                                     // or meter mode
                if(!testbit(MStatus, update)) setbit(MStatus, triggered);       // Set trigger
            }
            else {
                TCC1.CNT = 0;                   // TCC1 will count the post trigger samples
                TCC1.INTFLAGS = 0x01;           // Clear overflow flag
                Tpost = M.Tpost;                // Load number of samples to acquire after trigger
                if(Srate>1) Tpost=Tpost<<1;
                // Can't stop the ADC fast enough, so compensate
                if(Srate==0) Tpost-=8;
                else if(Srate==1) Tpost-=5;
                else if(Srate==2) Tpost-=5;
                else if(Srate<=5) Tpost-=4;
                else if(Tpost) Tpost--;
                TCC1.PERL = lobyte(Tpost);
                TCC1.PERH = hibyte(Tpost);
                TCC0.CNTL = TCC0.PERL;
                setbit(TCC0.INTFLAGS, TC2_LUNFIF_bp);    // Clear trigger timeout interrupt
                if(testbit(Trigger, autotrg)) TCC0.INTCTRLA |= TC2_LUNFINTLVL_LO_gc; // Enable Trigger timeout Interrupt
                uint8_t tlevelo;
				if(M.Tsource==0) { // // CH1 is trigger source
                    tlevelo=addwsat(M.Tlevel, -CH1.offset);
                    if(testbit(Trigger, window)) windowCH1(M.Window1,M.Window2);
                    else if(testbit(Trigger, slope)) {
                        tlevelo=M.Tlevel-0x80;
                        if(tlevelo>=128) tlevelo=-tlevelo;
                        if(tlevelo==0) tlevelo=1;
                        if(testbit(Trigger, trigdir)) slopedownCH1(-tlevelo);
                        else slopeupCH1(tlevelo);
                    }
                    else if(testbit(Trigger, edge)) {
                        // Apply CH1 offset to trigger level
                        if(testbit(Trigger, trigdir)) trigdownCH1(tlevelo);
                        else trigupCH1(tlevelo);
                    }
                    else {  // Dual edge trigger
                        if((int8_t)ADCA.CH0.RESL<(int8_t)(tlevelo-128)) trigdownCH1(tlevelo);
                        else trigupCH1(tlevelo);
                    }
				}
				else if(M.Tsource==1) {   // CH2 is trigger source
                    // Apply CH1 offset to trigger level
                    tlevelo=addwsat(M.Tlevel, -CH2.offset);
                    if(testbit(Trigger, window)) windowCH2(M.Window1,M.Window2);
                    else if(testbit(Trigger, slope)) {
                        tlevelo=M.Tlevel-0x80;
                        if(tlevelo>=128) tlevelo=255-tlevelo;
                        if(tlevelo==0) tlevelo=1;
                        if(testbit(Trigger, trigdir)) slopedownCH2(255-tlevelo);
                        else slopeupCH2(tlevelo);
                    }
                    else if(testbit(Trigger, edge)) {
                        if(testbit(Trigger, trigdir)) trigdownCH2(tlevelo);
                        else trigupCH2(tlevelo);
                    }
                    else {  // Dual edge trigger
                        if((int8_t)ADCB.CH0.RESL<(int8_t)(tlevelo-128)) trigdownCH2(tlevelo);
                        else trigupCH2(tlevelo);
                    }
				}
				else if(M.Tsource<=10) { // CHD and EXT trigger
                    if(testbit(Trigger, trigdir)) trigdownCHD(M.Tsource-2);
                    else trigupCHD(M.Tsource-2);
                }
            }
        }
        TCC0.INTCTRLA &= ~TC2_LUNFINTLVL_LO_gc; // Trigger timeout Interrupt not needed
///////////////////////////////////////////////////////////////////////////////
// Finish acquiring data
        if(testbit(MStatus, triggered)) {
            if(Srate<11) {
                int8_t   *q1, *q2, *q3;     // temp pointers to signed 8 bits
                uint8_t  *p1, *p2, *p3;     // temp pointers to unsigned 8 bits                
                uint16_t circular;          // Index of circular buffer                
                // Stop DMA trigger sources if in FREE mode
                _delay_us(500);             // 10ms/div may need time to complete one more sample
                TCE0.CTRLA = 0;
                ADCA.CTRLB = 0x14;          // signed mode, no free run, 8 bit
                ADCB.CTRLB = 0x14;          // signed mode, no free run, 8 bit
                // capture one last sample set
                DC.frame++;                  // Increase frame counter
                _delay_us(80);              // Wait to process ADC pipeline
                clrbit(MStatus, triggered);
                TCC1.CTRLA = 0;             // Stop post trigger counter
                // Done with fast sampling, disable DMAs and post trigger counter
                clrbit(DMA.CH0.CTRLA, 7);
                clrbit(DMA.CH2.CTRLA, 7);
                clrbit(DMA.CH1.CTRLA, 7);
                circular=512-DMA.CH0.TRFCNT;   // get index
///////////////////////////////////////////////////////////////////////////////
// Invert and adjust offset, apply channel math, loop thru circular buffer
                if(Srate<=1) {  // srate 0 and 1 only use the top half of the buffer
                    circular+=256;
                    if(circular>=512) circular=circular-512;
                }
                p1=DC.CH1data; p2=DC.CH2data; p3=DC.CHDdata;
                q1=(int8_t *)Temp.IN.CH1+circular;
                q2=(int8_t *)Temp.IN.CH2+circular;
                q3=(int8_t *)Temp.IN.CHD+circular;
                i=0;
                do {
                    uint8_t ch1raw, ch2raw, ch1end,ch2end;
                    *p3++ = *q3++;   // get Logic data
                    ch1raw=(*q1++);   // get CH1 signed data
                    ch2raw=(*q2++);   // get CH2 signed data
                    circular++;
                    if(circular==512) {  // Circular buffer
                        circular=0;
                        q1=(int8_t *)Temp.IN.CH1;
                        q2=(int8_t *)Temp.IN.CH2;
                        q3=(int8_t *)Temp.IN.CHD;
                    }
                    if(Srate>1) {
                        if(testbit(CH1ctrl,chaverage)) {
                            ch1raw=((int8_t)(ch1raw)>>1)+((int8_t)(*q1)>>1);
                        }
                        if(testbit(CH2ctrl,chaverage)) {
                            ch2raw=((int8_t)(ch2raw)>>1)+((int8_t)(*q2)>>1);
                        }
                        q1++; q2++; q3++; circular++;
                    }
                    if(circular==512) {  // Circular buffer
                        circular=0;
                        q1=(int8_t *)Temp.IN.CH1;
                        q2=(int8_t *)Temp.IN.CH2;
                        q3=(int8_t *)Temp.IN.CHD;
                    }
                    ch1end = saddwsat(ch1raw, CH1.offset);
                    if(testbit(CH1ctrl,chinvert)) ch1end = 255-ch1end;
                    ch2end = saddwsat(ch2raw, CH2.offset);
                    if(testbit(CH2ctrl,chinvert)) ch2end = 255-ch2end;
                    uint8_t tempch2;
                    temp1 = (int8_t)(ch1end-128);
                    tempch2 = (int8_t)(ch2end-128);
                    temp3=128+FMULS8(temp1,-(int8_t)tempch2);    // CH1*CH2
                    if(testbit(CH1ctrl,chmath)) {
                        if(testbit(CH1ctrl,submult)) ch1end=addwsat(ch1end,-(int8_t)tempch2); // CH1-CH2
                        else ch1end=temp3;    // CH1*CH2
                    }
                    if(testbit(CH2ctrl,chmath)) {
                        if(testbit(CH2ctrl,submult)) ch2end=addwsat(ch2end,-(int8_t)temp1); // CH2-CH1
                        else ch2end=temp3;    // CH1*CH2
                    }
                    if(testbit(Display,elastic)) {
                        *p1++=average(*p1,ch1end);
                        *p2++=average(*p2,ch2end);
                    }
                    else {
                        *p1++=ch1end;
                        *p2++=ch2end;
                    }
                } while (++i);
				if(testbit(Misc,autosend)) {
                    p1=DC.CH1data;
                    send(7);            // Start of frame: Send all three channels
	                for(uint16_t i16=0;i16<256*3;i16++) send(*p1++);
				}
				// USB - Send new data if previous transfer complete
				if((endpoints[1].in.STATUS & USB_EP_TRNCOMPL0_bm)) {
                    //RTC.CNT = 0;    // Prevent going to sleep if connected to USB
					endpoints[1].in.AUXDATA = 0;				// New transfer must clear AUXDATA
					endpoints[1].in.CNT = 770 | USB_EP_ZLP_bm;	// Send 256*3 bytes + frame, enable Auto Zero Length Packet
					endpoints[1].in.STATUS &= ~(USB_EP_TRNCOMPL0_bm | USB_EP_BUSNACK0_bm | USB_EP_OVF_bm);
				}
            }
///////////////////////////////////////////////////////////////////////////////
// Check slow sampling conditions
            else {
                if(testbit(Mcursors,roll)) {
                    clrbit(MStatus, triggered);
                    if(!testbit(MStatus,stop)) TCE0.INTCTRLA = 0x03;    // Enable sampling interrupt
                }
                else if(!testbit(Misc,sacquired)) TCE0.INTCTRLA = 0x03;  // Enable sampling interrupt
                else {    // Acquired complete buffer
                    clrbit(Misc, sacquired);
                    clrbit(MStatus, triggered);
                    DC.frame++;                  // Increase frame counter
                }
            }
			cli();
            // If single trigger, now go to stop
            if(testbit(Trigger, single)) {
                setbit(MStatus, stop);
                if(Srate<11 && Menu!=MTRIGTYPE) Menu=MHPOS;  // Horizontal Scroll
            }
            if(!testbit(Key, userinput)) clrbit(MStatus, update);
            sei();
        }
        clrbit(DMA.CH0.CTRLA, 7);
        clrbit(DMA.CH2.CTRLA, 7);
        clrbit(DMA.CH1.CTRLA, 7);
        if(!testbit(MStatus, triggered)) {
///////////////////////////////////////////////////////////////////////////////
// Erase old data
            if(!testbit(Display, persistent)) {
                if(((testbit(MFFT, fftmode) || testbit(MFFT, xymode))) ||
                (testbit(MFFT, scopemode) && (Srate<11 || testbit(Mcursors,roll))))
                clr_display();
            }
///////////////////////////////////////////////////////////////////////////////
// AWG sweep
            cli();
            if(Srate>=11 && testbit(Mcursors,roll) && !testbit(Misc,sacquired)) goto nosweep;
            clrbit(Misc,sacquired);   // Prevents frequency to change too fast in ROLL mode
            if(testbit(Sweep,SweepF)) {
                uint32_t freqv;
                freqv = pgm_read_dword_near(freqval+Srate)/4096;    // Sweep will have 256 * 16 steps
                if(Srate<=6) M.AWGdesiredF = (uint32_t)(AWGsweepi)*(freqv) / 2; // AWGSweepi max is 4095
                else M.AWGdesiredF = (uint32_t)(AWGsweepi)*(freqv) / 2000;
            }
            if(testbit(Sweep,SweepA)) M.AWGamp    = -(uint8_t)(AWGsweepi>>5);
            if(testbit(Sweep,SweepO)) M.AWGoffset =  (uint8_t)(AWGsweepi>>4)-128;
            if(testbit(Sweep,SweepD)) M.AWGduty   =  (uint8_t)(AWGsweepi>>4);
            int16_t sweepmin, sweepmax;
            int8_t sweepinc;
            if(AWGspeed==0) AWGspeed = M.SWSpeed;
            if(!testbit(Sweep,swdown)) sweepinc=AWGspeed; else sweepinc=-AWGspeed;
            sweepmin = (uint16_t)M.Sweep1*16;
            sweepmax = (uint16_t)M.Sweep2*16;
            AWGsweepi += sweepinc;
            if(AWGsweepi>sweepmax) {
                if(!testbit(Sweep,pingpong)) AWGsweepi = sweepmin;
                else AWGsweepi = sweepmax;
                setbit(Misc,bigfont);
            }
            else if(AWGsweepi<sweepmin) {
                if(!testbit(Sweep,pingpong)) AWGsweepi = sweepmax;
                else AWGsweepi=sweepmin;
                setbit(Misc,bigfont);
            }
            if(testbit(Sweep,SWAccel)) {    // Acceleration
                if(testbit(Sweep,SWAcceldir)) {
                    if(AWGspeed<127) AWGspeed++;
                }
                else {
                    if(AWGspeed>1) AWGspeed--;
                }
            }
            if(testbit(Misc,bigfont)) {  // Reached max or min
                AWGspeed = M.SWSpeed;
                if(testbit(Sweep,pingpong)) {
                    togglebit(Sweep,swdown);
                }
            }
            clrbit(Misc,bigfont);
            if(Sweep>=16) { // Sweep enabled
                setbit(MStatus, updateawg);
                if(testbit(Display,showset)) {
                    set_pixel(M.Sweep1>>1,MAX_Y);              // Min
                    set_pixel(M.Sweep2>>1,MAX_Y);              // Max
                    set_pixel((uint8_t)(AWGsweepi>>5),MAX_Y);  // Current
                }
            }
            nosweep:
///////////////////////////////////////////////////////////////////////////////
// Calculate min, max, peak to peak
            sei();
            uint8_t ch1max, ch1min, ch2max, ch2min;
            ch1min = ch1max = DC.CH1data[0];
            ch2min = ch2max = DC.CH2data[0];
            i=0; do {
                uint8_t curCH1,curCH2;
                curCH1=DC.CH1data[i]; curCH2=DC.CH2data[i];
                if(curCH1>ch1max) ch1max=curCH1;
                if(curCH1<ch1min) ch1min=curCH1;
                if(curCH2>ch2max) ch2max=curCH2;
                if(curCH2<ch2min) ch2min=curCH2;
            } while (++i);
            CH1.vpp = ch1max-ch1min;   // Peak to peak CH1
            CH2.vpp = ch2max-ch2min;   // Peak to peak CH2
            CH1.max=ch1max;
            CH1.min=ch1min;
            CH2.max=ch2max;
            CH2.min=ch2min;
            // Automatic cursors
            if(testbit(Mcursors,autocur) && testbit(MFFT, scopemode)) {
                AutoCursorV();
            }
        }
///////////////////////////////////////////////////////////////////////////////
// Display MSO data
        if(testbit(MFFT, scopemode)) {
            if(Srate<11 || testbit(Mcursors,roll)) {
                uint8_t k=0, prev=0;
                // Display new data
                if(Srate>=11 && testbit(Mcursors,roll)) j=(Index&0xFE)+1;    // clear last bit to prevent flicker
                else j=M.HPos;
                // i will scan display, j will scan data starting at M.HPos
                for(i=0; i<128; i++, k++, j++) {
                    uint8_t chdpos, chddata;
                    uint8_t och1,och2;
                    if(Srate>=11 && testbit(Mcursors,roll)) i=k>>1;
                    och1=temp1; och2=temp2;
                    chddata = DC.CHDdata[j];
                    // Show Digital Data
                    chdpos = M.CHDpos;
                    if(testbit(CHDctrl,chon)) {
                        uint8_t bitn,bitpos;
                        for(bitpos=128, bitn=7; bitpos; bitpos=bitpos>>1, bitn--) {
                            if(CHDmask&bitpos) {
                                if(i==0) {
                                    uint8_t chdpostext;
                                    chdpostext=chdpos>>3;
                                    lcd_goto(0,chdpostext);
                                    GLCD_Putchar('0'+bitn);     // Channel number
                                    if(M.Tsource-2==bitn && chdtrigpos<126) {     // Current bit is trigger source
                                        if(testbit(Trigger, normal) || testbit(Trigger, autotrg)) {
                                            lcd_goto(chdtrigpos,chdpostext);
                                            if(testbit(Trigger, trigdir)) GLCD_Putchar(0x19);
                                            else GLCD_Putchar(0x18);
                                        }
                                    }                                    
                                }
                                if(chddata&bitpos) set_pixel(i, chdpos);
                                else {
                                                    set_pixel(i, 6+chdpos);
                                    if(testbit(CHDctrl,low)) set_pixel(i, 5+chdpos);
                                }
                                chdpos+=8;
                            }
                        }
                    }
                    // Show Parallel Hex Display every 16 pixels if it fits
                    if(testbit(CHDctrl,hexp) && ((i&0x0F)==0) && chdpos<=56) {
                        lcd_goto(i,chdpos>>3);
                        printhex(chddata);
                    }
                    // Apply position
                    temp1=addwsat(DC.CH1data[j],M.CH1pos);
                    temp2=addwsat(DC.CH2data[j],M.CH2pos);
                    temp1=temp1>>1; // Scale to LCD (128x64)
                    temp2=temp2>>1; // Scale to LCD (128x64)
                    if(temp1>MAX_Y) temp1=MAX_Y;
                    if(temp2>MAX_Y) temp2=MAX_Y;
                    if(testbit(Display, line)) {
                        if(i==0) continue;
                        if((temp1!=och1) || (temp1 && och1<MAX_Y))
                            if(testbit(CH1ctrl,chon)) lcd_line(i, temp1, prev, och1);
                        if((temp2!=och2) || (temp2 && och2<MAX_Y))
                            if(testbit(CH2ctrl,chon)) lcd_line(i, temp2, prev, och2);
                    }
                    else {
                        // Don't draw when data==0 or data==MAX_Y, signal could be clipping
                        if(testbit(CH1ctrl,chon) && temp1 && temp1<MAX_Y) set_pixel(i, temp1);
                        if(testbit(CH2ctrl,chon) && temp2 && temp2<MAX_Y) set_pixel(i, temp2);
                    }
                    prev=i;
                }
                if(testbit(CHDctrl,hexs)) HEXSerial();
            }
        }
///////////////////////////////////////////////////////////////////////////////
// Display XY
        if(testbit(MFFT, xymode)) {
            uint8_t *p1,*p2;
            if(testbit(Display,showset)) tiny_printp(0,0,PSTR("XY MODE"));
            p1=DC.CH1data;
            p2=DC.CH2data;
            i=0; do {
                uint8_t y;
                if(Srate>=11 && !testbit(Mcursors,roll)) {
                    if(i==Index) break; // Don't display old data in slow sampling rate
                }
                temp1=255-(*p1++);
                y=(*p2++)/*-M.HPos*/;
                set_pixel(temp1>>1, y>>1); // Scale to 128
            } while(++i);
        }
///////////////////////////////////////////////////////////////////////////////
// Display Frequency Spectrum
        if(testbit(MFFT, fftmode)) {
            if(!testbit(MStatus, triggered)) {    // Data ready
                uint8_t divide, fft2pos;
                // Both channels on -> Show CH1 on upper half, CH2 on lower half of display
                if(testbit(CH1ctrl,chon) && testbit(CH2ctrl,chon)) {
                    temp1 = (MAX_Y/2)-4; 	// position CH1
                    fft2pos = (MAX_Y-8);	// position CH2
                    divide = 2;  // divide by 4
                }
                else {  // Only one channel on
                    temp1 = (MAX_Y-8);		// position CH1
                    fft2pos = (MAX_Y-8);	// position CH2
                    divide = 1;  // divide by 2
                }
                if(testbit(MFFT,iqfft)) {   // Display new FFT data
					uint8_t fftdata;
                    if(Display&0x03) {              // Grid
            			set_pixel(M.HPos,16);       // Vertical dot
    	        		set_pixel(M.HPos,32);       // Vertical dot
    			        set_pixel(M.HPos,48);       // Vertical dot
			        }
                    fft_stuff(NULL);
                    for(i=0; i<FFT_N/2; i++) {
				        fftdata=Temp.FFT.magn[(uint8_t)(i-M.HPos)]>>1;
						if(fftdata>(MAX_Y-8)) fftdata=(MAX_Y-8);
                        if(testbit(Display, line)) lcd_line(i, (MAX_Y-8)-fftdata, i, (MAX_Y-8));
                        else set_pixel(i, (MAX_Y-8)-fftdata);
                    }
                }
                else {
                    if(testbit(CH1ctrl,chon)) {     // Display new FFT data
    					uint8_t fftdata;
                        CH1.f=fft_stuff(DC.CH1data);
                        for(i=0,j=0; j<FFT_N/2; i++,j++) {
    				        fftdata=Temp.FFT.magn[j]>>divide;
							if(fftdata>temp1) fftdata=temp1;
                            if(testbit(Display, line)) lcd_line(i, temp1-fftdata, i, temp1);
                            else set_pixel(i, temp1-fftdata);
                        }
                    }
                    if(testbit(CH2ctrl,chon)) {
    					uint8_t fftdata;
                        CH2.f=fft_stuff(DC.CH2data);
                        // Display new FFT data
                        for(i=0,j=0; j<FFT_N/2; i++,j++) {
							fftdata=Temp.FFT.magn[j]>>divide;
							if(fftdata>temp1) fftdata=temp1;    
                            if(testbit(Display, line)) lcd_line(i, fft2pos-fftdata, i, fft2pos);
                            else set_pixel(i, fft2pos-fftdata);
                        }
                    }
                }
                // Automatic cursors
                if(testbit(Mcursors,autocur)) {
                    AutoCursorV();
                }
            }
        }
///////////////////////////////////////////////////////////////////////////////
// Display Multimeter
        if(MFFT<0x20) {
            if(!testbit(MStatus, triggered)) {  // Data ready
                if(adjusting==0) {              // Done adjusting, now show data
                    uint8_t *p1,*p2;
                    adjusting = 6;              // Re-init autosetup
                    clr_display();
                    tiny_printp(12,0, menustxt[12]+1);   // CH1 text
                    tiny_printp(76,0, menustxt[12]+16);  // CH2 Text
                    if(testbit(MStatus,vdc) || testbit(MStatus,vp_p)) {
                        // Always V Units in meter mode
                        tiny_printp(49,0,unitV);    // Display V units
                        tiny_printp(113,0,unitV);   // Display V units
                        // Display traces
                        p1=DC.CH1data;
                        p2=DC.CH2data;
                        for (i=0; i<64; i++) {
                            set_pixel(   i,28+((*p1++)>>3));
                            set_pixel(64+i,28+((*p2++)>>3));
                        }
                    }
                    else {
                        if(M.Tsource<2 || M.Tsource==10) {
                            EVSYS.CH2MUX    = 0x5A;     // Event CH2 = PORTB Pin 2 (External Trigger)
                            tiny_printp(16,3,  menustxt[23]+1);   // Ext Trigger text
                        }                            
                        else {
                            EVSYS.CH2MUX    = 0x60-2+M.Tsource;     // Event CH2 = PORTC Pin M.Tsource-2
                            tiny_printp(16,3,  menustxt[6]+28);
                            GLCD_Putchar('0'-2+M.Tsource);
                        }
                        tiny_printp(98,3, unitkHz);
                        if(Srate<=6) text = unitkHz;
                        else text = unitkHz+1;  // Hz: Use same text as kHz, with + 1 offset
                        tiny_printp(45,0,text);
                        tiny_printp(109,0,text);
                    }
                    Measurements();
                }
            }
        }
///////////////////////////////////////////////////////////////////////////////
// Send single point data
        if(testbit(Misc,slowacq)) {
            clrbit(Misc,slowacq);
			// USB - Send new data if previous transfer complete
			if((endpoints[1].in.STATUS & USB_EP_TRNCOMPL0_bm)) {
                //RTC.CNT = 0;    // Prevent going to sleep if connected to USB
                DC.index = Index;
				endpoints[1].in.AUXDATA = 0;				// New transfer must clear AUXDATA
				endpoints[1].in.CNT = 770 | USB_EP_ZLP_bm;	// Send 256*3 bytes + frame, enable Auto Zero Length Packet
				endpoints[1].in.STATUS &= ~(USB_EP_TRNCOMPL0_bm | USB_EP_BUSNACK0_bm | USB_EP_OVF_bm);
			}
            if(testbit(Misc,autosend) && Srate>=11) {    // Send to PC
                send(DC.CH1data[Index]);
                send(DC.CH2data[Index]);
                send(DC.CHDdata[Index]);
            }
        }
///////////////////////////////////////////////////////////////////////////////
// Auto setup
        if(adjusting) {
            uint8_t tempmfft, tempsrate, tempch1gain,tempch2gain;
            tempch1gain=M.CH1gain; tempch2gain=M.CH2gain;
            tempsrate= Srate;
			tempmfft = MFFT;
			clrbit(MFFT,uselog);
            clrbit(CH1ctrl,chmath);
            clrbit(CH2ctrl,chmath);
            CH1.f=fft_stuff(DC.CH1data);
            CH2.f=fft_stuff(DC.CH2data);
			MFFT=tempmfft;
            j=5;
            if(MFFT<0x20) {
                if(testbit(MStatus, vp_p) || testbit(MStatus, vdc)) j=3; // Limit maximum gain on meter mode when measuring V
            }
            checknext:
            if(M.CH1gain>j) M.CH1gain=j;  // Check maximum gain on CH1
            if(M.CH2gain>j) M.CH2gain=j;  // Check maximum gain on CH2
            // Check sampling rate
            if(Srate>10) Srate=10;
            switch(adjusting) {
                case 0: // Done adjusting
                    if(MFFT>=0x20) { // Not in meter mode
                        uint8_t center1, center2;
                        clrbit(Trigger, slope);
                        clrbit(Trigger, window);
                        setbit(Trigger, edge);
                        setbit(CH1ctrl,chon);
                        setbit(CH2ctrl,chon);
                        if(CH1.vpp<16) clrbit(CH1ctrl,chon);	// no signal at CH1, turn it off
                        if(CH2.vpp<16) clrbit(CH2ctrl,chon);	// no signal at CH2, turn it off
                        // If both channels are off, turn them on again
                        if(!testbit(CH1ctrl,chon) && !testbit(CH2ctrl,chon)) {
                            setbit(CH1ctrl,chon);
                            setbit(CH2ctrl,chon);
                        }
                        // Reset M.HPos and Tpre
                        M.HPos = 64;
                        M.Tpost=128;
                        // Determine trigger source
                        center1 = CH1.min + (CH1.vpp/2);
                        center2 = CH2.min + (CH2.vpp/2);
                        if(testbit(CH1ctrl,chon) && CH1.f>1 && (CH1.f>=CH2.f)) {
                            setbit(Trigger, autotrg);
                            M.Tsource = 0;
                            M.Tlevel = center1;
                            if(testbit(CH1ctrl,chinvert)) M.Tlevel=255-M.Tlevel;
                        }
                        else if(testbit(CH2ctrl,chon) && CH2.f>1) {
                            setbit(Trigger, autotrg);
                            M.Tsource = 1;
                            M.Tlevel = center2;
                            if(testbit(CH2ctrl,chinvert)) M.Tlevel=255-M.Tlevel;
                        }
                        if(testbit(MFFT, scopemode)) {
                            // If both channels, reduce gain and adjust positions
                            M.CH1pos = -center1/2;
                            M.CH2pos = -center2/2;
                            if(testbit(CH1ctrl,chon) && testbit(CH2ctrl,chon)) {
                                if(M.CH1pos>=-96) M.CH1pos-=32; else M.CH1pos=-128;
                                if(M.CH2pos< -32) M.CH2pos+=32; else M.CH2pos=0;
                                Reduce();
                            }
                            // Decrease gain some more to fit signal in display
                            Reduce();
                        }
                    }
                    if(MFFT>=0x20 || (testbit(MStatus, vdc) || testbit(MStatus, vp_p))) {
                        uint8_t autosrate;
                        if(CH1.f<CH2.f) autosrate=CH2.f;
                        else autosrate=CH1.f;
                        autosrate=autosrate/16;
                        if(Srate>autosrate) Srate-=autosrate;
                        else Srate=1;
                    }
                break;
                case 1: // Increase gain CH2
                    if((CH2.max<188 && CH2.min>73)  && M.CH2gain<j)      M.CH2gain++;
                break;
                case 2: // Decrease gain CH2
                    if((CH2.max>=250 || CH2.min <18) && M.CH2gain>0)     M.CH2gain--;
                break;
                case 3: // Increase gain CH1
                    if((CH1.max<188 && CH1.min>73) && M.CH1gain<j)       M.CH1gain++;
                break;
                case 4: // Decrease gain CH1
                    if ((CH1.max>=250 || CH1.min <18) && M.CH1gain>0)    M.CH1gain--;
                break;
                case 5: // Increase sampling rate
                    if((CH1.f>124 || CH2.f>124) && Srate>1) Srate--;
                break;
                case 6: // Decrease sampling rate
                    if(CH1.f<50 && CH2.f<50 && Srate<10) Srate++;
                break;
                case 7: // Increase gain CH2
                    if((CH2.max<188 && CH2.min>73)  && M.CH2gain<j)      M.CH2gain++;
                break;
                case 8: // Decrease gain CH2
                    if((CH2.max>=250 || CH2.min <18) && M.CH2gain>0)     M.CH2gain--;
                break;
                case 9: // Increase gain CH1
                    if((CH1.max<188 && CH1.min>73) && M.CH1gain<j)       M.CH1gain++;
                break;
                case 10: // Decrease gain CH1
                    if ((CH1.max>=250 || CH1.min <18) && M.CH1gain>0)    M.CH1gain--;
                break;
                case 11: // Start with fastest sampling rate;
                    Srate=1;
                break;
            }
            // Check if settings have changed
            if(tempch1gain!=M.CH1gain || tempch2gain!=M.CH2gain || tempsrate!=Srate) {
                setbit(MStatus, updatemso);    // Apply changes
            }
            else if(adjusting) { adjusting--; goto checknext; }
        }
///////////////////////////////////////////////////////////////////////////////
// Check User Input
        if(testbit(Key, userinput)) {
            uint8_t oldsource=M.Tsource;
            oldsource=M.Tsource;
            clrbit(Key, userinput);
            if(testbit(Key,KB)) {
                if(Menu==Mdefault) return; // Back key
                Menu=Mdefault;
            }                 
            // Check key inputs KA thru KD depending on the menu
            if(testbit(Key,KM)) {
                if(Menu==MMAIN5) SaveEE();       // Save MSO settings
                else if(Menu==MSNIFFER) setbit(MStatus, gosniffer);
                Menu=pgm_read_byte_near(Next+Menu); // Menu flow
            }
            // Shortcuts
            if(Menu>=MSWSPEED && Menu<=MCH2POS && testbit(Key,K1)) {
                uint8_t *pmove;                 // pointer for move- move+ menus                
                pmove = (uint8_t *)pgm_read_word(movetable+Menu-MSWSPEED);
                *pmove=pgm_read_byte_near((&shortcuts[0][0])+(shortcuti++)+5*(Menu-MSWSPEED));
                if(shortcuti>4) shortcuti=0;
            }
            switch(Menu) {
                case Mdefault:     // default menu
                    cli();   // Disable all interrupts
                    if(testbit(Key,K1)) {    // Next Channel
                        // Trigger is Single -> Enable one more trace
                        if(testbit(Trigger, single)) {
                            Index=0;
                            clrbit(MStatus, stop);
                        }
                        // Trigger is Normal or Free -> Toggle stop / run
                        else {
                            togglebit(MStatus, stop);
                            if(testbit(MStatus, stop)) {
                                clrbit(MStatus, update);    // prevent screen erase
                            }
                        }
                    }
                    if(testbit(Key,K2) && testbit(Key,K3)) M.HPos = 64;
                    else {
                        if(testbit(Key,K2)) {    // Sampling rate
                            if(!testbit(MStatus,stop)) {
                                if(Srate<21) Srate++;
                                else Srate=21;
                                i=0; do {
                                    DC.CH1data[i]=128;
                                    DC.CH2data[i]=128;
                                    DC.CHDdata[i]=0;
                                } while (++i);
                                setbit(Misc, redraw);
                                Index=0;
                                TCE0.INTCTRLA = 0;
                                clrbit(MStatus,triggered);
                            }
                        }
                        if(testbit(Key,K3)) {    // Sampling rate
                            if(!testbit(MStatus,stop)) {
                                if(Srate) Srate--;
                                i=0; do {
                                    DC.CH1data[i]=128;
                                    DC.CH2data[i]=128;
                                    DC.CHDdata[i]=0;
                                } while (++i);
                                setbit(Misc, redraw);
                                Index=0;
                                TCE0.INTCTRLA = 0;
                                clrbit(MStatus,triggered);
                            }
                        }
                    }
                    sei(); // Enable all interrupts
                break;
                case MCH1:     // Channel 1 menu
                    setbit(Misc, redraw);                
                    if(testbit(Key,K1)) togglebit(CH1ctrl,chon);    // Channel 1 on/off
                    if(testbit(Key,K2)) {    // Less gain
                        if(M.CH1gain) {
                            M.CH1gain--;
                            i=0; do {   // resize
                                DC.CH1data[i] = half(DC.CH1data[i]);
                            } while (++i);
                        }
                    }
                    if(testbit(Key, K3)) {    // More gain
                        if(M.CH1gain<6) {
                            M.CH1gain++;
                            i=0; do {   // resize
                                DC.CH1data[i] = twice(DC.CH1data[i]);
                            } while (++i);
                        }
                    }
                break;
                case MCH2:                     // Channel 2 menu
                    setbit(Misc, redraw);                
                    if(testbit(Key,K1)) togglebit(CH2ctrl,chon);    // Channel 2 on/off
                    if(testbit(Key,K2)) {    // Less gain
                        if(M.CH2gain) {
                            M.CH2gain--;
                            i=0; do {   // resize
                                DC.CH2data[i] = half(DC.CH2data[i]);
                            } while (++i);
                        }
                    }
                    if(testbit(Key,K3)) {    // More gain
                        if(M.CH2gain<6) {
                            M.CH2gain++;
                            i=0; do {   // resize
                                DC.CH2data[i] = twice(DC.CH2data[i]);
                            } while (++i);
                        }
                    }
                break;
                case MCHD:                     // Logic Analyzer menu
                    if(testbit(Key,K1)) togglebit(CHDctrl,chon);   // Logic on/off
                    if(testbit(Key,K2)) Menu = MCHDSEL1;            // Bit Select
                    if(testbit(Key,K3)) Menu = MPROTOCOL;           // Protocol Sniffer
                break;
                case MSNIFFER:      // Sniffer mode
                    if(testbit(Key,K1)) clrbit(Mcursors, singlesniff);    // Normal buffer
                    if(testbit(Key,K2)) setbit(Mcursors, singlesniff);    // Single buffer
                    if(testbit(Key,K3)) togglebit(Trigger, round);   // Circular buffer
                break;
                case MTRIGTYPE:     // Trigger Type
                    if(testbit(Key,K1)) {    // Trigger Normal
                        if(testbit(Trigger, normal) && !testbit(Trigger, single)) {
                            clrbit(Trigger, normal);
                        } else {
                            clrbit(Trigger,autotrg);
                            clrbit(Trigger,single);
                            setbit(Trigger, normal);
                        }
                        clrbit(MStatus, stop);
                    }
                    if(testbit(Key,K2)) {    // Trigger Single
                        clrbit(Trigger, autotrg);
                        setbit(Trigger, normal);
                        setbit(Trigger, single);
                        clrbit(MStatus, stop);
                    }
                    if(testbit(Key,K3)) {    // Trigger Auto
                        if(testbit(Trigger, autotrg)) {
                            clrbit(Trigger, autotrg);
                        } else {
                            setbit(Trigger,autotrg);
                            clrbit(Trigger,single);
                            clrbit(Trigger, normal);
                        }
                        clrbit(MStatus, stop);
                    }
                break;
                case MCURSOR1:     // Cursor menu
                    if(testbit(Key,K1)) {    // Vertical Cursors
                        if(testbit(Mcursors, cursorv)) {
                            clrbit(Mcursors, cursorv);
                        }
                        else {
                            setbit(Mcursors, cursorv);
                            Menu = MVC1;
                        }
                    }
                    if(testbit(Key,K2)) {    // CH1 Horizontal Cursors
                        togglebit(Mcursors, cursorh1);
                        if(testbit(Mcursors, cursorh1)) {
                            clrbit(Mcursors, cursorh2);
                            Menu = MCH1HC1;
                        }
                    }
                    if(testbit(Key,K3)) {    // CH2 Horizontal Cursors
                        togglebit(Mcursors, cursorh2);
                        if(testbit(Mcursors, cursorh2)) {
                            clrbit(Mcursors, cursorh1);
                            Menu = MCH2HC1;
                        }
                    }
                break;
                case MWINDOW:     // Spectrum Analyzer menu
                    if(testbit(Key,K1)) {    // Use Hamming Window
						if(testbit(MFFT,hamming)) clrbit(MFFT,hamming);
                        else {
                            setbit(MFFT, hamming);
                            clrbit(MFFT, hann);
                            clrbit(MFFT, blackman);
                        }
                    }
                    if(testbit(Key,K2)) {    // Use Hann Window
						if(testbit(MFFT,hann)) clrbit(MFFT,hann);
                        else {
                            clrbit(MFFT, hamming);
                            setbit(MFFT, hann);
                            clrbit(MFFT, blackman);
                        }
                    }
                    if(testbit(Key,K3)) {    // Use Cosine Window
						if(testbit(MFFT,blackman)) clrbit(MFFT,blackman);
                        else {
                            clrbit(MFFT, hamming);
                            clrbit(MFFT, hann);
                            setbit(MFFT, blackman);
                        }
                    }
                break;
                case MSOURCE:     // Trigger Source
                    if(testbit(Key,K1)) {    // Trigger source is CH1
                        M.Tsource = 0;
                        if(testbit(Trigger,window)) Menu=MTW1;
                        else Menu= MTLEVEL;
                    }
                    if(testbit(Key,K2)) {    // Trigger source is CH2
                        M.Tsource = 1;
                        if(testbit(Trigger,window)) Menu=MTW1;
                        else Menu= MTLEVEL;
                    }
                    if(testbit(Key,K3)) Menu = MTSEL1;  // Trigger source is LOGIC
                break;
                case MDISPLAY1:     // More Display Options
                    if(testbit(Key,K1)) {    // Grid type
                        if(testbit(Display,grid0)) togglebit(Display,grid1);
                        togglebit(Display,grid0);
                    }
                    if(testbit(Key,K2)) togglebit(Display, flip);    // Flip Display
                    if(testbit(Key,K3)) togglebit(Display,disp_inv);    // Invert
                break;
                case MMETER:     // Voltmeter menu
                    if(testbit(Key,K1)) {       // VDC
                        setbit(MStatus,vdc);
                        clrbit(MStatus,vp_p);
                    }
                    if(testbit(Key,K2)) {       // VPP
                        clrbit(MStatus,vdc);
                        setbit(MStatus,vp_p);
                    }
                    if(testbit(Key,K3)) {       // FREQUENCY
                        clrbit(MStatus,vdc);
                        clrbit(MStatus,vp_p);
                    }
                break;
                case MAWG:    // AWG Control
                    if(testbit(Key,K1)) M.AWGtype = 1; // Sine
                    if(testbit(Key,K2)) M.AWGtype = 2; // Square
                    if(testbit(Key,K3)) M.AWGtype = 3; // Triangle
                    setbit(MStatus, updateawg);
                break;
                case MCH1OPT:    // CH1 Menu 2
                    if(testbit(Key,K1)) Menu = MCH1POS;  // CH1 Position
                    if(testbit(Key,K2)) {   // Invert Channel
                        togglebit(CH1ctrl,chinvert);
                        setbit(Misc, redraw);
                    }
                    if(testbit(Key,K3)) Menu = MCH1MATH;    // Math
                break;
                case MCH2OPT:    // CH2 Menu 2
                    if(testbit(Key,K1)) Menu=MCH2POS;   // CH2 Position
                    if(testbit(Key,K2)) {   // Invert Channel
                        togglebit(CH2ctrl,chinvert);
                        setbit(Misc, redraw);
                    }
                    if(testbit(Key,K3)) Menu=MCH2MATH;  // Math
                break;
                case MCHDOPT1:    // Logic Analyzer Options
                    if(testbit(Key,K1)) M.CHDpos+=8;              // Logic Position
                    if(testbit(Key,K2)) togglebit(CHDctrl,chinvert);    // Invert Channel
                    if(testbit(Key,K3)) togglebit(CHDctrl,low);         // Thick line when logic '0'
                break;
                case MMAIN1:     // Menu Select 1: Channel
                    if(testbit(Key,K1)) Menu = MCH1;     // CH1
                    if(testbit(Key,K2)) Menu = MCH2;     // CH2
                    if(testbit(Key,K3)) Menu = MCHD;     // LOGIC
                break;
                case MMAIN2:     // Menu Select 2: Trigger
                    if(testbit(Key,K1)) Menu = MTRIGTYPE;   // Trigger Type
                    if(testbit(Key,K2)) Menu = MSOURCE;     // Trigger Source
                    if(testbit(Key,K3)) Menu = MTRIG2;      // Post Trigger
                break;
                case MMAIN3:     // Menu Select 3: Mode
                    // Check Tactile Switches
                    temp1=MFFT;
        		    if(MFFT<0x20) RestorefromMeter();
                    if(testbit(Key,K1) && testbit(Key,K3)) { // Scope or XY + FFT
                        if(testbit(MFFT,xymode)) {
                            clrbit(MFFT,scopemode);
                            setbit(MFFT,xymode);
                            setbit(MFFT,fftmode);
                        }
                        else {
                            setbit(MFFT,scopemode);
                            clrbit(MFFT,xymode);
                            setbit(MFFT,fftmode);
                        }
                        while(Key);
                    }
                    else {
                        if(testbit(Key,K1)) {   // Set Scope mode
                            if(testbit(MFFT,fftmode) || !testbit(MFFT,xymode)) {
                                setbit(MFFT,scopemode);
                                clrbit(MFFT,xymode);
                                clrbit(MFFT,fftmode);
                            }
                            Menu=MSCOPEOPT;
                        }
                        if(testbit(Key,K2)) {   // Set Meter mode
                            clrbit(MFFT,scopemode);
                            clrbit(MFFT,xymode);
                            clrbit(MFFT,fftmode);
                            Menu=MMETER;
                        }
                        if(testbit(Key,K3)) {   // Set FFT mode
                            clrbit(MFFT,scopemode);
                            clrbit(MFFT,xymode);
                            setbit(MFFT,fftmode);
                            Menu=MMAIN4;
                        }
                    }
					if(MFFT<0x20) GoingtoMeter();
                    if(temp1!=MFFT) setbit(Misc,redraw);    // Mode changed
                break;
                case MMAIN4:     // Menu Select 4: FFT
                    if(testbit(Key,K1)) togglebit(MFFT, iqfft);     // Set IQ FFT
                    if(testbit(Key,K2)) Menu=MWINDOW;               // FFT Window Menu
                    if(testbit(Key,K3)) togglebit(MFFT, uselog);    // Use logarithmic display
                break;
                case MMAIN5:     // Menu Select 5: Miscellaneous
                    if(testbit(Key,K1)) Menu=MCURSOR1;       // Cursors
                    if(testbit(Key,K2)) Menu=MDISPLAY2;      // Display Menu
                    if(testbit(Key,K3)) Menu=MAWG2;          // AWG Menu
                break;
                case MAWG2:     // AWG Menu 2
                    if(testbit(Key,K1)) Menu = MAWG;         // Waveform Type
                    if(testbit(Key,K2)) Menu = MAWG6;        // Go to Advanced Settings
                    if(testbit(Key,K3)) Menu = MAWGFREQ;     // Frequency
                break;
                case MAWG4:     // AWG Menu 4
                    if(testbit(Key,K1)) M.AWGtype = 4;  // Exponential
                    if(testbit(Key,K2)) M.AWGtype = 0;  // Noise
                    if(testbit(Key,K3)) M.AWGtype = 5;  // Custom
                    setbit(MStatus, updateawg);
                break;
                case MAWG5:     // AWG Menu 5
                    if(testbit(Key,K1)) Menu=MSWSPEED;   // Sweep Menu
                    if(testbit(Key,K2)) Menu=MSWMODE;    // Sweep Mode Menu
                    if(testbit(Key,K3)) Menu=MSW1;       // Range Menu
                break;
                case MAWG6:     // AWG Menu 6
                    if(testbit(Key,K1)) togglebit(Sweep,SweepF);    // Toggle F sweep
                    if(testbit(Key,K2)) togglebit(Sweep,SweepA);    // Toggle A sweep
                    if(testbit(Key,K3)) togglebit(Sweep,SweepD);    // Toggle D sweep
                break;
/*                case MAWG7:     // AWG Menu 7
                    if(testbit(Key,KA)) Menu = MAWG6;   // Go to Sweep menu
                    if(testbit(Key,KB)) Menu = MCVG;    // Go to CV/Gate
                    if(testbit(Key,KC)) togglebit(MStatus,AWGPositive);     // Positive Range
                break;
                case MCVG:     // CV/Gate Menu
                    if(testbit(Key,KA)) Menu = MCVG;  // Go to CV/Gate
                    if(testbit(Key,KB)) Menu = MAWG6;   // Go to Sweep menu
                    togglebit(MStatus,AWGPositive);     // Positive Range*/
                break;
                case MSCOPEOPT:
                    if(testbit(Key,K1)) {   // Roll
                        togglebit(Mcursors,roll);
                        if(testbit(Mcursors,roll)) clrbit(Display,elastic);
                    }
                    if(testbit(Key,K2)) {   // Elastic
                        togglebit(Display, elastic);
                        if(testbit(Display,elastic)) clrbit(Mcursors,roll);
                    }
                    if(testbit(Key,K3)) {   // Toggle XY Mode
                        setbit(Misc, redraw);
                        if(testbit(MFFT,xymode)) {
                            setbit(MFFT,scopemode);
                            clrbit(MFFT,xymode);
                        }
                        else {
                            clrbit(MFFT,scopemode);
                            setbit(MFFT,xymode);
                        }
                    }
                break;
                case MTRIG2: // Trigger edge and mode
                    if(testbit(Key,K1)) Menu = MTRIGMODE;    // Trigger mode
                    if(testbit(Key,K2)) Menu = MPOSTT;       // Post trigger menu
                    if(testbit(Key,K3)) Menu = MTHOLD;       // Trigger holdoff menu
                break;
                case MTRIGMODE: // Trigger mode
                    if(testbit(Key,K1)) {   // Set Window Mode
						if(testbit(Trigger,window)) clrbit(Trigger,window);
						else {
    						clrbit(Trigger, edge);
    						clrbit(Trigger, slope);
    						setbit(Trigger, window);
						}
                    }
                    if(testbit(Key,K2)) {   // Set Edge Mode
						if(testbit(Trigger,edge)) clrbit(Trigger,edge);
						else {
    						setbit(Trigger, edge);
    						clrbit(Trigger, slope);
    						clrbit(Trigger, window);
						}
                    }
                    if(testbit(Key,K3)) {   // Set Slope Mode
						if(testbit(Trigger,slope)) clrbit(Trigger,slope);
						else {
    						clrbit(Trigger, edge);
    						setbit(Trigger, slope);
    						clrbit(Trigger, window);
						}
                    }
                break;
                case MCURSOR2:     // More Cursors Options
                    if(testbit(Key,K1)) togglebit(Mcursors, autocur);   // Auto Cursors
                    if(testbit(Key,K2)) togglebit(Mcursors, track);     // Track Vertical Cursors
                    if(testbit(Key,K3)) {   // Show Reference
                        togglebit(Mcursors, reference);
                        if(testbit(Mcursors, reference)) {
                            // Save waveform to EEPROM
                            tiny_printp(50,4,PSTR("SAVING...")); dma_display();
                            i=0;
                            do {     // Apply position
                                uint8_t eech1,eech2;
                                eech1=addwsat(DC.CH1data[i],M.CH1pos);
                                eech1=eech1>>1; // Scale to LCD (128x64)
                                if(eech1>=64) eech1=63;
                                eech2=addwsat(DC.CH2data[i],M.CH2pos);
                                eech2=eech2>>1; // Scale to LCD (128x64)
                                if(eech2>=64) eech2=63;
                                eeprom_write_byte(&EECHREF1[i], eech1);
                                eeprom_write_byte(&EECHREF2[i], eech2);
                            } while(++i);
                        }
                    }
                break;
                case MCHDSEL1:     // Logic bit select
                    M.CHDpos = 0;
                    if(testbit(Key,K1)) {   // Select All / None
                        if(CHDmask==0xFF) CHDmask=0;
                        else CHDmask = 0xFF;
                    }
                    if(testbit(Key,K2)) togglebit(CHDmask, 0);
                    if(testbit(Key,K3)) togglebit(CHDmask, 1);
                break;
                case MCHDSEL2:     // Logic bit select
                    M.CHDpos = 0;
                    if(testbit(Key,K1)) togglebit(CHDmask, 2);
                    if(testbit(Key,K2)) togglebit(CHDmask, 3);
                    if(testbit(Key,K3)) togglebit(CHDmask, 4);
                break;
                case MCHDSEL3:     // Logic bit select
                    M.CHDpos = 0;
                    if(testbit(Key,K1)) togglebit(CHDmask, 5);
                    if(testbit(Key,K2)) togglebit(CHDmask, 6);
                    if(testbit(Key,K3)) togglebit(CHDmask, 7);
                break;
                case MTSEL1:     // Digital Trigger source
                    if(testbit(Key,K1)) M.Tsource = 10;    // External Trigger
                    if(testbit(Key,K2)) M.Tsource = 2;     // 0
                    if(testbit(Key,K3)) M.Tsource = 3;     // 1
                    if(!testbit(Key,KM) && (oldsource==M.Tsource)) togglebit(Trigger, trigdir);
                break;
                case MTSEL2:     // Digital Trigger source
                    if(testbit(Key,K1)) M.Tsource = 4;     // 2
                    if(testbit(Key,K2)) M.Tsource = 5;     // 3
                    if(testbit(Key,K3)) M.Tsource = 6;     // 4
                    if(!testbit(Key,KM) && (oldsource==M.Tsource)) togglebit(Trigger, trigdir);                    
                break;
                case MTSEL3:     // Digital Trigger source
                    if(testbit(Key,K1)) M.Tsource = 7;     // 5
                    if(testbit(Key,K2)) M.Tsource = 8;     // 6
                    if(testbit(Key,K3)) M.Tsource = 9;     // 7
                    if(!testbit(Key,KM) && (oldsource==M.Tsource)) togglebit(Trigger, trigdir);                    
                break;
                case MCHDOPT2:                     // Logic Options 2
                    if(testbit(Key,K1)) togglebit(CHDctrl,hexp);     // Parallel
                    if(testbit(Key,K2)) togglebit(CHDctrl,hexs);     // Serial
                    if(testbit(Key,K3)) Menu=MCHDPULL;               // Pull Resistors
                break;
                case MPROTOCOL:                     // Protocol Decoding
                    if(testbit(Key,K1)) {   // I2C
						M.CHDdecode = i2c;
                        Menu=MSNIFFER;
                    }
                    if(testbit(Key,K2)) {   // UART
						M.CHDdecode = rs232;
                        Menu=MUART;
                    }
                    if(testbit(Key,K3)) {   // SPI
						M.CHDdecode = spi;
                        Menu=MSPI;
                    }
                break;
                case MPROTOCOL2:                     // Protocol Decoding
                    if(testbit(Key,K1)) {   // IRDA
                        M.CHDdecode = irda;
                        Menu=MSNIFFER;
                    }
                    if(testbit(Key,K2)) {   // 1WIRE
                        M.CHDdecode = onewire;
                        Menu=MSNIFFER;
                    }
                    if(testbit(Key,K3)) {   // MIDI
                        M.CHDdecode = midi;
                        Menu=MSNIFFER;
                    }
                break;
                case MCHDPULL:     // Logic Input Pull
                    if(testbit(Key,K1)) clrbit(CHDctrl,pull);   // No Pull
                    if(testbit(Key,K2)) {   // Pull Up
                        setbit(CHDctrl,pull);
                        setbit(CHDctrl,pullup);
                    }
                    if(testbit(Key,K3)) {   // Pull Down
                        setbit(CHDctrl,pull);
                        clrbit(CHDctrl,pullup);
                    }
                break;
                case MDISPLAY2:     // Display menu
                    if(testbit(Key,K1)) {   // Persistent mode
                        togglebit(Display, persistent);
                        //if(!testbit(Mcursors,roll)) Index=0;
                    }
                    if(testbit(Key,K2)) togglebit(Display, line);        // Line mode
                    if(testbit(Key,K3)) togglebit(Display, showset);     // Show channel settings
                break;
                case MSPI:    // SPI Menu
                    if(testbit(Key,K1)) togglebit(Sniffer,CPOL);        // Toggle CPOL
                    if(testbit(Key,K2)) togglebit(Sniffer,CPHA);        // Toggle CPHA
                    if(testbit(Key,K3)) togglebit(Sniffer,SSINV);        // Toggle CPHA
                break;
                case MCH1MATH:    // Channel 1 math
                    if(testbit(Key,K1)) {   // Subtract
                        if(testbit(CH1ctrl,chmath)) {
                            togglebit(CH1ctrl,submult);
                            if(!testbit(CH1ctrl,submult)) clrbit(CH1ctrl,chmath);
                        }
                        else {
                            setbit(CH1ctrl,chmath);
                            setbit(CH1ctrl,submult);
                        }
                    }
                    if(testbit(Key,K2)) {   // Multiply
                        if(testbit(CH1ctrl,chmath)) {
                            togglebit(CH1ctrl,submult);
                            if(testbit(CH1ctrl,submult)) clrbit(CH1ctrl,chmath);
                        }
                        else {
                            setbit(CH1ctrl,chmath);
                            clrbit(CH1ctrl,submult);
                        }
                    }
                    if(testbit(Key,K3)) togglebit(CH1ctrl,chaverage);   // Average
                break;
                case MCH2MATH:    // Channel 2 math
                    if(testbit(Key,K1)) {   // Subtract
                        if(testbit(CH2ctrl,chmath)) {
                            togglebit(CH2ctrl,submult);
                            if(!testbit(CH2ctrl,submult)) clrbit(CH2ctrl,chmath);
                        }
                        else {
                            setbit(CH2ctrl,chmath);
                            setbit(CH2ctrl,submult);
                        }
                    }
                    if(testbit(Key,K2)) {   // Multiply
                        if(testbit(CH2ctrl,chmath)) {
                            togglebit(CH2ctrl,submult);
                            if(testbit(CH2ctrl,submult)) clrbit(CH2ctrl,chmath);
                        }
                        else {
                            setbit(CH2ctrl,chmath);
                            clrbit(CH2ctrl,submult);
                        }
                    }
                    if(testbit(Key,K3)) togglebit(CH2ctrl,chaverage);   // Average
                break;
                case MAWG3:     // AWG Menu 3
                    if(testbit(Key,K1)) Menu=MAWGAMP;    // Amplitude
                    if(testbit(Key,K2)) Menu=MAWGDUTY;   // Duty Cycle
                    if(testbit(Key,K3)) Menu=MAWGOFF;    // Offset
                break;
                case MSWMODE:
                    if(testbit(Key,K1)) togglebit(Sweep,swdown);    // Sweep direction
                    if(testbit(Key,K2)) togglebit(Sweep,pingpong);   // Ping Pong
                    if(testbit(Key,K3)) {   // Accelerate up or accelerate down
                        if(testbit(Sweep,SWAccel) && testbit(Sweep, SWAcceldir)) {
                            clrbit(Sweep,SWAccel);
                            clrbit(Sweep, SWAcceldir);
                        }
                        else if(testbit(Sweep,SWAccel)) setbit(Sweep,SWAcceldir);
                        else setbit(Sweep,SWAccel);
                    }
                break;
                case MUART:    // Baud Rate Menu 1
                    if(testbit(Key,K1)) {   // Change Baud Rate
                        uint8_t baud;
                        baud=Sniffer&0x1F;
                        baud++;
                        baud=baud&0x1F;
                        Sniffer&=0xE0;
                        Sniffer|=baud;
                    }
                    if(testbit(Key,K2)) {   // Parity
                        if(!testbit(Sniffer,parmode)) {
                            setbit(Sniffer,parmode);
                            clrbit(Sniffer,parity);
                        }
                        else {
                            if(!testbit(Sniffer,parity)) setbit(Sniffer,parity);
                            else clrbit(Sniffer,parmode);
                        }
                    }
                    if(testbit(Key,K3)) togglebit(Sniffer,stopbit);    // Stop Bits
                break;
                case MPOSTT:
                    Tpost = M.Tpost;
                    if(testbit(Key,K1)) {   // Shortcut values
                        if(Tpost == 128) Tpost = 256;
                        else if(Tpost == 256) Tpost = 0;
                        else Tpost = 128;
                    }
                    if(testbit(Key,K2)) { if(Tpost) Tpost--; }
                    if(testbit(Key,K3)) Tpost++;
                    M.Tpost=Tpost;
                    CheckPost();
                break;
                case MAWGFREQ:     // Frequency
                    if(testbit(Key,K1)) moveF();    // Shortcuts: 10KHz, 1KHz, 100Hz, 10Hz, 1Hz
                    if(testbit(Key,K2)) {
                        setbit(Misc, negative);    // decrease
                        moveF();
                    }
                    if(testbit(Key,K3)) {
                        setbit(Misc, bigfont);      // increase
                        moveF();
                    }
                    clrbit(Misc, negative);
                    clrbit(Misc, bigfont);
                    setbit(MStatus, updateawg);
                break;
                case MTLEVEL:     // Trigger Level
                    if(testbit(Key,K1)) {   // Shortcut to 0V or average
                        if(M.Tlevel==128) {
                            if(M.Tsource==0) {
                                M.Tlevel = CH1.min + (CH1.vpp/2);
                                if(testbit(CH1ctrl,chinvert)) M.Tlevel=255-M.Tlevel;
                            }
                            else if(M.Tsource==1) {
                                M.Tlevel = CH2.min + (CH2.vpp/2);
                                if(testbit(CH2ctrl,chinvert)) M.Tlevel=255-M.Tlevel;
                            }
                        }
                        else M.Tlevel=128;
                    }
                    if(testbit(Key,K2)) {   // decrease
                        if(M.Tlevel<255) M.Tlevel++;
                        setbit(Trigger,trigdir);
                    }
                    if(testbit(Key,K3)) {  // increase
                        if(M.Tlevel) M.Tlevel--;
                        clrbit(Trigger,trigdir);
                    }
                break;
                case MTW1:     // Window Level 1
                    if(testbit(Key,K1)) Menu=MTW2;    // Select Cursor
                    if(testbit(Key,K2)) { if(M.Window1<254) M.Window1+=2; }
                    if(testbit(Key,K3)) {
                        if(M.Window1) M.Window1--;
                        if(M.Window1) M.Window1--;
                    }
                break;
                case MTW2:     // Window Level 1
                    if(testbit(Key,K1)) Menu=MTW1;    // Select Cursor
                    if(testbit(Key,K2)) { if(M.Window2<254) M.Window2+=2; }
                    if(testbit(Key,K3)) {
                        if(M.Window2) M.Window2--;
                        if(M.Window2) M.Window2--;
                    }
                break;
                case MSW1:     // Sweep start frequency
                    if(testbit(Key,K1)) Menu=MSW2;    // Select Cursor
                    if(testbit(Key,K2)) { if(M.Sweep1)     M.Sweep1--; }
                    if(testbit(Key,K3)) { if(M.Sweep1<255) M.Sweep1++; }
                break;
                case MSW2:     // Sweep end frequency
                    if(testbit(Key,K1)) Menu=MSW1;    // Select Cursor
                    if(testbit(Key,K2)) { if(M.Sweep2)     M.Sweep2--; }
                    if(testbit(Key,K3)) { if(M.Sweep2<255) M.Sweep2++; }
                break;
                case MHPOS:     // Stop - Horizontal Scroll
                    if(testbit(Key,K2) && testbit(Key,K3)) M.HPos = 64;   // KB and KC pressed simultaneously
                    else {
                        if(testbit(Key,K1)) {   // Start acquisition
                            clrbit(MStatus, stop);
                            Menu=Mdefault;
                        }
                        if(testbit(Key,K2)) { if(M.HPos) M.HPos--; }
                        if(testbit(Key,K3)) M.HPos++;
                    }
                break;
                case MSWSPEED:  // Sweep speed
                    if(testbit(Key,K2)) { if(M.SWSpeed>1)    M.SWSpeed--; }
                    if(testbit(Key,K3)) { if(M.SWSpeed<127)  M.SWSpeed++; }
                    AWGspeed=M.SWSpeed;
                break;
                case MAWGAMP:     // Amplitude
                    if(testbit(Key,K2)) M.AWGamp++;   // Decrease
                    if(testbit(Key,K3)) { if(M.AWGamp>-128) M.AWGamp--; }  // Increase
                    setbit(MStatus, updateawg);
                break;
                case MAWGOFF:     // Offset
                    if(testbit(Key,K2)) { if(M.AWGoffset<127) M.AWGoffset++;  }   // decrease
                    if(testbit(Key,K3)) { if(M.AWGoffset>-128) M.AWGoffset--; }   // increase
                    setbit(MStatus, updateawg);
                break;
                case MAWGDUTY:     // Duty Cycle
                    if(testbit(Key,K2)) { if(M.AWGduty>0)   M.AWGduty--; }    // decrease
                    if(testbit(Key,K3)) { if(M.AWGduty<255) M.AWGduty++; }    // increase
                    setbit(MStatus, updateawg);
                break;
                case MTHOLD:
                    if(testbit(Key,K2)) { if(M.Thold)     M.Thold--; }
                    if(testbit(Key,K3)) { if(M.Thold<255) M.Thold++; }
                break;
                case MCH1POS:     // CH1 Position
                    if(testbit(Key,K2)) M.CH1pos+=2;
                    if(testbit(Key,K3)) {
                        if(M.CH1pos>-128) M.CH1pos--;
                        if(M.CH1pos>-128) M.CH1pos--;
                    }
                break;
                case MCH2POS:     // CH2 Position
                    if(testbit(Key,K2)) M.CH2pos+=2;
                    if(testbit(Key,K3)) {
                        if(M.CH2pos>-128) M.CH2pos--;
                        if(M.CH2pos>-128) M.CH2pos--;
                    }
                break;
                case MVC1:     // V Cursor 1
                    if(testbit(Key,K1)) Menu=MVC2;   // Select Cursor
                    if(testbit(Key,K2)) {
                        clrbit(Mcursors, autocur);
                        if(M.VcursorA) M.VcursorA--;
                    }
                    if(testbit(Key,K3)) {
                        clrbit(Mcursors, autocur);
                        M.VcursorA++;
                    }
                break;
                case MVC2:     // V Cursor 2
                    if(testbit(Key,K1)) Menu=MVC1;    // Select Cursor
                    if(testbit(Key,K2)) {
                        clrbit(Mcursors, autocur);
                        if(M.VcursorB) M.VcursorB--;
                    }
                    if(testbit(Key,K3)) {
                        clrbit(Mcursors, autocur);
                        M.VcursorB++;
                    }
                break;
                case MCH1HC1:     // H Cursor 1
                    if(testbit(Key,K1)) Menu=MCH1HC2;   // Select Cursor
                    if(testbit(Key,K2)) {
//                        if(!testbit(MFFT,xymode)) M.Hcursor1A++;
                        M.Hcursor1A++;
                    }
                    if(testbit(Key,K3)) {
//                        if(!testbit(MFFT,xymode)) { if(M.Hcursor1A) M.Hcursor1A--; }
                        if(M.Hcursor1A) M.Hcursor1A--;
                    }
                break;
                case MCH1HC2:     // H Cursor 2
                    if(testbit(Key,K1)) Menu=MCH1HC1;    // Select Cursor
                    if(testbit(Key,K2)) M.Hcursor1B++;
                    if(testbit(Key,K3)) if(M.Hcursor1B) M.Hcursor1B--;
                break;
                case MCH2HC1:     // H Cursor 1
                    if(testbit(Key,K1)) Menu=MCH2HC2;   // Select Cursor
                    if(testbit(Key,K2)) {
                        M.Hcursor2A++;
//                        if(!testbit(MFFT,xymode)) M.Hcursor2A++;
                    }
                    if(testbit(Key,K3)) {
//                        if(!testbit(MFFT,xymode)) { if(M.Hcursor2A) M.Hcursor2A--; }
                        if(M.Hcursor2A) M.Hcursor2A--;
                    }
                break;
                case MCH2HC2:     // H Cursor 2
                    if(testbit(Key,K1)) Menu=MCH2HC1;    // Select Cursor
                    if(testbit(Key,K2)) M.Hcursor2B++;
                    if(testbit(Key,K3)) if(M.Hcursor2B) M.Hcursor2B--;
                break;
            }  // end switch(menu)
            // Prevent uglyness on slow sampling with cursors
            if(Menu>=MVC1 && Menu<=MCH2HC2 && Srate>=11) setbit(Misc, redraw);
            if(Menu==Mdefault && testbit(MStatus, stop))
                if(Srate<11 || testbit(MFFT,xymode)) Menu=MHPOS;  // Horizontal Scroll
            CheckMax(); // Check variables
        }
///////////////////////////////////////////////////////////////////////////////
// Display info (Menu, Grid, cursors, reference, settings...)
        if(testbit(MStatus, update)) {
            clrbit(MStatus, update);
            Apply();        // Apply new oscilloscope settings
            if(testbit(Misc, redraw) || testbit(Display, persistent)) {
                clrbit(Misc, redraw);
                clr_display();
            }
            else {  // Only clear menu area
                uint8_t *p;
                p=Disp_send.buffer+15;  // locate pointer to last byte in first line
                for(uint8_t i=0; i<128; i++) {
                    *p=0;
                    p+=18;              // Next line
                }
            }
        }
        if(testbit(Misc,lowbatt)) {
            //memcpy_P(Disp_send.display_data,  &BATTICON, 8);   // Low battery icon
        }
        // Print menu, also determine which items are selected and
        // print them on a black background (inverted print)
        if(Menu) {
            if(Menu<MUART) {
                const char *menuch;
                lcd_goto(0,LAST_LINE);  // Menu position
                menuch=menustxt[pgm_read_byte_near(menupoint+Menu-1)];
                for(i=0; i<3; i++) {    // 3 Items in the menu
                    clrbit(Misc,negative);    // negative font
                    switch(Menu) {
                        case MCH1:
                            if(i==0 && testbit(CH1ctrl,chon)) setbit(Misc,negative);
                        break;
                        case MCH2:
                            if(i==0 && testbit(CH2ctrl,chon)) setbit(Misc,negative);
                        break;
                        case MCHD:
                            if(i==0 && testbit(CHDctrl,chon)) setbit(Misc,negative);
                        break;
                        case MSNIFFER:      // Sniffer mode
                            if( (i==0 && !testbit(Mcursors, singlesniff)) ||
                                (i==1 && testbit(Mcursors, singlesniff)) ||
                                (i==2 && testbit(Trigger, round)) ) setbit(Misc,negative);
                        break;
                        case MTRIGTYPE:
                            if( (i==0 && testbit(Trigger, normal) && !testbit(Trigger, single)) ||
                                (i==1 && testbit(Trigger, single)) ||
                                (i==2 && testbit(Trigger, autotrg)) ) setbit(Misc,negative);
                        break;
                        case MCURSOR1:
                            if( (i==0 && testbit(Mcursors, cursorv)) ||
                                (i==1 && testbit(Mcursors, cursorh1)) ||
                                (i==2 && testbit(Mcursors, cursorh2)) ) setbit(Misc,negative);
                        break;
                        case MWINDOW:
                            if( (i==0 && testbit(MFFT, hamming)) ||
                                (i==1 && testbit(MFFT, hann)) ||
                                (i==2 && testbit(MFFT, blackman)) ) setbit(Misc,negative);
                        break;
                        case MSOURCE:
                            if( (i==0 && M.Tsource==0) ||
                                (i==1 && M.Tsource==1) ||
                                (i==2 && M.Tsource>=2) ) setbit(Misc,negative);
                        break;
                        case MDISPLAY1:
                            if( (i==0 && (Display&0x03)) ||
                                (i==1 && testbit(Display, flip)) ||
                                (i==2 && testbit(Display, disp_inv)) ) setbit(Misc,negative);
                        break;
                        case MMETER:
                            if( (i==0 && testbit(MStatus, vdc)) ||
                                (i==1 && testbit(MStatus, vp_p)) ||
                                (i==2 && (!testbit(MStatus, vdc) && !testbit(MStatus, vp_p)) ) ) setbit(Misc,negative);
                        break;
                        case MAWG:
                            if( (i==0 && M.AWGtype==1) ||
                                (i==1 && M.AWGtype==2) ||
                                (i==2 && M.AWGtype==3) ) setbit(Misc,negative);
                        break;
                        case MCH1OPT:
                            if( (i==1 && testbit(CH1ctrl,chinvert)) ||
                                (i==2 && testbit(CH1ctrl,chmath)) ) setbit(Misc,negative);
                        break;
                        case MCH2OPT:
                            if( (i==1 && testbit(CH2ctrl,chinvert)) ||
                                (i==2 && testbit(CH2ctrl,chmath)) ) setbit(Misc,negative);
                        break;
                        case MCHDOPT1:
                            if( (i==1 && testbit(CHDctrl,chinvert)) ||
                                (i==2 && testbit(CHDctrl,low)) ) setbit(Misc,negative);
                        break;
                        case MMAIN3:
                            if( (i==0 && (testbit(MFFT, scopemode) || testbit(MFFT,xymode))) ||
                                (i==1 && (MFFT<0x20)) ||
                                (i==2 && testbit(MFFT, fftmode)) ) setbit(Misc,negative);
                        break;
                        case MMAIN4:
                            if( (i==0 && testbit(MFFT, iqfft)) ||
                                (i==2 && testbit(MFFT, uselog)) ) setbit(Misc,negative);
                        break;
                        case MAWG4:
                            if( (i==0 && M.AWGtype==4) ||
                                (i==1 && M.AWGtype==0) ||
                                (i==2 && M.AWGtype==5) ) setbit(Misc,negative);
                        break;
                        case MAWG6:
                            if( (i==0 && testbit(Sweep,SweepF)) ||
                                (i==1 && testbit(Sweep,SweepA)) ||
                                (i==2 && testbit(Sweep,SweepD)) ) setbit(Misc,negative);
                        break;
/*                        case MAWG7:
                            if( (i==2 && testbit(MStatus,AWGPositive)) ) setbit(Misc,negative);
                        break;*/
                        case MSCOPEOPT:
                            if( (i==0 && testbit(Mcursors, roll)) ||
                            (i==1 && testbit(Display, elastic)) ||
                            (i==2 && testbit(MFFT, xymode)) ) setbit(Misc,negative);
                        break;
                        case MTRIGMODE:
                            if( (i==0 && testbit(Trigger, window)) ||
                                (i==1 && testbit(Trigger, edge)) ||
                                (i==2 && testbit(Trigger, slope)) ) setbit(Misc,negative);
                        break;
                        case MCURSOR2:
                            if( (i==0 && testbit(Mcursors, autocur)) ||
							    (i==1 && testbit(Mcursors, track)) ||
                                (i==2 && testbit(Mcursors, reference)) ) setbit(Misc,negative);
                        break;
                        case MCHDSEL1:
                            if( (i==0 && CHDmask==0xFF) ||
                                (i==1 && testbit(CHDmask, 0)) ||
                                (i==2 && testbit(CHDmask, 1)) ) setbit(Misc,negative);
                        break;
                        case MCHDSEL2:
                            if( (i==0 && testbit(CHDmask, 2)) ||
                                (i==1 && testbit(CHDmask, 3)) ||
                                (i==2 && testbit(CHDmask, 4)) ) setbit(Misc,negative);
                        break;
                        case MCHDSEL3:
                            if( (i==0 && testbit(CHDmask, 5)) ||
                                (i==1 && testbit(CHDmask, 6)) ||
                                (i==2 && testbit(CHDmask, 7)) ) setbit(Misc,negative);
                        break;
                        case MTSEL1:
                            if( (i==0 && M.Tsource==10) ||
                                (i==1 && M.Tsource==2) ||
                                (i==2 && M.Tsource==3) ) setbit(Misc,negative);
                        break;
                        case MTSEL2:
                            if( (i==0 && M.Tsource==4) ||
                                (i==1 && M.Tsource==5) ||
                                (i==2 && M.Tsource==6) ) setbit(Misc,negative);
                        break;
                        case MTSEL3:
                            if( (i==0 && M.Tsource==7) ||
                                (i==1 && M.Tsource==8) ||
                                (i==2 && M.Tsource==9) ) setbit(Misc,negative);
                        break;
                        case MCHDOPT2:
                            if( (i==0 && testbit(CHDctrl,hexp)) ||
                                (i==1 && testbit(CHDctrl,hexs)) ) setbit(Misc,negative);
                        break;
                        case MCHDPULL:
                            if( (i==0 && !testbit(CHDctrl,pull)) ||
                                (i==1 && testbit(CHDctrl,pull) && testbit(CHDctrl,pullup)) ||
                                (i==2 && testbit(CHDctrl,pull) && !testbit(CHDctrl,pullup)) ) setbit(Misc,negative);
                        break;
                        case MDISPLAY2:
                            if( (i==0 && testbit(Display, persistent)) ||
                                (i==1 && testbit(Display, line)) ||
                                (i==2 && testbit(Display, showset)) ) setbit(Misc,negative);
                        break;
                        case MSPI:
                            if( (i==2 && testbit(Sniffer,SSINV)) ) setbit(Misc,negative);
                        break;
                        case MCH1MATH:
                            if( (i==0 && testbit(CH1ctrl,chmath) && testbit(CH1ctrl,submult)) ||
                                (i==1 && testbit(CH1ctrl,chmath) && !testbit(CH1ctrl,submult)) ||
                                (i==2 && testbit(CH1ctrl,chaverage)) ) setbit(Misc,negative);
                        break;
                        case MCH2MATH:
                            if( (i==0 && testbit(CH2ctrl,chmath) && testbit(CH2ctrl,submult)) ||
                                (i==1 && testbit(CH2ctrl,chmath) && !testbit(CH2ctrl,submult)) ||
                                (i==2 && testbit(CH2ctrl,chaverage)) ) setbit(Misc,negative);
                        break;
                        case MSWMODE:
                            if( (i==0 && testbit(Sweep,swdown)) ||
                                (i==1 && testbit(Sweep,pingpong)) ||
                                (i==2 && testbit(Sweep,SWAccel)) ) setbit(Misc,negative);
                        break;
                    }
                    // Print text
                    char ch;
                    while ((ch=pgm_read_byte(menuch++)) != 0x00) GLCD_Putchar(ch);
                    clrbit(Misc,negative);
                }
            }
			if(Menu>=MPOSTT) {
				if(Menu>=MOUTSIDE) Menu=Mdefault; // Menu outside range
				else {
				    tiny_printp(56,LAST_LINE,PSTR("MOVE-        MOVE+"));
				    if(Menu>=MCH1POS) {
                        lcd_goto(0,LAST_LINE);
                        if(Menu<=MCH2POS) lcd_putsp(menustxt[10]);  // "Position"
                        else {
                            if(Menu<=MVC2) GLCD_Putchar('V'); else GLCD_Putchar('H');
                            lcd_putsp(menustxt[4]+2);  // "Cursor"
                            if(testbit(Menu,0)) GLCD_Putchar('1'); else GLCD_Putchar('2');
                        }
                    }
				}
			}
            // Info menus: AWG settings, Trigger Level, etc...
            lcd_goto(0,LAST_LINE);
            if(M.Tsource==0) temp1=M.CH1gain;
            else temp1=M.CH2gain;
            if(temp1>=4) text = unitmV;
            else text = unitV;
            switch(Menu) {
                case MSPI:  // SPI Configuration
                
                
                
                
                
//                    if(testbit(Sniffer,CPOL))   memcpy_P(Disp_send.display_data+(128*7+112),  &PULSEINV, 8);   // icon
//                    else                        memcpy_P(Disp_send.display_data+(128*7+112),  &PULSE, 8);   // icon






                    if(testbit(Sniffer,CPHA))   set_pixel(38,MAX_Y-6);
                    else                        set_pixel(35,MAX_Y-6);
                break;
                case MSWMODE: // Acceleration direction
                    lcd_goto(124,LAST_LINE);
                    if(testbit(Sweep,SWAccel)) {
                        if(testbit(Sweep,SWAcceldir)) GLCD_Putchar(0x18);
                        else GLCD_Putchar(0x19);
                    }
                break;
                case MUART:
                    GLCD_Putchar(0x35+((Sniffer&0x18)>>3)); // UART Data Bits
                    tiny_printp(8,LAST_LINE,baudtxt[Sniffer&0x07]); // UART Baud Rate
                    if(testbit(Sniffer,parmode)) {
                    if(testbit(Sniffer,parity)) tiny_printp(44,LAST_LINE,PSTR("ODD PARITY"));
                    else tiny_printp(40,LAST_LINE,PSTR("EVEN PARITY"));
                    }
                    else tiny_printp(48,LAST_LINE,PSTR("NO PARITY"));
                    if(testbit(Sniffer,stopbit)) tiny_printp(92,LAST_LINE,PSTR("2 STOPBIT"));
                    else tiny_printp(92,LAST_LINE,PSTR("1 STOPBIT"));
                break;
                case MPOSTT:
                    if(Srate<11) {  // Post trigger only used in fast sampling
                        long lTpost;
                        lTpost = (long)M.Tpost*(long)pgm_read_word_near(timeval+Srate);
                        if(lTpost>=3999600) lTpost = 3999600;   // Prevent overflow on display
                        printF(0,LAST_LINE,(lTpost*250));
                        if(Srate<=6) GLCD_Putchar(0x17);    // micro
                        else { GLCD_Putchar(0x1A); GLCD_Putchar(0x1B); } // mili
                        GLCD_Putchar('S');  // seconds
                    }
                break;
                case MAWGFREQ:  // Frequency
                    if(M.AWGdesiredF<100000) {
                        printF(0,LAST_LINE,(int32_t)(cycles)*50*(125000000L/(TCD1.PER+1))); // 125000000 = (1000*F_CPU/256)
                        lcd_putsp(PSTR("HZ"));
                    }
                    else {
                        printF(0,LAST_LINE,(int32_t)(cycles)*50*(F_CPU/256)/(TCD1.PER+1));
                        lcd_putsp(PSTR("KHZ"));
                    }
                break;
                case MTLEVEL:   // Trigger Level
                    if(testbit(Trigger,slope)) {
                        uint8_t mtlevel;
                        mtlevel=0x80-M.Tlevel;
                        if(mtlevel>=128) mtlevel=-mtlevel;
                        printF(0,LAST_LINE,((int32_t)mtlevel)*((int32_t)milivolts[temp1]*6400/(pgm_read_word_near(timeval+Srate))));
                        lcd_putsp(text);  // Display V or mV
                        GLCD_Putchar('/');
                        if(Srate<=6) GLCD_Putchar(0x17);    // micro
                        else if(Srate<=15) { GLCD_Putchar(0x1A); GLCD_Putchar(0x1B); } // mili
                        GLCD_Putchar('s');  // seconds
                    }
                    else {  // Edge or Dual edge trigger mode
                        printV((int16_t)(128-M.Tlevel)*128,temp1);
                        lcd_putsp(text);
                    }
                break;
                case MTW1:
                    lcd_putsp(Fone+1); printV((int16_t)(128-M.Window1)*128,temp1); lcd_putsp(text);
                break;
                case MTW2:
                    lcd_putsp(Ftwo+1); printV((int16_t)(128-M.Window2)*128,temp1); lcd_putsp(text);
                break;
                case MSW1:
                    lcd_putsp(Fone+1); printN(M.Sweep1);
                break;
                case MSW2:
                    lcd_putsp(Ftwo+1); printN(M.Sweep2);
                    break;
                case MHPOS: lcd_putsp(PSTR("STOP")); break;
                case MSWSPEED:
                    printN(AWGspeed);
                break;
                case MAWGAMP:   // Amplitude
                    printF(0,LAST_LINE,(int32_t)(-M.AWGamp)*(100000/32));
                    GLCD_Putchar('V');
                break;
                case MAWGOFF:   // Offset
                    printF(0,LAST_LINE,((int32_t)(-M.AWGoffset)*(50016/32)));
                    GLCD_Putchar('V');
                break;
                case MAWGDUTY:  // Duty Cycle
                    printF(0,LAST_LINE,(int32_t)(M.AWGduty*(5000064/128)));
                    GLCD_Putchar('%');
                break;
                case MTHOLD:
                    lcd_goto(8,LAST_LINE);
                    printN(M.Thold);
                    GLCD_Putchar(0x1A); GLCD_Putchar(0x1B); GLCD_Putchar('s');  // mili seconds
                break;
            }
        }
        else if(testbit(MStatus, stop)) tiny_printp(0,LAST_LINE, PSTR("STOP"));
        if(MFFT>=0x20) { // Not Meter mode
            // Grid
            if(!testbit(MFFT, fftmode)) {
                uint8_t ch1gnd;
                ch1gnd = (M.CH1pos+128)/2;
                if(ch1gnd>MAX_Y) ch1gnd=MAX_Y;
                temp2 = (M.CH2pos+128)/2;
                if(temp2>MAX_Y) temp2=MAX_Y;
                if(testbit(MFFT, xymode)) {   //XY Mode
                    ch1gnd = temp2 = 64/*-(M.HPos/2)*/;
                }
                switch(Display&0x03) {
                    case 1:
                        set_pixel(64,16);       // Vertical dot
                        set_pixel(64,32);       // Vertical dot
                        set_pixel(64,48);       // Vertical dot
                        set_pixel(64,64);       // Vertical dot
                        set_pixel(64,80);       // Vertical dot
                        set_pixel(64,96);       // Vertical dot
                        set_pixel(64,112);      // Vertical dot
                    case 2:
                        for(i=16; i<=112; i+=16) {
                            if(testbit(CH1ctrl,chon)) set_pixel(i,ch1gnd); // CH1 ground
                            if(testbit(CH2ctrl,chon)) set_pixel(i,temp2);  // CH2 ground
                        }
                    break;
                    case 3: // Graticule
                        for(j=16; j<=(MAX_Y-15); j+=16) {
                            for(i=16; i<=(MAX_X-15); i+=16) set_pixel(i,j);
                        }
                    break;
                }
            }
            // Horizontal Cursors (or XY cursors)
            if((testbit(Mcursors, cursorh1) && testbit(CH1ctrl,chon)) ||
                (testbit(Mcursors, cursorh2) && testbit(CH2ctrl,chon))) ShowCursorH();
            if(!testbit(MFFT,xymode)) { // Vertical Cursors
                if(testbit(Mcursors, cursorv)) ShowCursorV();
            }
            // Display time and gain settings
            ypos=0;
            if(testbit(Display, showset)) {
                if(testbit(CH1ctrl,chon)) {
                    if(testbit(CH1ctrl,chmath)) {
                        lcd_goto(88,0);
                        if(testbit(CH1ctrl,chinvert)) GLCD_Putchar('-'); else GLCD_Putchar(' ');
                        lcd_putsp(menustxt[12]+1);  // CH1 text
                        if(testbit(CH1ctrl,submult)) {
                            if(testbit(CH2ctrl,chinvert)) GLCD_Putchar('+'); else GLCD_Putchar('-');
                        }
                        else {
                            GLCD_Putchar('X');
                            if(testbit(CH2ctrl,chinvert)) { GLCD_Putchar('-'); u8CursorX-=4; }
                        }
                        lcd_putsp(menustxt[12]+15); // CH2 text
                    }
                    else {
                        lcd_goto(76,0);
                        if(testbit(CH1ctrl,chinvert)) GLCD_Putchar('-'); else GLCD_Putchar(' ');
                        lcd_putsp(menustxt[12]+1);  // CH1 text
                        lcd_putsp(gaintxt[M.CH1gain]);
                        lcd_putsp(Vdiv);    // V/div
                    }
                    ypos++;
                }
                if(testbit(CH2ctrl,chon)) {
                    if(testbit(CH2ctrl,chmath)) {
                        lcd_goto(88,ypos);
                        if(testbit(CH2ctrl,chinvert)) GLCD_Putchar('-'); else GLCD_Putchar(' ');
                        lcd_putsp(menustxt[12]+16); // CH2 text
                        if(testbit(CH2ctrl,submult)) {
                            if(testbit(CH1ctrl,chinvert)) GLCD_Putchar('+'); else GLCD_Putchar('-');
                        }
                        else {
                            GLCD_Putchar('X');
                            if(testbit(CH2ctrl,chinvert)) { GLCD_Putchar('-'); u8CursorX-=4; }
                        }
                        lcd_putsp(menustxt[12]);  // CH1 text
                    }
                    else {
                        lcd_goto(76,ypos);
                        if(testbit(CH2ctrl,chinvert)) GLCD_Putchar('-'); else GLCD_Putchar(' ');
                        lcd_putsp(menustxt[12]+16); // CH2 text
                        lcd_putsp(gaintxt[M.CH2gain]);
                        lcd_putsp(Vdiv);    // V/div
                    }
                    ypos++;
                }
                if(testbit(MFFT,fftmode)) {
                    tiny_printp(89,ypos,freqtxt[Srate]);    // Display Nyquist Frequency
                    lcd_putsp(PSTR("HZ MAX"));
                }
                else {
                    tiny_printp(96,ypos,ratetxt[Srate]);    // Display time base
                    lcd_putsp(Sdiv);    // S/div
                }
                ypos++;
            }
            // Show reference waveforms
            if(testbit(Mcursors, reference)) {
                i=0;
                if(Srate>=11) {
                    do {
                        uint8_t nx, ox, ny1, oy1, ny2, oy2;
                        oy1=ny1; oy2=ny2;
                        ny1=eeprom_read_byte(&EECHREF1[i]);
                        ny2=eeprom_read_byte(&EECHREF2[i]);
                        nx=i>>1;
                        ox=(i-1)>>1;
                        if(testbit(Display, line)) {
                            if(i==0) continue;
                            if(testbit(CH1ctrl,chon)) lcd_line(nx, ny1, ox, oy1);
                            if(testbit(CH2ctrl,chon)) lcd_line(nx, ny2, ox, oy2);
                        }
                        else {
                            if(testbit(CH1ctrl,chon)) set_pixel(nx, ny1);
                            if(testbit(CH2ctrl,chon)) set_pixel(nx, ny2);
                        }
                    } while(++i);
                }
                else {
                    uint8_t ny1, ny2, oy2;
                    for (j=M.HPos; i<128; i++, j++, temp3=ny1, oy2=ny2) {
                        ny1=eeprom_read_byte(&EECHREF1[j]);
                        ny2=eeprom_read_byte(&EECHREF2[j]);
                        if(testbit(Display, line)) {
                            if(i==0) continue;
                            if(testbit(CH1ctrl,chon)) lcd_line(i, ny1, i-1, temp3);
                            if(testbit(CH2ctrl,chon)) lcd_line(i, ny2, i-1, oy2);
                        }
                        else {
                            if(testbit(CH1ctrl,chon)) set_pixel(i, ny1);
                            if(testbit(CH2ctrl,chon)) set_pixel(i, ny2);
                        }
                    }
                }
            }
            i=ypos;
            // Trigger mark if tsource is CH1 or CH2
            if((testbit(Trigger, normal) || testbit(Trigger, autotrg)) && testbit(MFFT, scopemode)) {
                chdtrigpos = 255;
                if(Srate>=11 || hibyte(M.Tpost)==0) {
                    uint8_t trig1, trig2;
                    if(testbit(Trigger, window)) trig1=M.Window1;
                    else {
                        trig1=M.Tlevel;
                        if(testbit(Trigger,trigdir)) setbit(Misc,bigfont);
                    }
                    trig2=M.Window2;
                    if(M.Tsource==0) {                      // Adjust position relative to CH1
                        if(testbit(CH1ctrl,chinvert)) {
                            trig1=255-trig1;
                            trig2=255-trig2;
                            togglebit(Misc, bigfont);
                        }
                        trig1+=M.CH1pos;
                        trig2+=M.CH1pos;
                    }
                    else if(M.Tsource==1) {                 // Adjust position relative to CH2
                        if(testbit(CH2ctrl,chinvert)) {
                            trig1=255-trig1;
                            trig2=255-trig2;
                            togglebit(Misc, bigfont);
                        }
                        trig1+=M.CH2pos;
                        trig2+=M.CH2pos;
                    }
                    trig1=trig1>>1; // Trigger or Window Trigger 1
                    trig2=trig2>>1; // Window Trigger 2
                    uint8_t trigpos;
                    trigpos=-M.HPos-lobyte(M.Tpost)-1;    // Horizontal location of trigger
                    if(Srate>=11) {
                        if(testbit(Mcursors,roll)) trigpos=255;   // don't display the trigger mark in roll mode
                        else trigpos=0;
                    }
                    chdtrigpos=trigpos;
                    if(trigpos<126 && M.Tsource<=1) {
                        if((Display&0x03)==2) {     // Grid Vertical dots follow trigger
                            set_pixel(trigpos,16);    // Vertical dot
                            set_pixel(trigpos,32);    // Vertical dot
                            set_pixel(trigpos,48);    // Vertical dot
                        }
                        if(testbit(Trigger, window)) {
                            if(trig1<=(MAX_Y-3) && testbit(Misc, bigfont)) { // up
                                sprite(trigpos, trig1, tup);
                            }
                            else if(trig1>=3 && trig1<=MAX_Y && !testbit(Misc, bigfont)) { // down
                                sprite(trigpos, trig1, tdown);
                            }
                            if(trig2<=(MAX_Y-3) && testbit(Misc, bigfont)) { // down
                                sprite(trigpos, trig2, tdown);
                            }
                            else if(trig2>=3 && trig2<=MAX_Y && !testbit(Misc, bigfont)) { // up
                                sprite(trigpos, trig2, tup);
                            }
                        }
                        else if(testbit(Trigger, slope)) {
                            uint8_t trigy;
                            trigy=M.Tlevel>>2;
                            lcd_line(trigpos,trigy,trigpos+2,trigy);
                            trigy=(uint8_t)(-M.Tlevel)>>2;
                            lcd_line(trigpos,trigy,trigpos+2,trigy);
                        }
                        else if(testbit(Trigger, edge)) {
                            if(trig1<=(MAX_Y-3) && testbit(Misc, bigfont)) { // down
                                sprite(trigpos, trig1, tdown);
                            }
                            else if(trig1>=3 && trig1<=MAX_Y && !testbit(Misc, bigfont)) { // up
                                sprite(trigpos, trig1, tup);
                            }
                        }
                        else if(trig1>=3 && trig1<=(MAX_Y-3)) {  // Dual edge trigger
                            sprite(trigpos, trig1, tdual);
                        }
                    }
                    clrbit(Misc,bigfont);
                }
            }
        }
///////////////////////////////////////////////////////////////////////////////
// Finished using DMA, now use a DMA to transfer data to the display
        dma_display();
        if(testbit(USB.STATUS,USB_SUSPEND_bp) && USB.ADDR) USB_ResetInterface();
        if(!testbit(MStatus,stop) && (Srate<11 || Index==0)) {
            if(!(Srate>=11 && testbit(Mcursors,roll))) ONGRN();
//            if(!testbit(PORTE.IN,1)) ONRED(); // LINK signal or battery charging
        }
		if(testbit(MStatus, updateawg)) BuildWave();
        ADCA.CH2.CTRL     = 0x80;   // VCC measurement
        // Wait for display transfer
        WaitDisplay();
        if(ADCA.CH2RESL<18) setbit(Misc, lowbatt);
        else clrbit(Misc, lowbatt);
///////////////////////////////////////////////////////////////////////////////
// Limit LCD refresh rate
/*        if(MFFT>=0x20 || (adjusting==0)) { // Not meter mode, Auto setup has not yet locked on settings
            if(!testbit(TCD0.INTFLAGS, TC2_HUNFIF_bp)) {            //  If not overflowed, go to idle mode
                TCD0.INTCTRLA = TC2_HUNFINTLVL_LO_gc;               // Enable interrupt to wakeup
                SLEEP.CTRL = SLEEP_SMODE_IDLE_gc | SLEEP_SEN_bm;    //
                asm("sleep");
                TCD0.INTCTRLA = 0;                                  // Disable interrupt to wakeup
            }
            SLEEP.CTRL = 0x00;
            setbit(TCD0.INTFLAGS, TC2_HUNFIF_bp);                   // Clear flag
        }*/
    }
}

void AutoSet(void) {
    clrbit(MStatus,stop);
    // Set Free Trigger
    clrbit(Trigger, normal);    // Clear Normal trigger
    clrbit(Trigger, single);    // Clear Single trigger
    clrbit(Trigger, autotrg);   // Clear Auto trigger
    adjusting=11;
    Menu=Mdefault;
    Key=0;
}

// Reduce gain in Auto setup
void Reduce(void) {
//    uint8_t max=64;
//    if(testbit(CH1ctrl,chon) && testbit(CH2ctrl,chon)) max=32;
    if(M.CH1gain && CH1.vpp>32) {
        M.CH1gain--;
        if(M.Tsource==0) M.Tlevel=half(M.Tlevel);
        CH1.vpp=CH1.vpp/2;
//        M.CH1pos=M.CH1pos/2;
    }
    if(M.CH2gain && CH2.vpp>32) {
        M.CH2gain--;
        if(M.Tsource==1) M.Tlevel=half(M.Tlevel);
        CH2.vpp=CH2.vpp/2;
//        M.CH2pos=M.CH2pos/2;
    }
}

// Exit Meter mode, restore settings
void RestorefromMeter(void) {
    adjusting=0;            // Prevent autosetup when restoring from meter
    Srate = old_s;          // restore sampling and gains
    M.CH1gain = old_g1;
    M.CH2gain = old_g2;
}

// Set Meter mode, save settings
void GoingtoMeter(void) {
    old_s = Srate;          // save sampling and gains
    old_g1 = M.CH1gain;
    old_g2 = M.CH2gain;
    M.CH1gain=0;
    M.CH2gain=0;
    adjusting=11;
}

uint8_t fft_stuff(uint8_t *p1) {
    uint8_t i=0,f,w;
    const uint8_t *p2;		// Pointer to channel data
	const int8_t *p3;		// Pointer to window function
    if(testbit(MFFT, hamming)) p3=Hamming;          // Apply Hamming window
    else if(testbit(MFFT, hann)) p3=Hann;           // Apply Hann window
    else if(testbit(MFFT, blackman)) p3=Blackman;   // Apply Blackman window
	else setbit(Misc, bigfont);		// Temporally use this bit for "no window"
	if(testbit(MFFT,iqfft)) {
    	p1=DC.CH1data; p2=DC.CH2data;
        do {
            uint8_t ch1,ch2;
		    w=pgm_read_byte_near(p3);           // Get window data
		    if(testbit(Misc,bigfont)) w=127;    // No window
		    if(i<127) p3++;                     // Window symmetry
		    if(i>127) p3--;                     // (only stored half of window)
            ch1=(int8_t)((*p1++)-128);          // Convert to signed char
            ch2=(int8_t)((*p2++)-128);          // Convert to signed char
            Temp.FFT.bfly[i].r=FMULS(ch1, w);
		    Temp.FFT.bfly[i].i=FMULS(ch2, w);
        } while (++i);
	}
	else {
        do {
            uint8_t ch;
		    w=pgm_read_byte_near(p3);       // Get window data
		    if(testbit(Misc,bigfont)) w=127;	// No window
		    if(i<127) p3++;                 // Window symmetry
		    if(i>127) p3--;                 // (only stored half of window)
            ch=(int8_t)((*p1++)-128);       // Convert to signed char
            Temp.FFT.bfly[i].r=Temp.FFT.bfly[i].i=(signed int)(FMULS8(ch, w)*256);
        } while (++i);
	}
	clrbit(Misc,bigfont);
    fft_execute(Temp.FFT.bfly);
    fft_output(Temp.FFT.bfly, Temp.FFT.magn);
    // Find maximum frequency
    uint8_t max=3,current;
    i=1;                        // Ignore DC
    if(Temp.FFT.magn[0]>Temp.FFT.magn[1]) i=2;    // Ignore big DC
    f=0;
    for(; i<FFT_N/2; i++) {
        current=Temp.FFT.magn[i];
        if(current>max) {
            max=current; f=i;
        }
    }
    if(Temp.FFT.magn[f]>7) return f;
    else return 0;      // Signal too small
}

// Automatically set vertical cursors
void AutoCursorV(void) {
    uint8_t i, mid, *p, samples;
    if(Srate>=11) { // Slow sampling rates (below 50mS/div) use 2 samples per vertical line
        samples = 255;
    }
    else samples = 127;
    if(testbit(MFFT, fftmode)) {
        M.VcursorA = CH1.f;
        M.VcursorB = CH2.f;
        return;
    }
    else {  // Scan data
        // Start with cursors at the edges
        M.VcursorA=0;
        M.VcursorB=samples;
        // Decide which channel to use for vertical cursors
        if((testbit(CH1ctrl,chon) && !testbit(CH2ctrl,chon)) ||               // CH2 off, use CH1
        (testbit(CH1ctrl,chon) && (CH1.vpp > CH2.vpp)) ) { // CH1 has more amplitude
            p = DC.CH1data+M.HPos;
            mid = CH1.min + CH1.vpp/2;
            if(CH1.vpp<8) goto end_scan;    // Signal too small
        }
        else {                              // Use CH2
            p = DC.CH2data+M.HPos;
            mid = CH2.min + CH2.vpp/2;
            if(CH2.vpp<8) goto end_scan;    // Signal too small
        }
        i=0;
        if(p[0]<mid) {
            // Find first cross
            while(*p++<mid) { i++; if(i==samples) goto end_scan; }
            M.VcursorA=i;
            // Half cycle
            while(*p++>mid) { i++; if(i==samples) goto end_scan; }
            M.VcursorB = ++i;
            // Full cycle
            while(*p++<mid) { i++; if(i==samples) goto end_scan; }
            M.VcursorB=++i;
        }
        else {
            // Find first cross
            while(*p++>mid) { i++; if(i==samples) goto end_scan; }
            M.VcursorA=i;
            // Half cycle
            while(*p++<mid) { i++; if(i==samples) goto end_scan; }
            M.VcursorB = ++i;
            // Full cycle
            while(*p++>mid) { i++; if(i==samples) goto end_scan; }
            M.VcursorB=++i;
        }
    }
end_scan:
    if(Srate>=11) { // Slow sampling rates (below 50mS/div) use 2 samples per vertical line
        M.VcursorA=M.VcursorA>>1;
        M.VcursorB=M.VcursorB>>1;
    }
}

// Measurements for Meter Mode
static inline void Measurements(void) {
    int32_t avrg1=0, avrg2=0;       // Averages
    static int16_t v1,v2;
	int16_t calibrate;
    uint8_t i=0;
    setbit(Misc,bigfont);
    lcd_goto(0,2);
    if(testbit(MStatus,vdc)) {         // Display VDC
        uint8_t oldctrlb, oldprescale, oldch0, oldch1;
        if(testbit(MStatus,stop)) goto cancelvdc;
        oldctrlb = ADCA.CTRLB;
        oldprescale = ADCA.PRESCALER;
        oldch0=ADCA.CH0.CTRL;
        oldch1=ADCA.CH1.CTRL;
        ADCA.CTRLB = 0x90;          // signed mode, no free run, 12 bit right adjusted, low impedance
        ADCA.PRESCALER = 0x07;      // Prescaler 512 (125kHZ ADC clock)
        do {
            ADCA.CH0.CTRL     = 0x83;   // Start conversion, Differential input with gain
            ADCA.CH1.CTRL     = 0x83;   // Start conversion, Differential input with gain
            _delay_us(400);
            avrg1-= (int16_t)ADCA.CH0.RES;
            avrg2-= (int16_t)ADCA.CH1.RES;
            if(testbit(MStatus,update)) goto cancelvdc;
        } while(++i);
		calibrate=(int16_t)eeprom_read_word((uint16_t *)&offset16CH1);    // CH1 Offset Calibration
        avrg1+=calibrate;
//		calibrate=eeprom_read_word((int16_t *)&gain16CH1);      // CH1 Gain Calibration
//		avrg1*=calibrate;
		calibrate=(int16_t)eeprom_read_word((uint16_t *)&offset16CH2);    // CH2 Offset Calibration
        avrg2+=calibrate;
//		calibrate=eeprom_read_word((int16_t *)&gain16CH2);      // CH2 Gain Calibration
//		avrg2*=calibrate;
        v1=((avrg1>>5)+v1)/2; // no average: avrg2>>5;
        v2=((avrg2>>5)+v2)/2; // no average: avrg2>>5;
        ADCA.CH1.CTRL = oldch1;
        ADCA.CH0.CTRL = oldch0;
        ADCA.PRESCALER = oldprescale;
        ADCA.CTRLB = oldctrlb;
cancelvdc:
        printV(v1,0);
        lcd_goto(64,2);
        printV(v2,0);
    }
    else if(testbit(MStatus,vp_p)) {   // Display VPP
                        printV((int16_t)CH1.vpp*128,M.CH1gain);
        lcd_goto(64,2); printV((int16_t)CH2.vpp*128,M.CH2gain);
    }
    else {                          // Display frequency
        // TCC1:Lo16 TCE0:Hi16 TCC0:Timer
        // Event CH2:Ext, Event CH4:TCC0 overflow, Event CH5:TCC1 overflow
        uint32_t freqv;
        cli();
        TCC0.CTRLA = 0;             // Stop time keeper
        TCC0.CTRLE = 0;             // Normal mode
        TCC0.CTRLFSET = 0x0C;       // Reset timer
        TCC0.PER = 31250;           // Period is 1 second, one tick added to sync
        TCE0.CTRLA = 0;             // Stop timer prior to reset
        TCE0.CTRLFSET = 0x0C;       // Reset timer
        TCE0.CTRLB =  TC0_CCAEN_bm; // Capture enable
        TCE0.CTRLD = 0x3C;          // Input capture on event, delay compensate, Event CH4
        TCE0.CTRLA = TC_CLKSEL_EVCH5_gc;    // Event CH5 is TCE0 clock source
        TCC1.CTRLA = 0;             // Stop timer prior to reset
        TCC1.CTRLFSET = 0x0C;       // Reset timer
        TCC1.CTRLB = TC1_CCAEN_bm;  // Capture enable
        TCC1.CTRLD = 0x2C;          // Input capture on event, no delay compensate, Event CH4
        TCC0.CTRLA =  7;            // Start Timer, because of the common prescaler, the start is variable
        while(TCC0.CNTL==0);        // Sync with common prescaler
        TCC1.CTRLA = TC_CLKSEL_EVCH2_gc;    // Event CH2 is TCC1 clock source
        sei();
        freqv=pgm_read_dword_near(freqval+Srate)/256;
        printF( 0,2,(int32_t)(CH1.f)*freqv);
        printF(64,2,(int32_t)(CH2.f)*freqv);
        adjusting=11;
        calibrate = 1010;           // 1.01 second timeout
        while(!testbit(TCC0.INTFLAGS,TC1_OVFIF_bp)) {	// Should be ready in 1 second
            _delay_ms(1);
            if(testbit(MStatus,update) || (--calibrate==0)) goto cancelfreq;
        }
        freqv = TCE0.CCABUF;
        freqv = (freqv<<16) + TCC1.CCABUF;
        setbit(Misc,negative);
        printF(8,5,freqv);          // Print count
        clrbit(Misc,negative);
cancelfreq:
        // End
        setbit(MStatus, updatemso);
    }
    clrbit(Misc,bigfont);
}

// Display Vertical Cursor
static inline void ShowCursorV(void) {
    char const *unitF;
    uint8_t i,delta;
    uint32_t freqv;
    if(Srate<=6) unitF = unitkHz;
    else unitF = unitkHz+1; // Hz: Use same text as kHz, with + 1 offset

    for(i=1; i<=MAX_Y; i+=4) {
        set_pixel(M.VcursorA,i);
        set_pixel(M.VcursorB,2+i);
    }
    if(M.VcursorA>=M.VcursorB) delta=M.VcursorA-M.VcursorB;
    else delta=M.VcursorB-M.VcursorA;

    if(testbit(MFFT, fftmode)) {
		int8_t fch1,fch2;
        freqv = pgm_read_dword_near(freqval+Srate)/256;
		if(testbit(MFFT,iqfft)) {
			fch1=M.VcursorA-M.HPos;
			fch2=M.VcursorB-M.HPos;
		}
		else {
			fch1=M.VcursorA;
			fch2=M.VcursorB;
		}
        tiny_printp(0,0, Fone);  // F1 =
        printF(16,0,(long)(fch1)*(freqv));
        lcd_putsp(unitF);
        tiny_printp(0,1, Ftwo);  // F2 =
        printF(16,1,(long)(fch2)*(freqv));
        lcd_putsp(unitF);
    }
    else {
        tiny_printp(64,LAST_LINE-3, one_over_delta_T); // 1 delta T =
        if(delta) {
            freqv = pgm_read_dword_near(freqval+Srate)/delta;
            if(Srate>=11) freqv = freqv / 2;    // Slow sampling rate uses 2 samples per pixel
            printF(88,LAST_LINE-3,(long)freqv);
            lcd_putsp(unitF);
        }
        tiny_printp(76,LAST_LINE-2, one_over_delta_T+3);   // delta T = (use same string as one_over_delta_T)
        printF(88,LAST_LINE-2,((long)delta)*pgm_read_word_near(timeval+Srate)*250);
        if(Srate<=6) GLCD_Putchar(0x17);    // micro
        else if(Srate<=15) { GLCD_Putchar(0x1A); GLCD_Putchar(0x1B); } // mili
        GLCD_Putchar('S');  // seconds
    }
}

// Display Horizontal Cursor
static inline void ShowCursorH(void) {
    uint8_t i,y,gain,HcursorA,HcursorB,*data;
	int8_t CHPos;
	ACHANNEL *CH;
    // Display Horizontal Cursor, cursor disabled during FFT mode
    if(!testbit(MFFT, fftmode)) {
	    if(testbit(Mcursors,cursorh1)) {
    		gain=M.CH1gain;
		    HcursorA=M.Hcursor1A;
		    HcursorB=M.Hcursor1B<<1;
		    CHPos=M.CH1pos;
		    CH=&CH1; data=DC.CH1data;
	    }
	    else {
    		gain=M.CH2gain;
		    HcursorA=M.Hcursor2A;
		    HcursorB=M.Hcursor2B<<1;
		    CHPos=M.CH2pos;
		    CH=&CH2; data=DC.CH2data;
	    }
		if(testbit(Mcursors,track)) {
   			HcursorA=addwsat(data[M.VcursorA+M.HPos],CHPos);
    		HcursorB=addwsat(data[M.VcursorB+M.HPos],CHPos);
		}
		else if(testbit(Mcursors,autocur)) {
            // Apply position
            HcursorA=addwsat(CH->max,CHPos)>>1;
            HcursorB=addwsat(CH->min,CHPos)>>1;
		}
        if(HcursorA>=128) HcursorA=127;
        if(HcursorB>=128) HcursorB=127;
        //if(!testbit(MFFT,xymode)) HcursorA=HcursorA>>1;
        //else HcursorA=127-HcursorA;
        //HcursorB=HcursorB>>1;
        for(i=1; i<=(MAX_X-1); i+=4) {
            if(testbit(MFFT,xymode)) {
                if(i<=MAX_Y) set_pixel(HcursorA,i);  // Vertical Line for HcursorA
            }
            else set_pixel(i,HcursorA);                         // Horizontal Line for HcursorA
            set_pixel(i+2,HcursorB);                            // Horizontal Line for HcursorB
        }
        int8_t adjustedA,adjustedB;
        char const *Hcursorunit;                  // Horizontal cursor units
        if(testbit(MFFT,xymode)) {
            adjustedA=HcursorA-MAX_Y+1;
            adjustedB=(MAX_Y+1)-HcursorB-(M.HPos/2);
            tiny_printp(78,LAST_LINE-2, PSTR("X "));          // X
            printV(adjustedA*256, M.CH1gain);
            if(M.CH1gain>=4) Hcursorunit = unitmV;
            else Hcursorunit = unitV;
            lcd_putsp(Hcursorunit);
            tiny_printp(78,LAST_LINE-1, menustxt[35]+32);     // Y
            printV(adjustedB*256, M.CH2gain);
            if(M.CH2gain>=4) Hcursorunit = unitmV;
            else Hcursorunit = unitV;
            lcd_putsp(Hcursorunit);
        }
        else {
            // Apply position
            adjustedA=32-HcursorA+(CHPos+64)/2;
            adjustedB=32-HcursorB+(CHPos+64)/2;
            // Decide where to print text
            if(HcursorA<HcursorB) y=0;
            else y=LAST_LINE-1;
            // Display values
            if(gain>=4) Hcursorunit = unitmV;
            else Hcursorunit = unitV;
            lcd_goto(8,y); printV(adjustedA*256, gain);
            lcd_putsp(Hcursorunit);
            if(HcursorA<HcursorB) y=LAST_LINE-1;
            else y=0;
            lcd_goto(8,y); printV(adjustedB*256, gain);
            lcd_putsp(Hcursorunit);
            tiny_printp(76,LAST_LINE-1, delta_V);  // delta V =
            printV(((int16_t)HcursorA-HcursorB)*256, gain);
            lcd_putsp(Hcursorunit);
        }
    }
}

// Check variables
extern const NVMVAR MAXM;
void CheckMax(void) {
    uint8_t *p=&M.CH1gain;
    uint8_t max;
    for(uint8_t i=0; i<sizeof(NVMVAR); i++) {
        max=pgm_read_byte(&MAXM.CH1gain+i);
        if(*p>=max) *p=max;
        p++;
    }
//    if(M.CH1pos>=0)     M.CH1pos=-2;    // Position must be negative
//    if(M.CH2pos>=0)     M.CH2pos=-2;    // Position must be negative
    if(M.Sweep1>M.Sweep2) M.Sweep1=M.Sweep2;
    if(M.SWSpeed==0)    M.SWSpeed=1;    // Minimum sweep speed
    if(M.Tlevel<3)      M.Tlevel=3;     // Minimum Trigger Level
    if(M.Window1<M.Window2) M.Window1=M.Window2;
    if(Srate>21)        Srate=21;       // Maximum sampling rate
}

void CheckPost(void) {
    // Can't stop the ADC fast enough, so limit the range
    if(M.Tpost<255) {
        uint8_t adjust=0;
        uint8_t Tpost;
        Tpost = lobyte(M.Tpost);
        if(Srate==0 && Tpost<8) adjust=8;
        else if(Srate==1 && Tpost<5) adjust=5;
        else if(Srate==2 && Tpost<5) adjust=5;
        else if(Srate<=5 && Tpost<4) adjust=4;
        else if(Tpost==0) adjust=1;
        if(adjust) M.Tpost=adjust;
    }
    if(M.Tpost>=32768) M.Tpost=32767;
}

// Apply oscilloscope settings
void Apply(void) {
    PMIC.CTRL = 0x04;       // Only high level interrupts
    TCE0.CTRLA = 0;		    // TCE0 controls Interrupt ADC, srate: 6, 7, 8, 9, 10 and fixed value for slow sampling
    TCE0.CTRLB = 0;
    TCE0.INTCTRLA = 0;
    TCC1.CTRLA = 0;
    TCC1.CTRLB = 0;
    // TCC0 controls the auto trigger and auto key repeat
    TCC0.CTRLA = 0x0F;      // Event CH7: 0.04096mS period - 24.4140625 Hz
    TCC0.CTRLE = 0x02;      // Timer TCC0 Split Mode
    TCC0.PERH = 24;         // Auto Key Timeout is 1.024 second
    TCC0.PERL = M.Ttimeout;
    // TCD0: TCD0H controls LCD refresh rate, TCD0L is the slow clock generator
    TCD0.CTRLA = 0x0E;      // Event CH6 (CLKPER / 32768 -> 1.024ms)
    TCD0.CTRLE = 0x02;      // Timer TCD0 Split Mode
    TCD0.PERL = 39;         // 40.96mS -  24.4140625 Hz
    // Validate variables
    CheckMax();
    CheckPost();
    // Count CHD enabled pins
    uint8_t temp1,temp2;
    temp1=0;
    temp2=CHDmask;
    do {
        temp1 += temp2 & 1;
    } while (temp2 >>= 1);
    temp1 = (8-temp1)*8;    // Max position
    if(M.CHDpos>temp1)  M.CHDpos= 0;

    ADCA.CTRLB = 0x14;          // signed mode, no free run, 8 bit
    ADCB.CTRLB = 0x14;          // signed mode, no free run, 8 bit
    ADCA.EVCTRL = 0x05;         // Sweep channels 0, Event CH0 (TCE0), Sync Sweep
    ADCB.EVCTRL = 0x05;         // Sweep channels 0, Event CH0 (TCE0), Sync Sweep
    ADCA.PRESCALER = 0x03;      // Prescaler: 32
    ADCB.PRESCALER = 0x03;      // Prescaler: 32
    // Set ADC Clock
    if(Srate>=11) { // Slow Sample rates >= 20mS/div
        //TCD0.PERH = 0;       // Refresh rate per pixel 976.5625Hz.
        TCE0.CTRLA = 0x03;  // DIV4
        if(Srate==11) TCE0.PER = 4999;      // 1600 Hz
        else TCE0.PER = 6249;               // 1280 Hz
        slowval = (uint16_t)pgm_read_word_near(slowcnt-11+Srate);
    }
    else {  // Fast sampling
        Index = 0;
        //TCD0.PERH = 7;              // TCD0H controls LCD refresh rate: 122.07Hz
        if(Srate>=6) {
            TCE0.PER = (uint16_t)pgm_read_word_near(TCE0val+Srate-6); // ADC clock
        }
        else {  // sampling rate 256uS/div and under, use DMA
            ADCA.CTRLB  = 0x1C;     // signed mode, free run, 8 bit
            ADCB.CTRLB  = 0x1C;     // signed mode, free run, 8 bit
            if(Srate>1) {
                ADCA.PRESCALER = Srate; // Prescaler is 16, 32, 64, ...
                ADCB.PRESCALER = Srate; // Prescaler is 16, 32, 64, ...
                ADCA.EVCTRL = 0x00;     // Sweep channels 0
                ADCB.EVCTRL = 0x00;     // Sweep channels 0
            }
            else /*if(Srate)*/ {
                ADCA.PRESCALER = 0x02;
                ADCB.PRESCALER = 0x02;
                ADCA.EVCTRL = 0x00;     // Sweep channels 0
                ADCB.EVCTRL = 0x00;     // Sweep channels 0
            }
/*            else {  // 8uS / div - 2MSPS
                ADCA.PRESCALER = 0x02;
                ADCA.EVCTRL = 0x00;     // Only channel 0
            }*/
        }
    }
    // Display settings
//	GLCD_setting();
    // CH gains
    ADCA.CTRLA          = 0x01;     // Enable ADC
    ADCB.CTRLA          = 0x01;     // Enable ADC
    ADCA.REFCTRL        = 0x20;     // REF = AREF (2V)
    ADCB.REFCTRL        = 0x20;     // REF = AREF (2V)
    // ADC Linearity calibration
    ADCA.CALL           = SP_ReadCalibrationByte(offsetof(NVM_PROD_SIGNATURES_t, ADCACAL0) );
    ADCA.CALH           = SP_ReadCalibrationByte(offsetof(NVM_PROD_SIGNATURES_t, ADCACAL1) );
    ADCA.CH0.MUXCTRL    = 0x38;     // Channel 0 input: ADC7 pin - ADC4 pin
    ADCA.CH0.CTRL = 0x03 | (M.CH1gain<<2);       // Set gain
    ADCB.CH0.MUXCTRL    = 0x21;     // Channel 1 input: ADC4 pin - ADC5 pin
    ADCB.CH0.CTRL = 0x03 | (M.CH2gain<<2);       // Set gain
    ADCA.CH2.MUXCTRL    = 0x10;     // Channel 2 input: VCC/10
	// CH offset
	uint8_t srateoff;
	srateoff=Srate;
	if(Srate>7) srateoff=7;
    CH1.offset=-(eeprom_read_byte((uint8_t *)&offset8CH1[srateoff][M.CH1gain]));
    CH2.offset=-(eeprom_read_byte((uint8_t *)&offset8CH2[srateoff][M.CH2gain]));
    PMIC.CTRL = 0x07;   // Enable all interrupts
    // Logic input options
    uint8_t temp;
    temp=1;         // Sense rising edge (for freq. counter)
    if(testbit(CHDctrl,chinvert)) temp |=0x40;    // Invert logic inputs
    if(testbit(CHDctrl,pull)) {                 // Pull
        temp|=0x10;
        if(testbit(CHDctrl,pullup)) temp|=0x08;     // Pull up
    }
    PORTCFG.MPCMASK = 0xFF;
    PORTC.PIN0CTRL  = temp;
	clrbit(MStatus, updatemso);
 }

// Slow sampling control - 640Hz or 1600Hz
ISR(TCE0_OVF_vect) {
    static uint16_t count=0;
    static uint32_t sum1,sum2;
    uint8_t mul,chd,ch1,ch2,sch1,sch2;
    uint8_t ou8CursorX,ou8CursorY;

    ou8CursorX = u8CursorX;
    ou8CursorY = u8CursorY;
    // Get and apply offset
    ch1=ADCA.CH0.RESL; ch1 = saddwsat(ch1,CH1.offset);
    ch2=ADCB.CH0.RESL; ch2 = saddwsat(ch2,CH2.offset);
    // Invert
    if(testbit(CH1ctrl,chinvert)) ch1 = 255-ch1;
    if(testbit(CH2ctrl,chinvert)) ch2 = 255-ch2;
    // Math
    sch1=(int8_t)(ch1-128); // Convert to signed char
    sch2=(int8_t)(ch2-128); // Convert to signed char
    mul=128+FMULS8(sch1,-(int8_t)sch2);    // CH1*CH2
    if(testbit(CH1ctrl,chmath)) {
        if(testbit(CH1ctrl,submult)) ch1=addwsat(ch1,-(int8_t)sch2);   // CH1+CH2
        else ch1=mul;                                                   // CH1*CH2
    }
    if(testbit(CH2ctrl,chmath)) {
        if(testbit(CH2ctrl,submult)) ch2=addwsat(ch2,-(int8_t)sch1);   // CH1+CH2
        else ch2=mul;                                                   // CH1*CH2
    }
    sum1+=ch1;
    sum2+=ch2;

    count++;
    setbit(Misc,slowacq);
    if(count>=slowval) {
        count=0;
        // Average
        if(testbit(CH1ctrl,chaverage)) ch1=sum1/slowval;
        if(testbit(CH2ctrl,chaverage)) ch2=sum2/slowval;
        sum1=0; sum2=0;
        if(testbit(Display,elastic)) {
            ch1=average(DC.CH1data[Index],ch1);
            ch2=average(DC.CH2data[Index],ch2);
        }
        DC.CH1data[Index] = ch1;
        DC.CH2data[Index] = ch2;
        DC.CHDdata[Index] = VPORT2.IN;
    }
    if(testbit(MFFT, scopemode) && !testbit(Mcursors,roll)) {  // Draw data if in scope mode
        // Draw Channel 1
        if(count==0 || !testbit(CH1ctrl,chaverage)) {
            uint8_t oldch1;
            // Apply position
            ch1=addwsat(ch1,M.CH1pos);
            ch1=ch1>>1; // Scale to LCD (128x64)
            if(ch1>MAX_Y) ch1=MAX_Y;
            oldch1=addwsat(DC.CH1data[Index-1],M.CH1pos);
            oldch1=oldch1>>1; // Scale to LCD (128x64)
            if(oldch1>MAX_Y) oldch1=MAX_Y;
            // Draw data
            if(testbit(Display, line) && Index) {
                if((ch1!=oldch1) || (ch1 && oldch1<MAX_Y))
                    if(testbit(CH1ctrl,chon)) lcd_line(Index>>1, ch1, (Index-1)>>1, oldch1);
            }
            else {
                // Don't draw when data==0 or data==63, signal could be clipping
                if(testbit(CH1ctrl,chon) && ch1 && ch1<MAX_Y) set_pixel(Index>>1, ch1);
            }
        }
        // Draw Channel 2
        if(count==0 || !testbit(CH2ctrl,chaverage)) {
            uint8_t oldch2;
            // Apply position
            ch2=addwsat(ch2,M.CH2pos);
            ch2=ch2>>1; // Scale to LCD (128x128)
            if(ch2>MAX_Y) ch2=MAX_Y;
            oldch2=addwsat(DC.CH2data[Index-1],M.CH2pos);
            oldch2=oldch2>>1; // Scale to LCD (128x128)
            if(oldch2>MAX_Y) oldch2=MAX_Y;
            // Draw data
            if(testbit(Display, line) && Index) {
                if((ch2!=oldch2) || (ch2 && oldch2<MAX_Y))
                    if(testbit(CH2ctrl,chon)) lcd_line(Index>>1, ch2, (Index-1)>>1, oldch2);
            }
            else {
                // Don't draw when data==0 or data==63, signal could be clipping
                if(testbit(CH2ctrl,chon) && ch2 && ch2<MAX_Y) set_pixel(Index>>1, ch2);
            }
        }
        // Draw digital data
        chd = VPORT2.IN;
        // Show Digital Data
        uint8_t chdpos;
        chdpos = M.CHDpos;
        if(testbit(CHDctrl,chon)) {
            uint8_t bits, bitn;
            for(bits=0x80, bitn=7; bits; bits=bits>>1, bitn--) {
                if(CHDmask&bits) {
                    if(Index==4) {
                        lcd_goto(0,chdpos>>3);
                        GLCD_Putchar('0'+bitn);    // input number
                    }
                    if(chd&bits) set_pixel(Index>>1, chdpos);
                    else {
                                                 set_pixel(Index>>1, 6+chdpos);
                        if(testbit(CHDctrl,low)) set_pixel(Index>>1, 5+chdpos);
                    }
                    chdpos+=8;   // Next position
                }
            }
        }
        // Show Parallel Hex Display
        if(testbit(CHDctrl,hexp) && ((Index&0x0F)==0) && chdpos<=56) {
            lcd_goto((Index>>1)+1,chdpos>>3);
            printhex(chd);
        }
    }
    else if(testbit(MFFT, fftmode) && testbit(Display,showset)) set_pixel(Index>>1, 63);   // FFT progress
    if(count==0) {
        Index++;
        if(Index==0) setbit(Misc,sacquired);
        if(!testbit(Mcursors,roll)) {
            if(Index==0) {
                if(testbit(CHDctrl,hexs) && testbit(MFFT, scopemode)) HEXSerial();
                TCE0.INTCTRLA = 0;  // Stop acquiring until next trigger
                if(testbit(MStatus, stop)) {
                    u8CursorX = ou8CursorX;
                    u8CursorY = ou8CursorY;
                    return;
                }
            }
            if(testbit(MFFT, scopemode) && !testbit(Display, persistent)) {
                uint8_t vline;
                uint16_t address;
                if((Index&0x01)==0) {      // erase when index is even
                    vline=127-(Index>>1);      // clear next vertical line
                    u8CursorY=0;
                    address = vline*16;
                    for(uint8_t i=0; i<16; i++) Disp_send.buffer[address++] = 0;
                }
            }
        }
        else if(testbit(MStatus,stop)) TCE0.INTCTRLA = 0;  // Stop acquiring
    }
    u8CursorX = ou8CursorX;
    u8CursorY = ou8CursorY;
}

// Interrupt for auto trigger
ISR(TCC2_LUNF_vect) {
    TCC0.INTCTRLA &= ~TC2_LUNFINTLVL_LO_gc;         // Disable Trigger timeout interrupt
    setbit(MStatus, update);
    setbit(MStatus, triggered);
}

void StartDMAs(void) {
    setbit(DMA.CH0.CTRLA,6);    // reset DMA CH0
    setbit(DMA.CH2.CTRLA,6);    // reset DMA CH2
    setbit(DMA.CH1.CTRLA,6);    // reset DMA CH1
    // DMA for ADC CH0
    DMA.CH0.ADDRCTRL  = 0b00000101;         // Source fixed, incr dest, reload dest @ end block
    DMA.CH0.TRIGSRC   = 0x10;               // ADCA CH0 is trigger source
    DMA.CH0.TRFCNT    = 512;                // buffer size
    DMA.CH0.DESTADDR0 = (((uint16_t) Temp.IN.CH1)>>0*8) & 0xFF;
    DMA.CH0.DESTADDR1 = (((uint16_t) Temp.IN.CH1)>>1*8) & 0xFF;
//    DMA.CH0.DESTADDR2 = 0;
    DMA.CH0.SRCADDR0  = (((uint16_t)(&ADCA.CH0.RESL))>>0*8) & 0xFF;
    DMA.CH0.SRCADDR1  = (((uint16_t)(&ADCA.CH0.RESL))>>1*8) & 0xFF;
//    DMA.CH0.SRCADDR2  = 0;
    DMA.CH0.CTRLA     = 0b00100100;         // repeat, 1 byte burst
    // DMA for logic
    DMA.CH2.ADDRCTRL  = 0b00000101;         // Source fixed, incr dest, reload dest @ end block
    DMA.CH2.TRIGSRC   = 0x10;               // ADCA CH0 is trigger source
    DMA.CH2.TRFCNT    = 512;                // buffer size
    DMA.CH2.DESTADDR0 = (((uint16_t) Temp.IN.CHD)>>0*8) & 0xFF;
    DMA.CH2.DESTADDR1 = (((uint16_t) Temp.IN.CHD)>>1*8) & 0xFF;
//    DMA.CH2.DESTADDR2 = 0;
    DMA.CH2.SRCADDR0  = (((uint16_t)(&VPORT2.IN))>>0*8) & 0xFF;
    DMA.CH2.SRCADDR1  = (((uint16_t)(&VPORT2.IN))>>1*8) & 0xFF;
//    DMA.CH2.SRCADDR2  = 0;
    DMA.CH2.CTRLA     = 0b00100100;         // repeat, 1 byte burst
    // DMA for ADC CH1
    DMA.CH1.ADDRCTRL  = 0b00000101;         // Source fixed, incr dest, reload dest @ end block
    DMA.CH1.TRIGSRC   = 0x20;               // ADCB CH0 is trigger source
    DMA.CH1.TRFCNT    = 512;                // buffer size
    DMA.CH1.DESTADDR0 = (((uint16_t) Temp.IN.CH2)>>0*8) & 0xFF;
    DMA.CH1.DESTADDR1 = (((uint16_t) Temp.IN.CH2)>>1*8) & 0xFF;
//    DMA.CH1.DESTADDR2 = 0;
    DMA.CH1.SRCADDR0  = (((uint16_t)(&ADCB.CH0.RESL))>>0*8) & 0xFF;
    DMA.CH1.SRCADDR1  = (((uint16_t)(&ADCB.CH0.RESL))>>1*8) & 0xFF;
//    DMA.CH1.SRCADDR2  = 0;
    DMA.CH1.CTRLA     = 0b00100100;     // repeat, 1 byte burst

    setbit(DMA.CH0.CTRLA, 7);           // Start DMAs
    setbit(DMA.CH2.CTRLA, 7);
    setbit(DMA.CH1.CTRLA, 7);

    if(Srate<6) {
        ADCA.CTRLB = 0x1C;  // signed mode, free run, 8 bit
        ADCB.CTRLB = 0x1C;  // signed mode, free run, 8 bit
    }
    else TCE0.CTRLA  = 0x02;        // Enable Timer, Prescaler: clk/2
    // Minimum time: 128us, Maximum time: 160mS
    uint16_t i=0;
    while(!testbit(DMA.CH0.CTRLB,4)) {   // Capture one full buffer of pre-trigger samples
        _delay_us(3);
        i++;
        if(testbit(MStatus,update)) {
            clrbit(MStatus,triggered);   // Invalidate data, since the user is interacting
            break;
        }
        if(i==0) break;     // timeout ~ 197mS
    }
}
