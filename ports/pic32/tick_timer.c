/*
 * Copyright (c) 2014-2015, Ari Suutari <ari@stonepile.fi>.
 * All rights reserved. 
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 
 *  1. Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *  2. Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *  3. The name of the author may not be used to endorse or promote
 *     products derived from this software without specific prior written
 *     permission. 
 * 
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS
 * OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT,
 * INDIRECT,  INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <picoos.h>
#include <string.h>

#if PORTCFG_TICK_TIMER > 0

/*
 * Tick source based on PIC32 TIMER A.
 */

#define PERIOD(scaler) ((PORTCFG_CRYSTAL_CLOCK / scaler) / HZ - 1)

void portInitClock(void)
{
    IFS0bits.T1IF = 0;
    IPC1bits.T1IP = PORT_MAX_IPL;
    IPC1bits.T1IS = 0;
    IEC0bits.T1IE = 1;

    // Figure out suitable prescaler based on
    // timer input clock and HZ.

#if PERIOD(1) <= 65535

    T1CONbits.TCKPS = 0;
    PR1 = PERIOD(1);

#elif PERIOD(8) <= 65535

    T1CONbits.TCKPS = 1;
    PR1 = PERIOD(8);

#elif PERIOD(64) <= 65535

    T1CONbits.TCKPS = 2;
    PR1 = PERIOD(64);

#elif PERIOD(256) <= 65535

    T1CONbits.TCKPS = 3;
    PR1 = PERIOD(256);

#else
#error "No suitable prescaler for timer for current HZ setting."
#endif
    
    T1CONbits.ON = 1;
}

/*
 * Timer interrupt handler.
 */
void  PORT_NAKED __attribute__((vector(_TIMER_1_VECTOR))) TimerHandler(void)
{
  portSaveContext();
  c_pos_intEnter();

  IFS0CLR = _IFS0_T1IF_MASK;
  c_pos_timerInterrupt();

  c_pos_intExitQuick();
  portRestoreContext();
}
#endif
