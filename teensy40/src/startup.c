#include "imxrt.h"

#define F_CPU 600000000
extern volatile uint32_t F_CPU_ACTUAL;

// from the linker
extern unsigned long _stextload;
extern unsigned long _stext;
extern unsigned long _etext;
extern unsigned long _sdataload;
extern unsigned long _sdata;
extern unsigned long _edata;
extern unsigned long _sbss;
extern unsigned long _ebss;
extern unsigned long _flexram_bank_config;
extern unsigned long _estack;
extern unsigned long _flashimagelen;

__attribute__ ((used, aligned(1024)))
void (* _VectorsRam[NVIC_NUM_INTERRUPTS+16])(void);

static void memory_copy(uint32_t *dest, const uint32_t *src, uint32_t *dest_end);
static void memory_clear(uint32_t *dest, uint32_t *dest_end);
static void reset_PFD();
void configure_cache(void);
void unused_interrupt_vector(void);
extern void tempmon_init(void);
uint32_t set_arm_clock(uint32_t frequency);

extern int main(void);

__attribute__((section(".startup"), optimize("no-tree-loop-distribute-patterns"), naked))
void ResetHandler(void)
{
  unsigned int i;

  IOMUXC_GPR_GPR17 = (uint32_t)&_flexram_bank_config;
  IOMUXC_GPR_GPR16 = 0x00200007;
  IOMUXC_GPR_GPR14 = 0x00AA0000;
  __asm__ volatile("mov sp, %0" : : "r" ((uint32_t)&_estack) : );

  PMU_MISC0_SET = 1<<3; // Use bandgap-based bias currents for best performance (Page 1175)

  // Initialize memory
  memory_copy(&_stext, &_stextload, &_etext);
  memory_copy(&_sdata, &_sdataload, &_edata);
  memory_clear(&_sbss, &_ebss);

  // enable FPU
  SCB_CPACR = 0x00F00000;

  // set up blank interrupt & exception vector table
  for (i=0; i < NVIC_NUM_INTERRUPTS + 16; i++) _VectorsRam[i] = &unused_interrupt_vector;
  for (i=0; i < NVIC_NUM_INTERRUPTS; i++) NVIC_SET_PRIORITY(i, 128);
  SCB_VTOR = (uint32_t)_VectorsRam;

  // Set the PLL's Phase Fractional Dividers to the default frequencies
  reset_PFD();
	
  // Configure serial clocks
  // PIT & GPT timers to run from 24 MHz clock (independent of CPU speed)
  CCM_CSCMR1 = (CCM_CSCMR1 & ~CCM_CSCMR1_PERCLK_PODF(0x3F)) | CCM_CSCMR1_PERCLK_CLK_SEL;
  // UARTs run from 24 MHz clock (works if PLL3 off or bypassed)
  CCM_CSCDR1 = (CCM_CSCDR1 & ~CCM_CSCDR1_UART_CLK_PODF(0x3F)) | CCM_CSCDR1_UART_CLK_SEL;

  // Use fast GPIO6, GPIO7, GPIO8, GPIO9
  IOMUXC_GPR_GPR26 = 0xFFFFFFFF;
  IOMUXC_GPR_GPR27 = 0xFFFFFFFF;
  IOMUXC_GPR_GPR28 = 0xFFFFFFFF;
  IOMUXC_GPR_GPR29 = 0xFFFFFFFF;

  configure_cache();
  
  set_arm_clock(F_CPU);

  asm volatile("nop\n nop\n nop\n nop": : :"memory"); // why oh why?

  // Undo PIT timer usage by ROM startup
  CCM_CCGR1 |= CCM_CCGR1_PIT(CCM_CCGR_ON);
  PIT_MCR = 0;
  PIT_TCTRL0 = 0;
  PIT_TCTRL1 = 0;
  PIT_TCTRL2 = 0;
  PIT_TCTRL3 = 0;

  // initialize RTC
  if (!(SNVS_LPCR & SNVS_LPCR_SRTC_ENV)) {
    // if SRTC isn't running, start it with default Jan 1, 2019
    SNVS_LPSRTCLR = 1546300800u << 15;
    SNVS_LPSRTCMR = 1546300800u >> 17;
    SNVS_LPCR |= SNVS_LPCR_SRTC_ENV;
  }
  SNVS_HPCR |= SNVS_HPCR_RTC_EN | SNVS_HPCR_HP_TS;

  tempmon_init();

  main();
	
  for (;;) ;
}

// concise defines for SCB_MPU_RASR and SCB_MPU_RBAR, ARM DDI0403E, pg 696
#define NOEXEC		SCB_MPU_RASR_XN
#define READONLY	SCB_MPU_RASR_AP(7)
#define READWRITE	SCB_MPU_RASR_AP(3)
#define NOACCESS	SCB_MPU_RASR_AP(0)
#define MEM_CACHE_WT	SCB_MPU_RASR_TEX(0) | SCB_MPU_RASR_C
#define MEM_CACHE_WB	SCB_MPU_RASR_TEX(0) | SCB_MPU_RASR_C | SCB_MPU_RASR_B
#define MEM_CACHE_WBWA	SCB_MPU_RASR_TEX(1) | SCB_MPU_RASR_C | SCB_MPU_RASR_B
#define MEM_NOCACHE	SCB_MPU_RASR_TEX(1)
#define DEV_NOCACHE	SCB_MPU_RASR_TEX(2)
#define SIZE_32B	(SCB_MPU_RASR_SIZE(4) | SCB_MPU_RASR_ENABLE)
#define SIZE_64B	(SCB_MPU_RASR_SIZE(5) | SCB_MPU_RASR_ENABLE)
#define SIZE_128B	(SCB_MPU_RASR_SIZE(6) | SCB_MPU_RASR_ENABLE)
#define SIZE_256B	(SCB_MPU_RASR_SIZE(7) | SCB_MPU_RASR_ENABLE)
#define SIZE_512B	(SCB_MPU_RASR_SIZE(8) | SCB_MPU_RASR_ENABLE)
#define SIZE_1K		(SCB_MPU_RASR_SIZE(9) | SCB_MPU_RASR_ENABLE)
#define SIZE_2K		(SCB_MPU_RASR_SIZE(10) | SCB_MPU_RASR_ENABLE)
#define SIZE_4K		(SCB_MPU_RASR_SIZE(11) | SCB_MPU_RASR_ENABLE)
#define SIZE_8K		(SCB_MPU_RASR_SIZE(12) | SCB_MPU_RASR_ENABLE)
#define SIZE_16K	(SCB_MPU_RASR_SIZE(13) | SCB_MPU_RASR_ENABLE)
#define SIZE_32K	(SCB_MPU_RASR_SIZE(14) | SCB_MPU_RASR_ENABLE)
#define SIZE_64K	(SCB_MPU_RASR_SIZE(15) | SCB_MPU_RASR_ENABLE)
#define SIZE_128K	(SCB_MPU_RASR_SIZE(16) | SCB_MPU_RASR_ENABLE)
#define SIZE_256K	(SCB_MPU_RASR_SIZE(17) | SCB_MPU_RASR_ENABLE)
#define SIZE_512K	(SCB_MPU_RASR_SIZE(18) | SCB_MPU_RASR_ENABLE)
#define SIZE_1M		(SCB_MPU_RASR_SIZE(19) | SCB_MPU_RASR_ENABLE)
#define SIZE_2M		(SCB_MPU_RASR_SIZE(20) | SCB_MPU_RASR_ENABLE)
#define SIZE_4M		(SCB_MPU_RASR_SIZE(21) | SCB_MPU_RASR_ENABLE)
#define SIZE_8M		(SCB_MPU_RASR_SIZE(22) | SCB_MPU_RASR_ENABLE)
#define SIZE_16M	(SCB_MPU_RASR_SIZE(23) | SCB_MPU_RASR_ENABLE)
#define SIZE_32M	(SCB_MPU_RASR_SIZE(24) | SCB_MPU_RASR_ENABLE)
#define SIZE_64M	(SCB_MPU_RASR_SIZE(25) | SCB_MPU_RASR_ENABLE)
#define SIZE_128M	(SCB_MPU_RASR_SIZE(26) | SCB_MPU_RASR_ENABLE)
#define SIZE_256M	(SCB_MPU_RASR_SIZE(27) | SCB_MPU_RASR_ENABLE)
#define SIZE_512M	(SCB_MPU_RASR_SIZE(28) | SCB_MPU_RASR_ENABLE)
#define SIZE_1G		(SCB_MPU_RASR_SIZE(29) | SCB_MPU_RASR_ENABLE)
#define SIZE_2G		(SCB_MPU_RASR_SIZE(30) | SCB_MPU_RASR_ENABLE)
#define SIZE_4G		(SCB_MPU_RASR_SIZE(31) | SCB_MPU_RASR_ENABLE)
#define REGION(n)	(SCB_MPU_RBAR_REGION(n) | SCB_MPU_RBAR_VALID)

__attribute__((section(".flashmem")))  
void configure_cache(void)
{
  // TODO: check if caches already active - skip?

  SCB_MPU_CTRL = 0; // turn off MPU

  uint32_t i = 0;
  SCB_MPU_RBAR = 0x00000000 | REGION(i++); //https://developer.arm.com/docs/146793866/10/why-does-the-cortex-m7-initiate-axim-read-accesses-to-memory-addresses-that-do-not-fall-under-a-defined-mpu-region
  SCB_MPU_RASR = SCB_MPU_RASR_TEX(0) | NOACCESS | NOEXEC | SIZE_4G;
	
  SCB_MPU_RBAR = 0x00000000 | REGION(i++); // ITCM
  SCB_MPU_RASR = MEM_NOCACHE | READWRITE | SIZE_512K;

  // TODO: trap regions should be created last, because the hardware gives
  //  priority to the higher number ones.
  SCB_MPU_RBAR = 0x00000000 | REGION(i++); // trap NULL pointer deref
  SCB_MPU_RASR =  DEV_NOCACHE | NOACCESS | SIZE_32B;

  SCB_MPU_RBAR = 0x00200000 | REGION(i++); // Boot ROM
  SCB_MPU_RASR = MEM_CACHE_WT | READONLY | SIZE_128K;

  SCB_MPU_RBAR = 0x20000000 | REGION(i++); // DTCM
  SCB_MPU_RASR = MEM_NOCACHE | READWRITE | NOEXEC | SIZE_512K;
	
  SCB_MPU_RBAR = ((uint32_t)&_ebss) | REGION(i++); // trap stack overflow
  SCB_MPU_RASR = SCB_MPU_RASR_TEX(0) | NOACCESS | NOEXEC | SIZE_32B;

  SCB_MPU_RBAR = 0x20200000 | REGION(i++); // RAM (AXI bus)
  SCB_MPU_RASR = MEM_CACHE_WBWA | READWRITE | NOEXEC | SIZE_1M;

  SCB_MPU_RBAR = 0x40000000 | REGION(i++); // Peripherals
  SCB_MPU_RASR = DEV_NOCACHE | READWRITE | NOEXEC | SIZE_64M;

  SCB_MPU_RBAR = 0x60000000 | REGION(i++); // QSPI Flash
  SCB_MPU_RASR = MEM_CACHE_WBWA | READONLY | SIZE_16M;

  SCB_MPU_RBAR = 0x70000000 | REGION(i++); // FlexSPI2
  SCB_MPU_RASR = MEM_CACHE_WBWA | READONLY | NOEXEC | SIZE_256M;

  SCB_MPU_RBAR = 0x70000000 | REGION(i++); // FlexSPI2
  SCB_MPU_RASR = MEM_CACHE_WBWA | READWRITE | NOEXEC | SIZE_16M;

  // TODO: protect access to power supply config

  SCB_MPU_CTRL = SCB_MPU_CTRL_ENABLE;

  // cache enable, ARM DDI0403E, pg 628
  asm("dsb");
  asm("isb");
  SCB_CACHE_ICIALLU = 0;

  asm("dsb");
  asm("isb");
  SCB_CCR |= (SCB_CCR_IC | SCB_CCR_DC);
}

// Configure PLLs and Phase Fractional Dividers
__attribute__((section(".flashmem")))    
void reset_PFD()
{	
  //Reset PLL2 PFDs, set default frequencies:
  CCM_ANALOG_PFD_528_SET = (1 << 31) | (1 << 23) | (1 << 15) | (1 << 7);
  CCM_ANALOG_PFD_528 = 0x2018101B; // PFD0:352, PFD1:594, PFD2:396, PFD3:297 MHz 	
  //PLL3:
  CCM_ANALOG_PFD_480_SET = (1 << 31) | (1 << 23) | (1 << 15) | (1 << 7);	
  CCM_ANALOG_PFD_480 = 0x13110D0C; // PFD0:720, PFD1:664, PFD2:508, PFD3:454 MHz
}

 // Stack frame
 //  xPSR
 //  ReturnAddress
 //  LR (R14) - typically FFFFFFF9 for IRQ or Exception
 //  R12
 //  R3
 //  R2
 //  R1
 //  R0
 // Code from :: https://community.nxp.com/thread/389002
__attribute__((naked))
void unused_interrupt_vector(void)
{
   __asm( ".syntax unified\n"
	  "MOVS R0, #4 \n"
	  "MOV R1, LR \n"
	  "TST R0, R1 \n"
	  "BEQ _MSP \n"
	  "MRS R0, PSP \n"
	  "B HardFault_HandlerC \n"
	  "_MSP: \n"
	  "MRS R0, MSP \n"
	  "B HardFault_HandlerC \n"
	  ".syntax divided\n") ;
}

__attribute__((weak))
void HardFault_HandlerC(unsigned int *hardfault_args)
{
   volatile unsigned int nn ;

   IOMUXC_SW_MUX_CTL_PAD_GPIO_B0_03 = 5; // pin 13
   IOMUXC_SW_PAD_CTL_PAD_GPIO_B0_03 = IOMUXC_PAD_DSE(7);
   GPIO2_GDIR |= (1 << 3);
   GPIO2_DR_SET = (1 << 3);
   GPIO2_DR_CLEAR = (1 << 3); //digitalWrite(13, LOW);

   if ( F_CPU_ACTUAL >= 600000000 )
     set_arm_clock(300000000);

   while (1)
     {
       GPIO2_DR_SET = (1 << 3); //digitalWrite(13, HIGH);
       // digitalWrite(13, HIGH);
       for (nn = 0; nn < 2000000/2; nn++) ;
       GPIO2_DR_CLEAR = (1 << 3); //digitalWrite(13, LOW);
       // digitalWrite(13, LOW);
       for (nn = 0; nn < 18000000/2; nn++) ;
     }
 }

__attribute__((section(".startup"), optimize("no-tree-loop-distribute-patterns")))
static void memory_copy(uint32_t *dest, const uint32_t *src, uint32_t *dest_end)
{
   if (dest == src) return;
   while (dest < dest_end) {
     *dest++ = *src++;
   }
}

__attribute__((section(".startup"), optimize("no-tree-loop-distribute-patterns")))
static void memory_clear(uint32_t *dest, uint32_t *dest_end)
{
  while (dest < dest_end) {
    *dest++ = 0;
  }
}

// syscall functions need to be in the same C file as the entry point "ResetVector"
// otherwise the linker will discard them in some cases.

#include <errno.h>

// from the linker script
extern unsigned long _heap_start;
extern unsigned long _heap_end;

char *__brkval = (char *)&_heap_start;

void * _sbrk(int incr)
{
  char *prev = __brkval;
  if (incr != 0) {
    if (prev + incr > (char *)&_heap_end) {
      errno = ENOMEM;
      return (void *)-1;
    }
    __brkval = prev + incr;
  }
  return prev;
}

__attribute__((weak))
int _read(int file, char *ptr, int len)
{
  return 0;
}

__attribute__((weak))
int _close(int fd)
{
  return -1;
}

#include <sys/stat.h>

__attribute__((weak))
int _fstat(int fd, struct stat *st)
{
  st->st_mode = S_IFCHR;
  return 0;
}

__attribute__((weak))
int _isatty(int fd)
{
  return 1;
}

__attribute__((weak))
int _lseek(int fd, long long offset, int whence)
{
  return -1;
}

__attribute__((weak))
void _exit(int status)
{
  while (1) asm ("WFI");
}

__attribute__((weak))
void __cxa_pure_virtual()
{
  while (1) asm ("WFI");
}

__attribute__((weak))
int __cxa_guard_acquire (char *g)
{
  return !(*g);
}

__attribute__((weak))
void __cxa_guard_release(char *g)
{
  *g = 1;
}

__attribute__((weak))
void abort(void)
{
  while (1) asm ("WFI");
}

/***********************************************************************

  Temperature Monitoring

 ***********************************************************************/

static uint16_t frequency = 0x03U;
static uint32_t highAlarmTemp   = 85U;
static uint32_t lowAlarmTemp    = 25U;
static uint32_t panicAlarmTemp  = 90U;

static uint32_t s_hotTemp, s_hotCount, s_roomC_hotC;
static float s_hot_ROOM;

void Panic_Temp_isr(void) {
  __disable_irq();
  IOMUXC_GPR_GPR16 = 0x00000007;
  SNVS_LPCR |= SNVS_LPCR_TOP; //Switch off now
  asm volatile ("dsb":::"memory");
  while (1) asm ("wfi");
}

__attribute__((section(".flashmem")))
void tempmon_init(void)
{
  // Notes:
  //    TEMPMON_TEMPSENSE0 &= ~0x2U;  Stops temp monitoring
  //    TEMPMON_TEMPSENSE0 |= 0x1U;   Powers down temp monitoring 
  uint32_t calibrationData;
  uint32_t roomCount;
  uint32_t tempCodeVal;
      
  //first power on the temperature sensor - no register change
  TEMPMON_TEMPSENSE0 &= ~0x1U;
    
  //set monitoring frequency - no register change
  TEMPMON_TEMPSENSE1 = (((uint32_t)(((uint32_t)(frequency)) << 0U)) & 0xFFFFU);
  
  //read calibration data - this works
  calibrationData = HW_OCOTP_ANA1;
  s_hotTemp = (uint32_t)(calibrationData & 0xFFU) >> 0x00U;
  s_hotCount = (uint32_t)(calibrationData & 0xFFF00U) >> 0X08U;
  roomCount = (uint32_t)(calibrationData & 0xFFF00000U) >> 0x14U;
  s_hot_ROOM = s_hotTemp - 25.0f;
  s_roomC_hotC = roomCount - s_hotCount;

  // Set alarm temperatures
    
  //Set High Alarm Temp
  tempCodeVal = (uint32_t)(s_hotCount + (s_hotTemp - highAlarmTemp) * s_roomC_hotC / s_hot_ROOM);
  TEMPMON_TEMPSENSE0 |= (((uint32_t)(((uint32_t)(tempCodeVal)) << 20U)) & 0xFFF00000U);
  
  //Set Panic Alarm Temp
  tempCodeVal = (uint32_t)(s_hotCount + (s_hotTemp - panicAlarmTemp) * s_roomC_hotC / s_hot_ROOM);
  TEMPMON_TEMPSENSE2 |= (((uint32_t)(((uint32_t)(tempCodeVal)) << 16U)) & 0xFFF0000U);
  
  // Set Low Temp Alarm Temp
  tempCodeVal = (uint32_t)(s_hotCount + (s_hotTemp - lowAlarmTemp) * s_roomC_hotC / s_hot_ROOM);
  TEMPMON_TEMPSENSE2 |= (((uint32_t)(((uint32_t)(tempCodeVal)) << 0U)) & 0xFFFU);
  
  //Start temp monitoring
  TEMPMON_TEMPSENSE0 |= 0x2U;   //starts temp monitoring

  //PANIC shutdown:
  NVIC_SET_PRIORITY(IRQ_TEMPERATURE_PANIC, 0);
  attachInterruptVector(IRQ_TEMPERATURE_PANIC, &Panic_Temp_isr);
  NVIC_ENABLE_IRQ(IRQ_TEMPERATURE_PANIC);
}


float tempmonGetTemp(void)
{
  uint32_t nmeas;
  float tmeas;

  while (!(TEMPMON_TEMPSENSE0 & 0x4U)) ;

  /* ready to read temperature code value */
  nmeas = (TEMPMON_TEMPSENSE0 & 0xFFF00U) >> 8U;
  /* Calculate temperature */
  tmeas = s_hotTemp - (float)((nmeas - s_hotCount) * s_hot_ROOM / s_roomC_hotC);

  return tmeas;
}

void tempmon_Start()
{
  TEMPMON_TEMPSENSE0 |= 0x2U;
}

void tempmon_Stop()
{
  TEMPMON_TEMPSENSE0 &= ~0x2U;
}

void tempmon_PwrDwn()
{
  TEMPMON_TEMPSENSE0 |= 0x1U;
}

/***********************************************************************

  Processor Clock Speed

 ***********************************************************************/

// A brief explanation of F_CPU_ACTUAL vs F_CPU
//  https://forum.pjrc.com/threads/57236?p=212642&viewfull=1#post212642
volatile uint32_t F_CPU_ACTUAL = 396000000;
volatile uint32_t F_BUS_ACTUAL = 132000000;

// Define these to increase the voltage when attempting overclocking
// The frequency step is how quickly to increase voltage per frequency
// The datasheet says 1600 is the absolute maximum voltage.  The hardware
// can actually create up to 1575.  But 1300 is the recommended limit.
//  (earlier versions of the datasheet said 1300 was the absolute max)
#define OVERCLOCK_STEPSIZE  28000000
#define OVERCLOCK_MAX_VOLT  1575

// stuff needing wait handshake:
//  CCM_CACRR  ARM_PODF
//  CCM_CBCDR  PERIPH_CLK_SEL
//  CCM_CBCMR  PERIPH2_CLK_SEL
//  CCM_CBCDR  AHB_PODF
//  CCM_CBCDR  SEMC_PODF

uint32_t set_arm_clock(uint32_t frequency)
{
  uint32_t cbcdr = CCM_CBCDR; // pg 1021
  uint32_t cbcmr = CCM_CBCMR; // pg 1023
  uint32_t dcdc = DCDC_REG3;

  // compute required voltage
  uint32_t voltage = 1150; // default = 1.15V
  if (frequency > 528000000) {
    voltage = 1250; // 1.25V
    if (frequency > 600000000) {
      voltage += ((frequency - 600000000) / OVERCLOCK_STEPSIZE) * 25;
      if (voltage > OVERCLOCK_MAX_VOLT) voltage = OVERCLOCK_MAX_VOLT;
    }
  } else if (frequency <= 24000000) {
    voltage = 950; // 0.95
  }

  // if voltage needs to increase, do it before switch clock speed
  CCM_CCGR6 |= CCM_CCGR6_DCDC(CCM_CCGR_ON);
  if ((dcdc & DCDC_REG3_TRG_MASK) < DCDC_REG3_TRG((voltage - 800) / 25)) {
    dcdc &= ~DCDC_REG3_TRG_MASK;
    dcdc |= DCDC_REG3_TRG((voltage - 800) / 25);
    DCDC_REG3 = dcdc;
    while (!(DCDC_REG0 & DCDC_REG0_STS_DC_OK)) ; // wait voltage settling
  }

  if (!(cbcdr & CCM_CBCDR_PERIPH_CLK_SEL)) {
    const uint32_t need1s = CCM_ANALOG_PLL_USB1_ENABLE | CCM_ANALOG_PLL_USB1_POWER |
      CCM_ANALOG_PLL_USB1_LOCK | CCM_ANALOG_PLL_USB1_EN_USB_CLKS;
    uint32_t sel, div;
    if ((CCM_ANALOG_PLL_USB1 & need1s) == need1s) {
      sel = 0;
      div = 3; // divide down to 120 MHz, so IPG is ok even if IPG_PODF=0
    } else {
      sel = 1;
      div = 0;
    }
    if ((cbcdr & CCM_CBCDR_PERIPH_CLK2_PODF_MASK) != CCM_CBCDR_PERIPH_CLK2_PODF(div)) {
      // PERIPH_CLK2 divider needs to be changed
      cbcdr &= ~CCM_CBCDR_PERIPH_CLK2_PODF_MASK;
      cbcdr |= CCM_CBCDR_PERIPH_CLK2_PODF(div);
      CCM_CBCDR = cbcdr;
    }
    if ((cbcmr & CCM_CBCMR_PERIPH_CLK2_SEL_MASK) != CCM_CBCMR_PERIPH_CLK2_SEL(sel)) {
      // PERIPH_CLK2 source select needs to be changed
      cbcmr &= ~CCM_CBCMR_PERIPH_CLK2_SEL_MASK;
      cbcmr |= CCM_CBCMR_PERIPH_CLK2_SEL(sel);
      CCM_CBCMR = cbcmr;
      while (CCM_CDHIPR & CCM_CDHIPR_PERIPH2_CLK_SEL_BUSY) ; // wait
    }
    // switch over to PERIPH_CLK2
    cbcdr |= CCM_CBCDR_PERIPH_CLK_SEL;
    CCM_CBCDR = cbcdr;
    while (CCM_CDHIPR & CCM_CDHIPR_PERIPH_CLK_SEL_BUSY) ; // wait
  }

  // TODO: check if PLL2 running, can 352, 396 or 528 can work? (no need for ARM PLL)

  // DIV_SELECT: 54-108 = official range 648 to 1296 in 12 MHz steps
  uint32_t div_arm = 1;
  uint32_t div_ahb = 1;
  while (frequency * div_arm * div_ahb < 648000000) {
    if (div_arm < 8) {
      div_arm = div_arm + 1;
    } else {
      if (div_ahb < 5) {
	div_ahb = div_ahb + 1;
	div_arm = 1;
      } else {
	break;
      }
    }
  }
  uint32_t mult = (frequency * div_arm * div_ahb + 6000000) / 12000000;
  if (mult > 108) mult = 108;
  if (mult < 54) mult = 54;
  frequency = mult * 12000000 / div_arm / div_ahb;

  const uint32_t arm_pll_mask = CCM_ANALOG_PLL_ARM_LOCK | CCM_ANALOG_PLL_ARM_BYPASS |
    CCM_ANALOG_PLL_ARM_ENABLE | CCM_ANALOG_PLL_ARM_POWERDOWN |
    CCM_ANALOG_PLL_ARM_DIV_SELECT_MASK;
  if ((CCM_ANALOG_PLL_ARM & arm_pll_mask) != (CCM_ANALOG_PLL_ARM_LOCK
					      | CCM_ANALOG_PLL_ARM_ENABLE | CCM_ANALOG_PLL_ARM_DIV_SELECT(mult))) {
    CCM_ANALOG_PLL_ARM = CCM_ANALOG_PLL_ARM_POWERDOWN;
    // TODO: delay needed?
    CCM_ANALOG_PLL_ARM = CCM_ANALOG_PLL_ARM_ENABLE
      | CCM_ANALOG_PLL_ARM_DIV_SELECT(mult);
    while (!(CCM_ANALOG_PLL_ARM & CCM_ANALOG_PLL_ARM_LOCK)) ; // wait for lock
  }

  if ((CCM_CACRR & CCM_CACRR_ARM_PODF_MASK) != (div_arm - 1)) {
    CCM_CACRR = CCM_CACRR_ARM_PODF(div_arm - 1);
    while (CCM_CDHIPR & CCM_CDHIPR_ARM_PODF_BUSY) ; // wait
  }

  if ((cbcdr & CCM_CBCDR_AHB_PODF_MASK) != CCM_CBCDR_AHB_PODF(div_ahb - 1)) {
    cbcdr &= ~CCM_CBCDR_AHB_PODF_MASK;
    cbcdr |= CCM_CBCDR_AHB_PODF(div_ahb - 1);
    CCM_CBCDR = cbcdr;
    while (CCM_CDHIPR & CCM_CDHIPR_AHB_PODF_BUSY); // wait
  }

  uint32_t div_ipg = (frequency + 149999999) / 150000000;
  if (div_ipg > 4) div_ipg = 4;
  if ((cbcdr & CCM_CBCDR_IPG_PODF_MASK) != (CCM_CBCDR_IPG_PODF(div_ipg - 1))) {
    cbcdr &= ~CCM_CBCDR_IPG_PODF_MASK;
    cbcdr |= CCM_CBCDR_IPG_PODF(div_ipg - 1);
    // TODO: how to safely change IPG_PODF ??
    CCM_CBCDR = cbcdr;
  }

  //cbcdr &= ~CCM_CBCDR_PERIPH_CLK_SEL;
  //CCM_CBCDR = cbcdr;  // why does this not work at 24 MHz?
  CCM_CBCDR &= ~CCM_CBCDR_PERIPH_CLK_SEL;
  while (CCM_CDHIPR & CCM_CDHIPR_PERIPH_CLK_SEL_BUSY) ; // wait

  F_CPU_ACTUAL = frequency;
  F_BUS_ACTUAL = frequency / div_ipg;

  // if voltage needs to decrease, do it after switch clock speed
  if ((dcdc & DCDC_REG3_TRG_MASK) > DCDC_REG3_TRG((voltage - 800) / 25)) {
    dcdc &= ~DCDC_REG3_TRG_MASK;
    dcdc |= DCDC_REG3_TRG((voltage - 800) / 25);
    DCDC_REG3 = dcdc;
    while (!(DCDC_REG0 & DCDC_REG0_STS_DC_OK)) ; // wait voltage settling
  }

  return frequency;
}

/***********************************************************************

  Boot vectors

 ***********************************************************************/

__attribute__ ((section(".vectors"), used))
const uint32_t vector_table[2] = {
  0x20010000, // 64K DTCM for boot, ResetHandler configures stack after ITCM/DTCM setup
  (uint32_t)&ResetHandler
};

__attribute__ ((section(".bootdata"), used))
const uint32_t BootData[3] = {
  0x60000000,
  (uint32_t)&_flashimagelen,
  0
};

__attribute__ ((section(".ivt"), used))
const uint32_t ImageVectorTable[8] = {
  0x402000D1,		// header
  (uint32_t)vector_table, // docs are wrong, needs to be vec table, not start addr
  0,			// reserved
  0,			// dcd
  (uint32_t)BootData,	// abs address of boot data
  (uint32_t)ImageVectorTable, // self
  0,			// command sequence file
  0			// reserved
};

__attribute__ ((section(".flashconfig"), used))
uint32_t FlexSPI_NOR_Config[128] = {
  // 448 byte common FlexSPI configuration block, 8.6.3.1 page 223 (RT1060 rev 0)
  // MCU_Flashloader_Reference_Manual.pdf, 8.2.1, Table 8-2, page 72-75
  0x42464346,		// Tag				0x00
  0x56010000,		// Version
  0,			// reserved
  0x00020101,		// columnAdressWidth,dataSetupTime,dataHoldTime,readSampleClkSrc

  0x00000000,		// waitTimeCfgCommands,-,deviceModeCfgEnable
  0,			// deviceModeSeq
  0, 			// deviceModeArg
  0x00000000,		// -,-,-,configCmdEnable

  0,			// configCmdSeqs		0x20
  0,
  0,
  0,

  0,			// cfgCmdArgs			0x30
  0,
  0,
  0,

  0x00000000,		// controllerMiscOption		0x40
  0x00030401,		// lutCustomSeqEnable,serialClkFreq,sflashPadType,deviceType
  0,			// reserved
  0,			// reserved

  0x00200000,		// sflashA1Size			0x50
  0,			// sflashA2Size
  0,			// sflashB1Size
  0,			// sflashB2Size

  0,			// csPadSettingOverride		0x60
  0,			// sclkPadSettingOverride
  0,			// dataPadSettingOverride
  0,			// dqsPadSettingOverride

  0,			// timeoutInMs			0x70
  0,			// commandInterval
  0,			// dataValidTime
  0x00000000,		// busyBitPolarity,busyOffset

  0x0A1804EB,		// lookupTable[0]		0x80
  0x26043206,		// lookupTable[1]
  0,			// lookupTable[2]
  0,			// lookupTable[3]

  0x24040405,		// lookupTable[4]		0x90
  0,			// lookupTable[5]
  0,			// lookupTable[6]
  0,			// lookupTable[7]

  0,			// lookupTable[8]		0xA0
  0,			// lookupTable[9]
  0,			// lookupTable[10]
  0,			// lookupTable[11]

  0x00000406,		// lookupTable[12]		0xB0
  0,			// lookupTable[13]
  0,			// lookupTable[14]
  0,			// lookupTable[15]

  0,			// lookupTable[16]		0xC0
  0,			// lookupTable[17]
  0,			// lookupTable[18]
  0,			// lookupTable[19]

  0x08180420,		// lookupTable[20]		0xD0
  0,			// lookupTable[21]
  0,			// lookupTable[22]
  0,			// lookupTable[23]

  0,			// lookupTable[24]		0xE0
  0,			// lookupTable[25]
  0,			// lookupTable[26]
  0,			// lookupTable[27]

  0,			// lookupTable[28]		0xF0
  0,			// lookupTable[29]
  0,			// lookupTable[30]
  0,			// lookupTable[31]

  0x081804D8,		// lookupTable[32]		0x100
  0,			// lookupTable[33]
  0,			// lookupTable[34]
  0,			// lookupTable[35]

  0x08180402,		// lookupTable[36]		0x110
  0x00002004,		// lookupTable[37]
  0,			// lookupTable[38]
  0,			// lookupTable[39]

  0,			// lookupTable[40]		0x120
  0,			// lookupTable[41]
  0,			// lookupTable[42]
  0,			// lookupTable[43]

  0x00000460,		// lookupTable[44]		0x130
  0,			// lookupTable[45]
  0,			// lookupTable[46]
  0,			// lookupTable[47]

  0,			// lookupTable[48]		0x140
  0,			// lookupTable[49]
  0,			// lookupTable[50]
  0,			// lookupTable[51]

  0,			// lookupTable[52]		0x150
  0,			// lookupTable[53]
  0,			// lookupTable[54]
  0,			// lookupTable[55]

  0,			// lookupTable[56]		0x160
  0,			// lookupTable[57]
  0,			// lookupTable[58]
  0,			// lookupTable[59]

  0,			// lookupTable[60]		0x170
  0,			// lookupTable[61]
  0,			// lookupTable[62]
  0,			// lookupTable[63]

  0,			// LUT 0: Read			0x180
  0,			// LUT 1: ReadStatus
  0,			// LUT 3: WriteEnable
  0,			// LUT 5: EraseSector

  0,			// LUT 9: PageProgram		0x190
  0,			// LUT 11: ChipErase
  0,			// LUT 15: Dummy
  0,			// LUT unused?

  0,			// LUT unused?			0x1A0
  0,			// LUT unused?
  0,			// LUT unused?
  0,			// LUT unused?

  0,			// reserved			0x1B0
  0,			// reserved
  0,			// reserved
  0,			// reserved

  // 64 byte Serial NOR configuration block, 8.6.3.2, page 346

  256,			// pageSize			0x1C0
  4096,			// sectorSize
  1,			// ipCmdSerialClkFreq
  0,			// reserved

  0x00010000,		// block size			0x1D0
  0,			// reserved
  0,			// reserved
  0,			// reserved

  0,			// reserved			0x1E0
  0,			// reserved
  0,			// reserved
  0,			// reserved

  0,			// reserved			0x1F0
  0,			// reserved
  0,			// reserved
  0			// reserved
};
