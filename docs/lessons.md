# Lessons learned

Each one is the symptom I saw, what caused it, and the fix. Most of these are C2000 things that STM32 does differently.

## GPIO and pins

**1. GPIO would not go high.**
The direction register is protected and the write was not wrapped in `EALLOW` / `EDIS`. Protected writes fail with no error. Fix: wrap every `GpioCtrlRegs` write.

**2. No clock on the SPICLK pin even though the SPI registers looked right.**
One GMUX line for GPIO60 was missing. I found it by putting the scope on the pin and seeing nothing. Fix: set both GMUX and MUX for every SPI pin. Check the pin before debugging the peripheral.

**3. Wrong bits set when shifting masks.**
`int` is 16 bits on C2000, so `1 << 19` overflows. Fix: put a `UL` suffix on constants used with shifts or `~`.

## SPI

**4. Nothing transmitted.**
`SPICTL.TALK` was not set. Master mode alone does not enable the output.

**5. Byte sent but the chip ignored it.**
An 8-bit word has to be left-justified in the 16-bit `SPITXBUF`. Fix: shift left by 8.

**6. Wait loop never exited, or never waited.**
Two causes. I copied the flag into a variable once, so it never updated. I also used `~flag` where I meant `flag == 0`. Fix: a macro that reads the register bit every time, and a plain comparison.

## AD2S1210

**7. Register read returned the value from the previous command.**
The reply is one transfer behind. Fix: send the address twice and keep the second reply.

**8. Fault register showed an old value.**
Fault data only updates on a falling edge of SAMPLE. Fix: pulse SAMPLE before the read.

**9. Registers kept old or scrambled values after a power cycle, and the reset never seemed to happen.**
My first theories were noise on floating lines, reset timing and back-powering. None of them held up. Measuring the board's 3.3 V rail showed it only dropped to about 2.2 V at power-off and then decayed slowly. A quick power cycle was really a brown-out, and the board's RC reset never fired. Fix: wait until the rail reads about 10 mV before powering on. After a clean reset with no resolver the registers read 0x91 = 0x28, 0x92 = 0x7E, fault = 0x70, with D0 clear.

**10. Do not trust the power-up state.**
At startup, write the registers that matter, read them back, and check D0 of the fault register.

**11. The parity error bit is not an SPI problem.**
The chip works out and stores parity itself when a register is written. D0 sets when the stored contents change afterwards, for example in a brown-out. A wrong clock edge would look different. Every transfer would come back shifted by one bit.

**12. Lines float between power-on and the GPIO init.**
The C2000 only has internal pull-ups, they are off at reset, and there are no pull-downs. During that window the mode pins and WR/FSYNC are not driven. The hardware fix would be 10 kΩ pull-downs on A0 and A1 and a 10 kΩ pull-up on WR/FSYNC. These were not fitted on my setup and I saw no problems without them.

## CCS and the project

**13. Board ran from the debugger but did nothing standalone.**
The RAM config was active, and the flash config was missing `_LAUNCHXL_F28379D` and `_FLASH`. Fix: set the symbols and build the flash config.

**14. "Unable to step, AET resources" in the flash build.**
Flash builds only have about two hardware breakpoints. Fix: remove breakpoints or debug from RAM.

**15. 85 linker errors, "memory range has already been specified".**
CCS added a second linker `.cmd` when I changed the device variant. Fix: exclude the extra one.

**16. Variable empty in the watch window.**
Locals get optimised away, and globals are not zeroed unless they are initialised. Fix: watch globals, and initialise them.

**17. Bitfield code did nothing in a driverlib-only project.**
The header only declares the register structs. Two more files make them real. `F2837xD_GlobalVariableDefs.c` creates them, and `F2837xD_Headers_nonBIOS_cpu1.cmd` places them at the hardware addresses. Without the `.cmd` the build still passes and the writes land in RAM. Check: `&SpiaRegs` should be 0x6100 in the debugger. STM32 does not have this step because its headers hold the address as a pointer.

**18. The debugger graph is not evenly sampled.**
CCS plots a value whenever the debugger reads one. So the slope is not speed, and the peaks can miss 0 and 360. For a real plot, log into an array inside a timed interrupt.

## Bitfield vs driverlib

What each bitfield step in this project would be in driverlib.

| Bitfield | Driverlib |
|---|---|
| GMUX / MUX lines for a pin | `GPIO_setPinConfig(GPIO_58_SPISIMOA)` |
| `GPxDIR` bit | `GPIO_setDirectionMode(pin, GPIO_DIR_MODE_OUT)` |
| `GPxSET` / `GPxCLEAR` | `GPIO_writePin(pin, 1)` / `GPIO_writePin(pin, 0)` |
| `SPISWRESET` 0 / 1 | `SPI_disableModule()` / `SPI_enableModule()` |
| polarity, phase, master, TALK, SPIBRR, SPICHAR | `SPI_setConfig(...)` |
| `SPIPRI.FREE = 1` | `SPI_setEmulationMode(..., SPI_EMULATION_FREE_RUN)` |
