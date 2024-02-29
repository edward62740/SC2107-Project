// RSLK
#include "msp.h"
#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include "..\inc\UART0.h"
#include "..\inc\EUSCIA0.h"
#include "..\inc\FIFO0.h"
#include "..\inc\Clock.h"
//#include "..\inc\SysTick.h"
#include "..\inc\SysTickInts.h"
#include "..\inc\CortexM.h"
#include "..\inc\TimerA1.h"
//#include "..\inc\Bump.h"
#include "..\inc\BumpInt.h"
#include "..\inc\LaunchPad.h"
#include "..\inc\Motor.h"
#include "../inc/IRDistance.h"
#include "../inc/ADC14.h"
#include "../inc/LPF.h"
#include "..\inc\Reflectance.h"
#include "../inc/TA3InputCapture.h"
#include "../inc/Tachometer.h"

#define P2_4 (*((volatile uint8_t *)(0x42098070)))
#define P2_3 (*((volatile uint8_t *)(0x4209806C)))
#define P2_2 (*((volatile uint8_t *)(0x42098068)))
#define P2_1 (*((volatile uint8_t *)(0x42098064)))
#define P2_0 (*((volatile uint8_t *)(0x42098060)))

void AppMotorTest(void);
void AppIRTest(void);
void AppBumperTest(void);
void AppRefTest(void);
void AppTachTest(void);
void AppAdvBumpTest(void);
void AppLineFollowerTest(void);

//#define DEBUG

/********* APP UTIL ***********/
const char *conv_int2byte(uint8_t x)
{
    static char b[9];
    b[0] = '\0';

    uint8_t z;
    for (z = 128; z > 0; z >>= 1)
    {
        strcat(b, ((x & z) == z) ? "1" : "0");
    }

    return b;
}


static void fsm_led(uint8_t v)
{
    LaunchPad_Output((v==0)<<1|(v==1)<<0|(v==2)<<2);
}

typedef enum
{
    SEND, REPLACE
} eUART_SEND;

static void AppUARTSend(char *str, eUART_SEND t)
{
    if (t == REPLACE)
        UART0_OutString("\033[A\33[2K\r");
    UART0_OutString(str);
    EUSCIA0_OutChar(CR);
    EUSCIA0_OutChar(LF);
}
/*****************************/

/* [7] FSM */

typedef enum
{
    STATE_OK = 0,
    STATE_L,
    STATE_R,
    STATE_ERR = -1
} eStateFsm;
typedef struct
{
    eStateFsm state;
    uint32_t iterations;
    void(*fp_led)(uint8_t);

} fsm_t ;
static void _proc_fsm(fsm_t *fsm, int32_t sig);
static const uint32_t STATE_PERSIST_DUR_MS = 25;
static uint32_t MOTOR_SPEED = 1200;

/***********/


void RSLK_Reset(void)
{
    DisableInterrupts();

    LaunchPad_Init();

    EnableInterrupts();
}

void TimedPause(uint32_t time)
{
    Clock_Delay1ms(time);          // run for a while and stop
    Motor_Stop();
}

// variables for ir sensors
volatile uint32_t ADCvalue;
volatile uint32_t ADCflag;
volatile uint32_t nr, nc, nl;


// variables for bumper
static volatile uint8_t bumpIntData, isCollided;
void handlerGpio4Bump(uint8_t v)
{
    Motor_Stop();
    bumpIntData = v;
    isCollided = 1;
    P4->IFG &= ~0xED; // clear interrupt flags

}
uint8_t bump_data;
bool bump[6] = { 0 };
char bumperBuf[30];


void handlerAdcRead(void)
{  // runs at 2000 Hz
    uint32_t raw17, raw12, raw16;
    P1OUT ^= 0x01;         // profile
    P1OUT ^= 0x01;         // profile
    ADC_In17_12_16(&raw17, &raw12, &raw16);  // sample
    nr = LPF_Calc(raw17);  // right is channel 17 P9.0
    nc = LPF_Calc2(raw12);  // center is channel 12, P4.1
    nl = LPF_Calc3(raw16);  // left is channel 16, P9.1
    ADCflag = 1;           // semaphore
    P1OUT ^= 0x01;         // profile
}





// tachometer variables
uint16_t Period0;              // (1/SMCLK) units = 83.3 ns units
uint16_t First0 = 0;             // Timer A3 first edge, P10.4
uint32_t Done0 = 0;              // set each rising

uint16_t Period2;              // (1/SMCLK) units = 83.3 ns units
uint16_t First2 = 0;             // Timer A3 first edge, P8.2
uint32_t Done2 = 0;              // set each rising
uint32_t LeftRPM = 0, RightRPM = 0;

// max period is (2^16-1)*83.3 ns = 5.4612 ms
// min period determined by time to run ISR, which is about 1 us
void PeriodMeasure0(uint16_t time)
{
    Period0 = (time - First0) & 0xFFFF; // 16 bits, 83.3 ns resolution
    First0 = time;                    // setup for next
    Done0++;
}

// max period is (2^16-1)*83.3 ns = 5.4612 ms
// min period determined by time to run ISR, which is about 1 us
void PeriodMeasure2(uint16_t time)
{
    Period2 = (time - First2) & 0xFFFF; // 16 bits, 83.3 ns resolution
    First2 = time;                    // setup for next
    Done2++;
}



// RSLK Self-Test
// Sample program of how the text based menu can be designed.
// Only one entry (RSLK_Reset) is coded in the switch case. Fill up with other menu entries required for Lab5 assessment.
// Init function to various peripherals are commented off.  For reference only. Not the complete list.

int main(void)
{
    uint32_t cmd = 0xDEAD, menu = 0;

    DisableInterrupts();
    Clock_Init48MHz();  // makes SMCLK=12 MHz
    //SysTick_Init(48000,2);  // set up SysTick for 1000 Hz interrupts
    Motor_Init();
    //Motor_Stop();
    LaunchPad_Init();
    BumpInt_Init(&handlerGpio4Bump);
    //Bumper_Init();
    //IRSensor_Init();
    //Tachometer_Init();
    EUSCIA0_Init();     // initialize UART
    EnableInterrupts();

    while (1)
    {                     // Loop forever
        // write this as part of Lab 5
        EUSCIA0_OutChar(CR);
        EUSCIA0_OutChar(LF);
        EUSCIA0_OutString("RSLK Self-Test (Basic + Adv) UI");
        EUSCIA0_OutChar(CR);
        EUSCIA0_OutChar(LF);
        EUSCIA0_OutString("[0] RSLK Reset");
        EUSCIA0_OutChar(CR);
        EUSCIA0_OutChar(LF);
        EUSCIA0_OutString("[1] Motor Test");
        EUSCIA0_OutChar(CR);
        EUSCIA0_OutChar(LF);
        EUSCIA0_OutString("[2] IR Sensor Test");
        EUSCIA0_OutChar(CR);
        EUSCIA0_OutChar(LF);
        EUSCIA0_OutString("[3] Bumper Test");
        EUSCIA0_OutChar(CR);
        EUSCIA0_OutChar(LF);
        EUSCIA0_OutString("[4] Reflectance Sensor Test");
        EUSCIA0_OutChar(CR);
        EUSCIA0_OutChar(LF);
        EUSCIA0_OutString("[5] Tachometer Test");
        EUSCIA0_OutChar(CR);
        EUSCIA0_OutChar(LF);
        EUSCIA0_OutString("[6][ADV] Simultaneous Bump Switches Detection");
        EUSCIA0_OutChar(CR);
        EUSCIA0_OutChar(LF);
        EUSCIA0_OutString("[7][ADV] Line follower test");
        EUSCIA0_OutChar(CR);
        EUSCIA0_OutChar(LF);

        EUSCIA0_OutString("CMD: ");
        cmd = EUSCIA0_InUDec();
        EUSCIA0_OutChar(CR);
        EUSCIA0_OutChar(LF);
        isCollided = 0;
// \033[A\33[2K\r VT100 code to clear prev line
        switch (cmd)
        {
        case 0:
            RSLK_Reset();
            menu = 1;
            cmd = 0xDEAD;
            AppUARTSend("SYSTEM RESET", SEND);
            break;

        case 1:
            // Motor Test
            AppUARTSend("STARTING MOTOR TEST", SEND);
            AppMotorTest();
            AppUARTSend("MOTOR TEST DONE. Returning.", SEND);
            break;

        case 2:
            // IR Sensor test
            AppUARTSend("STARTING IR SENSOR TEST", SEND);
            AppIRTest();
            AppUARTSend("IR SENSOR TEST DONE. Returning.", SEND);
            break;

        case 3:
            // Bumpers
            AppUARTSend("STARTING BUMPER TEST", SEND);
            AppBumperTest();
            AppUARTSend("BUMPER TEST DONE. Returning.", SEND);

            break;

        case 4:
            // reflectance
            AppUARTSend("STARTING REFLECTANCE SENSOR TEST", SEND);
            AppRefTest();
            AppUARTSend("REFLECTANCE SENSOR TEST DONE. Returning.", SEND);
            break;

        case 5:

            // Tachometer test
            AppUARTSend("STARTING TACHOMETER SENSOR TEST", SEND);

            AppTachTest();
            AppUARTSend("TACHOMETER SENSOR TEST DONE. Returning.", SEND);
            break;

        case 6:
            // Bumpers

            AppUARTSend("[ADV] Simultaneous bump switches detection", SEND);
            AppUARTSend(" ", SEND);
            AppAdvBumpTest();

            AppUARTSend(
                    "Simultaneous bump switches detection TEST DONE. Returning.",
                    SEND);
            break;
        case 7:
            // Bumpers

            AppUARTSend("[ADV] Line follower test. Unplug USB and press user button. ", SEND);
            AppUARTSend(" ", SEND);
            AppLineFollowerTest();

            AppUARTSend(
                    "Line follower TEST DONE. Returning.",
                    SEND);
            break;

        default:
            menu = 1;
            break;
        }

        if (!menu)
            Clock_Delay1ms(1000);

        else
        {
            menu = 0;
        }

    }
}

/*
 * This function uses timer PWM to control the motor speed.
 * It stops when all tests are complete.
 */
void AppMotorTest(void)
{
    AppUARTSend("DIRECTION: FORWARD", REPLACE);
    Motor_Forward(3000, 3000);
    TimedPause(2000);
    AppUARTSend("DIRECTION: BACKWARD", REPLACE);
    Motor_Backward(3000, 3000);
    TimedPause(2000);
    AppUARTSend("DIRECTION: RIGHT", REPLACE);
    Motor_Right(3000, 3000);
    TimedPause(2000);
    AppUARTSend("DIRECTION: LEFT", REPLACE);
    Motor_Left(3000, 3000);
    TimedPause(2000);
    AppUARTSend("RIGHT MOTOR ONLY", REPLACE);
    Motor_Forward(3000, 0);
    TimedPause(2000);
    AppUARTSend("LEFT MOTOR ONLY", REPLACE);
    Motor_Forward(0, 3000);
    TimedPause(2000);
    Motor_Stop();
}

/*
 * This function initializes ADC, runs a single sample to initialize lpf ringbuf,
 * then init timer a1 with fp to handlerAdcRead() to be called at 2000Hz. this will acquire
 * the raw ADC values and set the flag high.
 * the spinloop in the program will monitor the adc flag and print the values if it is set,
 * and exit the program if bumpswitch collision flag is set.
 */
void AppIRTest(void)
{
    isCollided = 0;

    char buf[100];
    uint32_t raw17, raw12, raw16;
    uint32_t s, n;

    ADCflag = 0;
    s = 256; // replace with your choice

    ADC0_InitSWTriggerCh17_12_16();   // initialize channels 17,12,16

    ADC_In17_12_16(&raw17, &raw12, &raw16);  // sample

    LPF_Init(raw17, s);     // P9.0/channel 17
    LPF_Init2(raw12, s);     // P4.1/channel 12
    LPF_Init3(raw16, s);     // P9.1/channel 16

    //UART0_Init();          // initialize UART0 115,200 baud rate

    TimerA1_Init(&handlerAdcRead, 250);    // 2000 Hz sampling
    EnableInterrupts();

    while (1)
    {
        for (n = 0; n < 250; n++)
        {
            while (ADCflag == 0)
                ;
            ADCflag = 0; // show every nth point
        }
        if (isCollided)
        {

            isCollided = 0;
            return;
        }
        sprintf(buf, "%5dcm,%5dcm,%5dcm", LeftConvert(nl), CenterConvert(nc),
                RightConvert(nr));
        AppUARTSend(buf, REPLACE);
    }

}

/*
 * This function uses reads the status of the bump switches from Bump_Read()
 * on demand. this function is also asynchronously called by the gpio4handler.
 * It exits when all bump switches have been pressed.
 */
void AppBumperTest(void)
{
    isCollided = 0;

    while (!bump[0] || !bump[1] || !bump[2] || !bump[3] || !bump[4] || !bump[5])
    {
        bump_data = Bump_Read();

        for (uint8_t i = 0; i < 6; i++)
        {
            if ((((bump_data >> i & 0xF) & 0x1)) == 0 && bump[i] == 0)
            {
                bump[i] = true;
                char buf[30];
                snprintf(buf, 30, "BUMPER %d PRESSED", i);
                AppUARTSend(buf, REPLACE);

            }
        }
    }
    for (uint8_t i = 0; i < 6; i++)  bump[i] = false; // rst state

}

/*
 * This function reads from the reflectance sensors and prints out their value
 * It exits when isCollided is set from gpio4 interrupt.
 */
void AppRefTest(void)
{
    isCollided = 0;

    uint8_t Data; // QTR-8RC
    int32_t Position; // 332 is right, and -332 is left of center

    char refBuf[20] = "";

    while (1)
    {

        if (isCollided)
        {

            isCollided = 0;
            break;
        }
        Data = Reflectance_Read(2000);
        Position = Reflectance_Position(Data);

            sprintf(refBuf, "%s, %d mm", conv_int2byte(Data), Position / 10);
            AppUARTSend(refBuf, REPLACE);


        Clock_Delay1ms(100);

    }
}

/*
 * This function init timer a3 in capture mode, starts motor, places the cpu in WFI, prints tachometer
 * info for each 100 exits from WFI, and exits if collided.
 */
void AppTachTest(void)
{
    isCollided = 0;
    EnableInterrupts();
    uint32_t count = 0;
    char buf[100];
    TimerA3Capture_Init(&PeriodMeasure0, &PeriodMeasure2);
    TimedPause(500);
    Motor_Forward(1500, 1500);
    EnableInterrupts();
    while (1)
    {
        //WaitForInterrupt();
        __WFI();
        count++;
        if (count % 100 == 0)
        {
            memset(buf, 0, sizeof(buf));
            // (pulse per rot * pulse dur * interval between pulse / 1k) = time taken for 1 rot in ms
            // ms in a minute / ^ = RPM
            LeftRPM = ((60 * 1000 * 1000) / (360 * 83.3 * Period0 / 1000));
            RightRPM = ((60 * 1000 * 1000) / (360 * 83.3 * Period2 / 1000));

            sprintf(buf, "Right RPM = %2d, Left RPM = %2d, Right count = %2d, Left count = %2d", RightRPM, LeftRPM, Period2, Period0);

            AppUARTSend(buf, REPLACE);
            count = 0;
        }
        if (isCollided)
        {

            isCollided = 0;
            return;
        }
    }
}

/*
 * This function also tests bump switches but allows for multiple simultaneous presses.
 * Exits when all are pressed
 */
void AppAdvBumpTest(void)
{
    while (1)
    {

        int pressed = 0;
        strcpy(bumperBuf, "Pressed on switches: ");
        bump_data = Bump_Read();
        if ((((bump_data >> 0) & 0x1)) == 0)
        {
            strcat(bumperBuf, "0 ");
            pressed = 1;
        }
        if ((((bump_data >> 1) & 0x1)) == 0)
        {
            strcat(bumperBuf, "1 ");
            pressed = 1;
        }
        if ((((bump_data >> 2) & 0x1)) == 0)
        {
            strcat(bumperBuf, "2 ");
            pressed = 1;
        }
        if ((((bump_data >> 3) & 0x1)) == 0)
        {
            strcat(bumperBuf, "3 ");
            pressed = 1;
        }
        if ((((bump_data >> 4) & 0x1)) == 0)
        {
            strcat(bumperBuf, "4 ");
            pressed = 1;
        }
        if ((((bump_data >> 5) & 0x1)) == 0)
        {
            strcat(bumperBuf, "5 ");
            pressed = 1;
        }

        if (!pressed)
        {
            AppUARTSend("Waiting for press", REPLACE);
        }
        else
            AppUARTSend(bumperBuf, REPLACE);

        if ((bump_data & 0b111111) == 0)
        {

            return;
        }

        Clock_Delay1ms(100);

    }
}

/*
 * This function runs a simple FSM which contain 3 states: left, right, center. The motor direction is determined by the state.
 * The motor speed is determined by the IR sensor readings; under some threshold, i.e. 20cm, it will slow down slightly,
 * if no line is detected, the function will reverse the robot back onto the line
 * the fsm state is indicated by the led color through the fp passed to fsm instance, and
 * this function terminates on bump switch collision.
 */
void AppLineFollowerTest(void)
{
    while(LaunchPad_Input()==0);  // wait for touch
    while(LaunchPad_Input());     // wait for release
    uint8_t Data; // QTR-8RC
    int32_t Position; // 332 is right, and -332 is left of center
    Data = Reflectance_Read(2000);
    Position = Reflectance_Position(Data);
    char buf[50];
    fsm_t fsm;
    fsm.state = STATE_OK;
    fsm.fp_led = fsm_led;
    bool firstRun = true;
    uint32_t raw17, raw12, raw16;
    uint32_t s, n;

    ADCflag = 0;
    s = 256; // replace with your choice
    ADC0_InitSWTriggerCh17_12_16();   // initialize channels 17,12,16

    ADC_In17_12_16(&raw17, &raw12, &raw16);  // sample

    LPF_Init(raw17, s);     // P9.0/channel 17
    LPF_Init2(raw12, s);     // P4.1/channel 12
    LPF_Init3(raw16, s);     // P9.1/channel 16

    TimerA1_Init(&handlerAdcRead, 250);    // 2000 Hz sampling


    while (!isCollided)
    {

        Clock_Delay1ms(STATE_PERSIST_DUR_MS);
        Data = Reflectance_Read(2000);
        Position = Reflectance_Position(Data);
        sprintf(buf, "%s, %d, FSM: %d ", conv_int2byte(Data), Position, fsm.state);
        LaunchPad_LED(Data == 0? 1: 0);

        if(ADCflag) // change speed of motor
        {
            ADCflag = 0;
            if(LeftConvert(nl) < 20 || CenterConvert(nc) < 20 || RightConvert(nr) < 20) MOTOR_SPEED = 500;
            else if(LeftConvert(nl) < 13 || CenterConvert(nc) < 13 || RightConvert(nr) < 13) MOTOR_SPEED = 0;
            else MOTOR_SPEED = 1200;

        }

        if(Data == 0) // detect if there is no line
        {
            if(fsm.state == STATE_R) Motor_Left(MOTOR_SPEED, MOTOR_SPEED);
            else Motor_Right(MOTOR_SPEED, MOTOR_SPEED);

            Clock_Delay1ms(400); // delay for turn
        }

        AppUARTSend(buf, REPLACE);
        _proc_fsm(&fsm, Position); // run state transitions
        firstRun = false;

    }
    Motor_Stop();
    isCollided = 0;
}

static void _proc_fsm(fsm_t *fsm, int32_t sig)
{

    fsm->fp_led(fsm->state);
    switch (fsm->state)
    {
    char buf[50];
#ifdef DEBUG
    sprintf(buf, "%d, %d", sig, (uint8_t)fsm->state);
#endif
    AppUARTSend(buf, SEND);
    case STATE_OK:
    {
        Motor_Forward(MOTOR_SPEED, MOTOR_SPEED);

        if (sig > 100)
        {
            fsm->state = STATE_L;
        }
        else if (sig < -100)
        {
            fsm->state = STATE_R;
        }
        break;

    }
    case STATE_L:
    {
        Motor_Forward(MOTOR_SPEED / 3, MOTOR_SPEED);
        if (sig <= 100)
        {
            fsm->state = STATE_OK;
        }
        break;

    }
    case STATE_R:
    {
        Motor_Forward(MOTOR_SPEED, MOTOR_SPEED / 3);
        if (sig >= -100)
        {
            fsm->state = STATE_OK;
        }
        break;

    }

    default:
    {
        fsm->state = STATE_ERR;
        break;
    }

    }

}

#if 0
//Sample program for using teh UART related functions.
int Program5_4(void)
{
//int main(void){
    // demonstrates features of the EUSCIA0 driver
    char ch;
    char string[20];
    uint32_t n;
    DisableInterrupts();
    Clock_Init48MHz();// makes SMCLK=12 MHz
    EUSCIA0_Init();// initialize UART
    EnableInterrupts();
    EUSCIA0_OutString("\nLab 5 Test program for EUSCIA0 driver\n\rEUSCIA0_OutChar examples\n");
    for(ch='A'; ch<='Z'; ch=ch+1)
    {     // print the uppercase alphabet
        EUSCIA0_OutChar(ch);
    }
    EUSCIA0_OutChar(LF);
    for(ch='a'; ch<='z'; ch=ch+1)
    {     // print the lowercase alphabet
        EUSCIA0_OutChar(ch);
    }
    while(1)
    {
        EUSCIA0_OutString("\n\rInString: ");
        EUSCIA0_InString(string,19); // user enters a string
        EUSCIA0_OutString(" OutString="); EUSCIA0_OutString(string); EUSCIA0_OutChar(LF);

        EUSCIA0_OutString("InUDec: "); n=EUSCIA0_InUDec();
        EUSCIA0_OutString(" OutUDec="); EUSCIA0_OutUDec(n); EUSCIA0_OutChar(LF);
        EUSCIA0_OutString(" OutUFix1="); EUSCIA0_OutUFix1(n); EUSCIA0_OutChar(LF);
        EUSCIA0_OutString(" OutUFix2="); EUSCIA0_OutUFix2(n); EUSCIA0_OutChar(LF);

        EUSCIA0_OutString("InUHex: "); n=EUSCIA0_InUHex();
        EUSCIA0_OutString(" OutUHex="); EUSCIA0_OutUHex(n); EUSCIA0_OutChar(LF);
    }
}
#endif
