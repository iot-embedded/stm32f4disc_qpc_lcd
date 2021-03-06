/*****************************************************************************
* Product: "DPP" example on STM32F4-Discovery board, preemptive QK kernel
* Last Updated for Version: 6.5.0
* Date of the Last Update:  2019-05-09
*
*                    Q u a n t u m  L e a P s
*                    ------------------------
*                    Modern Embedded Software
*
* Copyright (C) 2005-2019 Quantum Leaps, LLC. All rights reserved.
*
* This program is open source software: you can redistribute it and/or
* modify it under the terms of the GNU General Public License as published
* by the Free Software Foundation, either version 3 of the License, or
* (at your option) any later version.
*
* Alternatively, this program may be distributed and modified under the
* terms of Quantum Leaps commercial licenses, which expressly supersede
* the GNU General Public License and are specifically designed for
* licensees interested in retaining the proprietary status of their code.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program. If not, see <http://www.gnu.org/licenses/>.
*
* Contact information:
* https://www.state-machine.com
* mailto:info@state-machine.com
*****************************************************************************/
#include <stdbool.h>
#include "main.h"
#include "qpc.h"
#include "bsp.h"
#include "lvgl.h"

#include "../state_machine/AppSM.h"
#include "stm32f4xx_hal.h"  /* CMSIS-compliant header file for the MCU used */

#include "../lib/display/display.h"
/* add other drivers if necessary... */

Q_DEFINE_THIS_FILE

/* ISRs defined in this BSP ------------------------------------------------*/
void SysTick_Handler(void);
void USART2_IRQHandler(void);
/* Function with file scope */
static void disp_flush(struct _disp_drv_t * disp_drv, const lv_area_t * area, lv_color_t * color_p);
static disp_func_t *disp_func = NULL;
/* Local-scope defines -----------------------------------------------------*/

/* Buttons debounce structure */
typedef struct btn_deb_t_
{
	GPIO_TypeDef *GPIOx;
	uint32_t PinMask;
	uint8_t depressed;
	uint8_t previous;
} btn_deb_t;

static btn_deb_t btnsh[BTNSH_CNT]=
{
	{BTNSH_Up_GPIO_Port, BTNSH_Up_Pin, 0, 0      },
	{BTNSH_Down_GPIO_Port, BTNSH_Down_Pin, 0, 0  },
	{BTNSH_Left_GPIO_Port, BTNSH_Left_Pin, 0, 0  },
	{BTNSH_Right_GPIO_Port, BTNSH_Right_Pin, 0, 0},
};
/*
 * Display buffer
 */
static lv_disp_buf_t disp_buf;
static lv_color_t buf[LV_HOR_RES_MAX * 10];                     /*Declare a buffer for 10 lines*/
/* Port structure to keep pins for the display control */
static const disp_ctrl_t disp_ctrl = {
	.data =
	{
		{.gpio=GPIOD, .pin=0},
		{.gpio=GPIOD, .pin=1},
		{.gpio=GPIOD, .pin=2},
		{.gpio=GPIOD, .pin=3},
		{.gpio=GPIOE, .pin=4},
		{.gpio=GPIOE, .pin=5},
		{.gpio=GPIOE, .pin=6},
		{.gpio=GPIOE, .pin=7},
		{.gpio=GPIOE, .pin=8},
		{.gpio=GPIOE, .pin=9},
		{.gpio=GPIOE, .pin=10},
		{.gpio=GPIOE, .pin=11},
		{.gpio=GPIOE, .pin=12},
		{.gpio=GPIOE, .pin=13},
		{.gpio=GPIOE, .pin=14},
		{.gpio=GPIOE, .pin=15},
	},
	.wr = {.gpio=GPIOC, .pin=2},
	.rd = {.gpio=GPIOC, .pin=1},
	.rs = {.gpio=GPIOC, .pin=0},
	.cs = {.gpio=GPIOD, .pin=10},
	.reset = {.gpio=GPIOC, .pin=5},
	.led = {.gpio=GPIOA, .pin=15},
};

#ifdef Q_SPY
    static QSTimeCtr QS_tickTime_;
    static QSTimeCtr QS_tickPeriod_;

    /* event-source identifiers used for tracing */
    static uint8_t const l_SysTick;

    enum AppRecords { /* application-specific trace records */
    	Btnsh_on = QS_USER,
		Btnsh_off,
		COMMAND_STAT,
    };

#endif

/* ISRs used in this project ===============================================*/
void SysTick_Handler(void) {

    QK_ISR_ENTRY();   /* inform QK about entering an ISR */

#ifdef Q_SPY
    {
    	uint32_t tmp = SysTick->CTRL; /* clear SysTick_CTRL_COUNTFLAG */
        QS_tickTime_ += QS_tickPeriod_; /* account for the clock rollover */
    }
#endif

    QF_TICK_X(0U, &l_SysTick);

    /* Perform the debouncing of buttons. The algorithm for debouncing
    * adapted from the book "Embedded Systems Dictionary" by Jack Ganssle
    * and Michael Barr, page 71.
    */

    for (uint8_t btn_idx=0; btn_idx<BTNSH_CNT;btn_idx++)
    {
    	/*
    	 * Button has pull up:
    	 * Not pressed => H Level (1)
    	 * Pressed	   => L Level (0)
    	 */
        uint8_t current = LL_GPIO_IsInputPinSet(btnsh[btn_idx].GPIOx, btnsh[btn_idx].PinMask)?1:0; /* read Button */;
        uint8_t tmp = btnsh[btn_idx].depressed; /* save the debounced depressed buttons */
		btnsh[btn_idx].depressed |= (btnsh[btn_idx].previous & current); /* set depressed */
		btnsh[btn_idx].depressed &= (btnsh[btn_idx].previous | current); /* clear released */
		btnsh[btn_idx].previous   = current; /* update the history */
		tmp ^= btnsh[btn_idx].depressed;     /* changed debounced depressed */
		if (tmp  == 0x01) {  /* debounced B1 state changed? */
			if (btnsh[btn_idx].depressed == 0x01 ) { /* is Button released? */
				key_event_t *evt = Q_NEW(key_event_t, BTNSH_OFF);
				if (evt != NULL)
				{
					evt->btn = btn_idx;
					QF_PUBLISH((QEvt const * const) evt, (void*)0);
				}
			}
			else
			{ /* the button is pressed */
				key_event_t *evt = Q_NEW(key_event_t, BTNSH_ON);
				if (evt != NULL)
				{
					evt->btn = btn_idx;
					QF_PUBLISH((QEvt const * const) evt, (void*)0);
				}
			}
		}
    }
    lv_tick_inc(1);
    lv_task_handler();
    QK_ISR_EXIT();  /* inform QK about exiting an ISR */
}

/*..........................................................................*/
#ifdef Q_SPY
/*
* ISR for receiving bytes from the QSPY Back-End
* NOTE: This ISR is "QF-unaware" meaning that it does not interact with
* the QF/QK and is not disabled. Such ISRs don't need to call QK_ISR_ENTRY/
* QK_ISR_EXIT and they cannot post or publish events.
*/
void USART2_IRQHandler(void) {
    if (LL_USART_IsActiveFlag_RXNE(USART2)) {
        uint32_t b = LL_USART_ReceiveData8(USART2);
        QS_RX_PUT(b);
    }
}
#else
void USART2_IRQHandler(void) {}
#endif


/* BSP functions ===========================================================*/
void BSP_init(void) {

    /* NOTE: SystemInit() already called from the startup code
    *  but SystemCoreClock needs to be updated
    */
    SystemCoreClockUpdate();

    /* configure the FPU usage by choosing one of the options... */
#if 1
    /* OPTION 1:
    * Use the automatic FPU state preservation and the FPU lazy stacking.
    *
    * NOTE:
    * Use the following setting when FPU is used in more than one task or
    * in any ISRs. This setting is the safest and recommended, but requires
    * extra stack space and CPU cycles.
    */
    FPU->FPCCR |= (1U << FPU_FPCCR_ASPEN_Pos) | (1U << FPU_FPCCR_LSPEN_Pos);
#else
    /* OPTION 2:
    * Do NOT to use the automatic FPU state preservation and
    * do NOT to use the FPU lazy stacking.
    *
    * NOTE:
    * Use the following setting when FPU is used in ONE task only and not
    * in any ISR. This setting is very efficient, but if more than one task
    * (or ISR) start using the FPU, this can lead to corruption of the
    * FPU registers. This option should be used with CAUTION.
    */
    FPU->FPCCR &= ~((1U << FPU_FPCCR_ASPEN_Pos) | (1U << FPU_FPCCR_LSPEN_Pos));
#endif


    if (QS_INIT((void *)0) == 0U) { /* initialize the QS software tracing */
        Q_ERROR();
    }
    QS_OBJ_DICTIONARY(&l_SysTick);
    //QS_USR_DICTIONARY(PHILO_STAT);
    //QS_USR_DICTIONARY(COMMAND_STAT);
    /*
     * Set up display
     */
    disp_func = disp_ctor(SSD1289, &disp_ctrl);
    disp_func->on();
    lv_init();
    lv_disp_buf_init(&disp_buf, buf, NULL, LV_HOR_RES_MAX * 10);    /*Initialize the display buffer*/
    lv_disp_drv_t disp_drv;               /*Descriptor of a display driver*/
    lv_disp_drv_init(&disp_drv);          /*Basic initialization*/
    disp_drv.flush_cb = disp_flush;    /*Set your driver function*/
    disp_drv.buffer = &disp_buf;          /*Assign the buffer to the display*/
    lv_disp_drv_register(&disp_drv);      /*Finally register the driver*/

}

/*..........................................................................*/
/*
 * Display handling functions
 */
/*..........................................................................*/
static void disp_flush(struct _disp_drv_t * disp_drv, const lv_area_t * area, lv_color_t * color_p)
{
    int32_t x, y;
    for(y = area->y1; y <= area->y2; y++) {
        for(x = area->x1; x <= area->x2; x++) {
            //disp_set_pixel(x, y, *color_p);  /* Put a pixel to the display.*/
            color_p++;
        }
    }

    lv_disp_flush_ready(disp_drv);         /* Indicate you are ready with the flushing*/
}

/*..........................................................................*/
void BSP_ledOn(Ledsh_t ledNr) {
	if (ledNr==LEDSH_RED)
	{
		LL_GPIO_SetOutputPin(GPIOC, LDSH_Red_Pin);
	} else if (ledNr==LEDSH_GREEN)
	{
		LL_GPIO_SetOutputPin(GPIOC, LDSH_Green_Pin);
	}
}
/*..........................................................................*/
void BSP_ledOff(Ledsh_t ledNr) {
	if (ledNr==LEDSH_RED)
	{
		LL_GPIO_ResetOutputPin(GPIOC, LDSH_Red_Pin);
	} else if (ledNr==LEDSH_GREEN)
	{
		LL_GPIO_ResetOutputPin(GPIOC, LDSH_Green_Pin);
	}
}
#if 0 //Not used
/*..........................................................................*/
void BSP_displayPhilStat(uint8_t n, char const *stat) {
    (void)n;

    if (stat[0] == 'h') {
        LED_GPIO_PORT->BSRRL = LED3_PIN; /* turn LED on  */
    }
    else {
        LED_GPIO_PORT->BSRRH = LED3_PIN; /* turn LED off */
    }
    if (stat[0] == 'e') {
        LED_GPIO_PORT->BSRRL = LED5_PIN; /* turn LED on  */
    }
    else {
        LED_GPIO_PORT->BSRRH = LED5_PIN; /* turn LED off */
    }

    QS_BEGIN(PHILO_STAT, AO_Philo[n]) /* application-specific record begin */
        QS_U8(1, n);  /* Philosopher number */
        QS_STR(stat); /* Philosopher status */
    QS_END()          /* application-specific record end */
}
/*..........................................................................*/
void BSP_displayPaused(uint8_t paused) {
    if (paused) {
        LED_GPIO_PORT->BSRRL = LED4_PIN; /* turn LED on  */
    }
    else {
        LED_GPIO_PORT->BSRRH = LED4_PIN; /* turn LED on  */
    }
}
#endif
/*..........................................................................*/
void BSP_terminate(int16_t result) {
    (void)result;
}

/* QF callbacks ============================================================*/
void QF_onStartup(void) {
    /* set up the SysTick timer to fire at BSP_TICKS_PER_SEC rate */
    SysTick_Config(SystemCoreClock / BSP_TICKS_PER_SEC);

    /* assign all priority bits for preemption-prio. and none to sub-prio. */
    NVIC_SetPriorityGrouping(0U);

    /* !!!!!!!!!!!!!!!!!!!!!!!!!!!! CAUTION !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
    * Assign a priority to EVERY ISR explicitly, see NOTE00.
    * DO NOT LEAVE THE ISR PRIORITIES AT THE DEFAULT VALUE!
    */
    /* kernel UNAWARE interrupts, see NOTE00 */
    NVIC_SetPriority(USART2_IRQn,    0U);
    /* ... */

    /* kernel AWARE interrupts, see NOTE00 */
    NVIC_SetPriority(SysTick_IRQn,   QF_AWARE_ISR_CMSIS_PRI);
    /* ... */

    /* enable IRQs... */
#ifdef Q_SPY
    NVIC_EnableIRQ(USART2_IRQn); /* USART2 interrupt used for QS-RX */
#endif
}
/*..........................................................................*/
void QF_onCleanup(void) {
}
/*..........................................................................*/
void QK_onIdle(void) {
    QF_INT_DISABLE();
    //BSP_ledOn(LEDSH_RED); /* turn LED on  */
    __NOP(); /* wait a little to actually see the LED glow */
    __NOP();
    __NOP();
    __NOP();
    // BSP_ledOff(LEDSH_RED); /* turn LED off  */
    QF_INT_ENABLE();

#ifdef Q_SPY
    QS_rxParse();  /* parse all the received bytes */

    if (LL_USART_IsActiveFlag_TXE(USART2)) { /* TXE empty? */
        uint16_t b;

        QF_INT_DISABLE();
        b = QS_getByte();
        QF_INT_ENABLE();

        if (b != QS_EOD) {  /* not End-Of-Data? */
        	LL_USART_TransmitData8(USART2, b & 0xFFU);  /* send byte */
        }
    }
#elif defined NDEBUG
    /* Put the CPU and peripherals to the low-power mode.
    * you might need to customize the clock management for your application,
    * see the datasheet for your particular Cortex-M MCU.
    */
    /* !!!CAUTION!!!
    * The WFI instruction stops the CPU clock, which unfortunately disables
    * the JTAG port, so the ST-Link debugger can no longer connect to the
    * board. For that reason, the call to __WFI() has to be used with CAUTION.
    *
    * NOTE: If you find your board "frozen" like this, strap BOOT0 to VDD and
    * reset the board, then connect with ST-Link Utilities and erase the part.
    * The trick with BOOT(0) is that it gets the part to run the System Loader
    * instead of your broken code. When done disconnect BOOT0, and start over.
    */
    //__WFI(); /* Wait-For-Interrupt */
#endif
}

/*..........................................................................*/
void Q_onAssert(char const *module, int loc) {
    /*
    * NOTE: add here your application-specific error handling
    */
    (void)module;
    (void)loc;
    QS_ASSERTION(module, loc, (uint32_t)10000U); /* report assertion to QS */
    NVIC_SystemReset();
}

/* QS callbacks ============================================================*/
#ifdef Q_SPY
static uint8_t qsBuf[2*1024]; /* buffer for QS-TX channel */
static uint8_t qsRxBuf[100];  /* buffer for QS-RX channel */
/*..........................................................................*/
uint8_t QS_onStartup(void const *arg) {

    (void)arg; /* avoid the "unused parameter" compiler warning */
    QS_initBuf(qsBuf, sizeof(qsBuf));
    QS_rxInitBuf(qsRxBuf, sizeof(qsRxBuf));

    LL_USART_EnableIT_RXNE(USART2);

    QS_tickPeriod_ = SystemCoreClock / BSP_TICKS_PER_SEC;
    QS_tickTime_ = QS_tickPeriod_; /* to start the timestamp at zero */

    /* setup the QS filters... */
    QS_FILTER_ON(QS_SM_RECORDS); /* state machine records */
    QS_FILTER_ON(QS_AO_RECORDS); /* active object records */
    QS_FILTER_ON(QS_UA_RECORDS); /* all user records */

    return (uint8_t)1; /* return success */
}
/*..........................................................................*/
void QS_onCleanup(void) {
}
/*..........................................................................*/
QSTimeCtr QS_onGetTime(void) {  /* NOTE: invoked with interrupts DISABLED */
    if ((SysTick->CTRL & SysTick_CTRL_COUNTFLAG_Msk) == 0) { /* not set? */
        return QS_tickTime_ - (QSTimeCtr)SysTick->VAL;
    }
    else { /* the rollover occured, but the SysTick_ISR did not run yet */
        return QS_tickTime_ + QS_tickPeriod_ - (QSTimeCtr)SysTick->VAL;
    }
}
/*..........................................................................*/
void QS_onFlush(void) {
    uint16_t b;

    QF_INT_DISABLE();
    while ((b = QS_getByte()) != QS_EOD) { /* while not End-Of-Data... */
        QF_INT_ENABLE();
        while (!LL_USART_IsActiveFlag_TXE(USART2)) { /* while TXE not empty */
        }
        LL_USART_TransmitData8(USART2, b & 0xFFU);
        QF_INT_DISABLE();
    }
    QF_INT_ENABLE();
}
/*..........................................................................*/
/*! callback function to reset the target (to be implemented in the BSP) */
void QS_onReset(void) {
    NVIC_SystemReset();
}
/*..........................................................................*/
/*! callback function to execute a user command (to be implemented in BSP) */
void QS_onCommand(uint8_t cmdId,
                  uint32_t param1, uint32_t param2, uint32_t param3)
{
    void assert_failed(uint8_t *module, uint32_t loc);
    (void)cmdId;
    (void)param1;
    (void)param2;
    (void)param3;
    QS_BEGIN(COMMAND_STAT, (void *)1) /* application-specific record begin */
        QS_U8(2, cmdId);
        QS_U32(8, param1);
        QS_U32(8, param2);
        QS_U32(8, param3);
    QS_END()

    if (cmdId == 10U) {
        Q_ERROR();
    }
    else if (cmdId == 11U) {
        assert_failed((uint8_t*)"QS_onCommand", 123);
    }
}

#endif /* Q_SPY */
/*--------------------------------------------------------------------------*/

/*****************************************************************************
* NOTE00:
* The QF_AWARE_ISR_CMSIS_PRI constant from the QF port specifies the highest
* ISR priority that is disabled by the QF framework. The value is suitable
* for the NVIC_SetPriority() CMSIS function.
*
* Only ISRs prioritized at or below the QF_AWARE_ISR_CMSIS_PRI level (i.e.,
* with the numerical values of priorities equal or higher than
* QF_AWARE_ISR_CMSIS_PRI) are allowed to call any QF services. These ISRs
* are "QF-aware".
*
* Conversely, any ISRs prioritized above the QF_AWARE_ISR_CMSIS_PRI priority
* level (i.e., with the numerical values of priorities less than
* QF_AWARE_ISR_CMSIS_PRI) are never disabled and are not aware of the kernel.
* Such "QF-unaware" ISRs cannot call any QF services. The only mechanism
* by which a "QF-unaware" ISR can communicate with the QF framework is by
* triggering a "QF-aware" ISR, which can post/publish events.
*
* NOTE01:
* The QK_onIdle() callback is called with interrupts enabled.
*
* NOTE02:
* One of the LEDs is used to visualize the idle loop activity. The brightness
* of the LED is proportional to the frequency of invcations of the idle loop.
* Please note that the LED is toggled with interrupts locked, so no interrupt
* execution time contributes to the brightness of the User LED.
*/
