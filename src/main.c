//
// AD2S1210 resolver to digital converter on a TI LAUNCHXL-F28379D, over SPI A.
//
// Register level (bitfield) code written from the TRM and datasheets.
// The only TI helper calls are Device_init() and DEVICE_DELAY_US(),
// both from TI's device.c / device.h.
//
// Pins (full table in docs/wiring.md):
//   GPIO58  SPISIMOA  -> SDI
//   GPIO59  SPISOMIA  <- SDO
//   GPIO60  SPICLKA   -> SCLK
//   GPIO61  SPISTEA   -> WR/FSYNC
//   GPIO19  (J1 pin 3)  -> A0 / A1   mode select
//   GPIO32  (J1 pin 2)  -> A0 / A1   mode select
//   GPIO40  (J5 pin 50) -> SAMPLE
//
// Clocks: SYSCLK 200 MHz, LSPCLK 50 MHz, SPI clock 50 MHz / (49 + 1) = 1 MHz.
//

#include "F28x_Project.h"
#include "driverlib.h"
#include "device.h"


// Reads the SPI status bit every time it is used (INT_FLAG clears when SPIRXBUF is read)
#define SPI_FLAG  (SpiaRegs.SPISTS.bit.INT_FLAG)

// Globals so they show up in the CCS watch window
Uint16 SPI_RX;
Uint16 FAULT_RX;            // fault register (0xFF)
Uint16 control_reg;         // control register (0x92)
volatile Uint16 position;   // raw angle, 0 to 65535 counts
volatile float real_pos = 0;    // angle in electrical degrees


//
// GPIO: three plain outputs (A0, A1, SAMPLE) and the four SPI A pins
//
void GPIO_init(){

    EALLOW;     // GpioCtrlRegs are protected

    //J1 PIN 2 (GPIO32) set to Output. Mux 0 = GPIO, the reset default
    GpioCtrlRegs.GPBGMUX1.all |= 0x00UL;
    GpioCtrlRegs.GPBMUX1.all |= 0x00UL;
    GpioCtrlRegs.GPBDIR.all |= 0x1UL;

    //J1 PIN 3 (GPIO19) set to Output
    GpioCtrlRegs.GPAGMUX2.all |= 0x00UL;
    GpioCtrlRegs.GPAMUX2.all |= 0x00UL;
    GpioCtrlRegs.GPADIR.all |= 1UL<<19;

    //J5 PIN 50 (GPIO40) set to Output, drives SAMPLE
    GpioCtrlRegs.GPBGMUX1.all |= 0x00UL;
    GpioCtrlRegs.GPBMUX1.all |= 0x00UL;
    GpioCtrlRegs.GPBDIR.bit.GPIO40 = 0x1UL;

    // SPI A is mux option 15 on GPIO58 to 61: GMUX = 3 and MUX = 3 on every pin
    //SPISIMO A
    GpioCtrlRegs.GPBGMUX2.bit.GPIO58 = 0x3UL;
    GpioCtrlRegs.GPBMUX2.bit.GPIO58 = 0x3UL;
    //SPISOMI A
    GpioCtrlRegs.GPBGMUX2.bit.GPIO59 = 0x3UL;
    GpioCtrlRegs.GPBMUX2.bit.GPIO59 = 0x3UL;
    //SPICLK A
    GpioCtrlRegs.GPBGMUX2.bit.GPIO60 = 0x3UL;
    GpioCtrlRegs.GPBMUX2.bit.GPIO60 = 0x3UL;
    //SPISTE A_bar, used as WR/FSYNC
    GpioCtrlRegs.GPBGMUX2.bit.GPIO61 = 0x3UL;
    GpioCtrlRegs.GPBMUX2.bit.GPIO61 = 0x3UL;

    EDIS;
}

//
// SPI A: master, idle low clock, no phase delay, 1 MHz, 8 bit words, no FIFO
//
void SPI_init(){

    SpiaRegs.SPICCR.bit.SPISWRESET = 0x0UL;     // hold in reset while configuring
    SpiaRegs.SPICTL.bit.MASTER_SLAVE = 0x1UL;
    SpiaRegs.SPICTL.bit.TALK = 0x1UL;           // master alone does not enable the output

    SpiaRegs.SPICCR.bit.CLKPOLARITY = 0x0UL;
    SpiaRegs.SPICTL.bit.CLK_PHASE = 0x0UL;

    SpiaRegs.SPIBRR.bit.SPI_BIT_RATE = 49; // 50 MHz / (49+1) = 1 MHz clock
    SpiaRegs.SPICCR.bit.SPICHAR = 0x7UL; //8 bit message size
    SpiaRegs.SPIPRI.bit.FREE = 1; //Allows SPI clock to run in debug
    SpiaRegs.SPICCR.bit.SPISWRESET = 0x1UL;     // release reset last

    //SpiaRegs.SPICCR.bit.SPILBK = 1;
}

//
// A0 = A1 = 1: configuration mode, 8 bit register access
//
void AD2S_Config_mode(){

    //Sets A0,A1 to 11 for configuration mode
    GpioDataRegs.GPASET.all = 1UL<<19; //J1 PIN3 (GPIO19)
    GpioDataRegs.GPBSET.all = 0x1UL; //J1 PIN2 (GPIO32)
    SpiaRegs.SPICCR.bit.SPICHAR = 0x7UL;    // 8 bit words for register access

}

//
// A0 = A1 = 0: normal mode, 16 bit position words
//
void AD2S_Normal_mode(){

    //Sets A0,A1 to 00 for normal mode
    GpioDataRegs.GPACLEAR.all = 1UL<<19; //J1 PIN3 (GPIO19)
    GpioDataRegs.GPBCLEAR.all = 0x1UL; //J1 PIN2 (GPIO32)

    SpiaRegs.SPICCR.bit.SPISWRESET = 0;     // SPI into reset to change the word size
    SpiaRegs.SPICCR.bit.SPICHAR = 0xF;      // 16 bit words for the position
    SpiaRegs.SPICCR.bit.SPISWRESET = 1;     // and back out of reset

}

//
// Send one byte in configuration mode (an address or a data byte).
// 8 bit words must be left justified in SPITXBUF, hence the << 8.
//
void SPI_Tx_polling(Uint16 data){

    SpiaRegs.SPITXBUF = (data)<<8;
    while(SPI_FLAG==0){}
    SPI_RX = SpiaRegs.SPIRXBUF;

}

//
// Read one register in configuration mode.
// The reply is one transfer behind, so the address is sent twice
// and the second reply is kept.
//
Uint16 SPI_Rx_polling(Uint16 data){

    //Pulse the sample pin ON/OFF (falling edge latches fault data)
    GpioDataRegs.GPBSET.bit.GPIO40 = 0x1UL;
    DEVICE_DELAY_US(1);
    GpioDataRegs.GPBCLEAR.bit.GPIO40 = 0x1UL;

    SpiaRegs.SPITXBUF = (data)<<8;
    while(SPI_FLAG==0){}
    SPI_RX = SpiaRegs.SPIRXBUF;

    //Send the address again as dummy byte to shift in the AD2S' answer
    SpiaRegs.SPITXBUF = (data)<<8;
    while(SPI_FLAG==0){}
    SPI_RX = SpiaRegs.SPIRXBUF;

    return SPI_RX;
}

//
// Normal mode position read.
// Pulse SAMPLE, then one 16 bit transfer. The reply is the angle.
//
Uint16 SPI_Rx_normal(){

    Uint16 data = 0x5555;   // dummy word, only sent to generate the 16 clocks

    //Pulse the sample pin ON/OFF (falling edge latches the position)
    GpioDataRegs.GPBSET.bit.GPIO40 = 0x1UL;
    DEVICE_DELAY_US(1);
    GpioDataRegs.GPBCLEAR.bit.GPIO40 = 0x1UL;

    //Clock out 16 bits to shift in the angle (16 bit word, no shift needed)
    SpiaRegs.SPITXBUF = (data);
    while(SPI_FLAG==0){}
    SPI_RX = SpiaRegs.SPIRXBUF;

    return SPI_RX;
}


void main(void)
{
    GpioDataRegs.GPBSET.bit.GPIO40 = 0x1UL;     // SAMPLE latch high before the pin is an output
    Device_init();                              // clocks, PLL, watchdog (TI device.c)
    DEVICE_DELAY_US(100000);                    // 100 ms for the converter board to settle

    GPIO_init();
    SPI_init();
    EINT;   // enable global interrupts (clears INTM)
    ERTM;

    //
    // Bench verification (configuration mode)
    // Used on the home bench to prove register access before the real
    // resolver test: read the excitation frequency, write it, then read
    // the fault and control registers. Check the values in the watch window.
    //
    AD2S_Config_mode();

    SPI_Rx_polling(0x91U);              // read excitation frequency register

    SPI_Tx_polling(0x91U);              // address: excitation frequency
    SPI_Tx_polling(0x28U);              // data: 0x28 x 250 Hz = 10 kHz

    FAULT_RX = SPI_Rx_polling(0xFFU);   // fault register, 0x00 with a good resolver

    control_reg = SPI_Rx_polling(0x92U);    // control register, default 0x7E

    //
    // Normal mode: position read
    //
    AD2S_Normal_mode();

    while (1){

        position = SPI_Rx_normal();                     // 0 to 65535 counts
        real_pos = (position/65536.0f)*360.0f;          // electrical degrees, 65536 counts = 360

    }

}
