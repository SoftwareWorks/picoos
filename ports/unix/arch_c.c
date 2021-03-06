/*
 * Copyright (c) 2011-2013, Ari Suutari <ari@stonepile.fi>.
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

#define NANOINTERNAL
#include <picoos.h>
#include <stdlib.h>
#include <stdio.h>
#include <memory.h>
#include <assert.h>
#include <signal.h>
#include <sys/time.h>

#if NOSCFG_FEATURE_CONIN == 1
#include <termios.h>
#include <unistd.h>
#include <fcntl.h>
#endif

static void timerExpiredContext(void);
static void timerExpired(int sig, siginfo_t *info, void *uap);

#if PORTCFG_IRQ_STACK_SIZE >= PORTCFG_MIN_STACK_SIZE
static char sigStack[PORTCFG_IRQ_STACK_SIZE];
#else
static char sigStack[PORTCFG_MIN_STACK_SIZE];
#endif

ucontext_t sigContext;

/*
 * Initialize task context.
 */

#if (POSCFG_TASKSTACKTYPE == 1)

VAR_t p_pos_initTask(POSTASK_t task, 
                    UINT_t stacksize,
                    POSTASKFUNC_t funcptr, 
                    void *funcarg)
{
  int ret;
  volatile int stk = stacksize;

  if (stk < PORTCFG_MIN_STACK_SIZE)
    stk = PORTCFG_MIN_STACK_SIZE;

  ret = getcontext(&task->ucontext);
  assert(ret != -1);

  ret = getcontext(&task->uexit);
  assert(ret != -1);

  task->uexit.uc_link           = 0;
  task->uexit.uc_stack.ss_sp    = malloc(stk);
  task->uexit.uc_stack.ss_size  = stk;
  task->uexit.uc_stack.ss_flags = 0;

  makecontext(&task->uexit, (void(*)(void)) posTaskExit, 0);

  task->stack = malloc(stk);
  task->stackSize = stk;

  task->ucontext.uc_link           = &task->uexit;
  task->ucontext.uc_stack.ss_sp    = (char*)task->stack;
  task->ucontext.uc_stack.ss_size  = stk;
  task->ucontext.uc_stack.ss_flags = 0;
  sigemptyset(&task->ucontext.uc_sigmask);
  assert(task->ucontext.uc_stack.ss_sp != 0);

#if POSCFG_ARGCHECK > 1
  nosMemSet(task->stack, PORT_STACK_MAGIC, stk);
#endif

  makecontext(&task->ucontext, (void(*)(void)) funcptr, 1, funcarg);

  return 0;
}


void  p_pos_freeStack(POSTASK_t task)
{
  free(task->ucontext.uc_stack.ss_sp);
  free(task->uexit.uc_stack.ss_sp);
}

#else
#error "Error in configuration for the port (poscfg.h): POSCFG_TASKSTACKTYPE must be 1"
#endif

/*
 * Initialize system.
 */

void
p_pos_initArch(void)
{
  struct itimerval timer;
  struct sigaction sig;

  p_pos_blockSigs(NULL);

  memset(&sig, '\0', sizeof(sig));
  sig.sa_sigaction = timerExpired;
  sig.sa_flags = SA_RESTART | SA_SIGINFO; /* SA_NODEFER ?? */
  sigaction(SIGALRM, &sig, NULL);
  
  memset(&timer, '\0', sizeof(timer));
  timer.it_interval.tv_usec = (1000 * 1000) / HZ;
  timer.it_interval.tv_sec = timer.it_interval.tv_usec / 1000000;
  timer.it_interval.tv_usec = timer.it_interval.tv_usec % 1000000;
  timer.it_value.tv_sec = timer.it_interval.tv_sec;
  timer.it_value.tv_usec = timer.it_interval.tv_usec;
  setitimer(ITIMER_REAL, &timer, NULL);

#if NOSCFG_FEATURE_CONIN == 1

  struct termios ts;

  tcgetattr(0, &ts);

  cfmakeraw(&ts);
  ts.c_cc[VMIN]=1;
  ts.c_cc[VTIME]=0;

  tcsetattr(0, TCSANOW, &ts);
  fcntl(0, F_SETFL, fcntl(0, F_GETFL) | O_NONBLOCK);

#endif
}

/*
 * Called by pico]OS to switch tasks when not serving interrupt.
 * Since we run tasks in system/user mode, "swi" instruction is
 * used to generate an exception to get into suitable mode
 * for context switching. 
 *
 * The actual switching is then performed by armSwiHandler.
 */

void p_pos_softContextSwitch(void)
{
  int ret;
  ucontext_t* old;

#if 0
  printf("Soft CTXsw %s -> %s\n", posCurrentTask_g->deb.name, posNextTask_g->deb.name);
#endif
  old = &posCurrentTask_g->ucontext;
  posCurrentTask_g = posNextTask_g;

  ret = swapcontext(old, &posCurrentTask_g->ucontext);
  assert(ret != -1);
}

/*
 * Called by pico]OS at end of interrupt handler to switch task.
 * Before switching from current to next task it uses
 * current task stack to restore exception mode stack pointer
 * (which was saved by saveContext macro).
 * After switching task pointers the new task's context is simply restored
 * to get it running.
 */

void p_pos_intContextSwitch(void)
{
  int ret;

#if 0
  printf("Int CTXsw %s -> %s\n", posCurrentTask_g->deb.name, posNextTask_g->deb.name);
#endif
  posCurrentTask_g = posNextTask_g;

  ret = setcontext(&posCurrentTask_g->ucontext);
  assert(ret != -1);
}

/*
 * Called by pico]OS to start first task. Task
 * must be prepared by p_pos_initTask before calling this.
 */

void p_pos_startFirstContext()
{
  int ret;

  ret = setcontext(&posCurrentTask_g->ucontext);
  assert(ret != -1);
}

void p_pos_blockSigs(sigset_t* old)
{
  sigset_t set;

  sigfillset(&set);
  sigprocmask(SIG_BLOCK, &set, old);
}

void p_pos_unblockSigs(sigset_t* old)
{
  sigprocmask(SIG_SETMASK, old, NULL);
}

void p_pos_idleTaskHook()
{
  sigset_t set;

  sigemptyset(&set);
  sigsuspend(&set);
}

static void timerExpiredContext()
{
  c_pos_intEnter();
  c_pos_timerInterrupt();

#if NOSCFG_FEATURE_CONIN == 1

// Make a nonblocking read to stdin.

  unsigned char c;
  if (read(0, &c, 1) == 1)
    c_nos_keyinput(c);
  
#endif

  c_pos_intExit();
  setcontext(&posCurrentTask_g->ucontext);
  assert(0);
}

static void timerExpired(int sig, siginfo_t *info, void *ucontext)
{
  getcontext(&sigContext);
  sigContext.uc_stack.ss_sp = sigStack;
  sigContext.uc_stack.ss_size = sizeof(sigStack);
  sigContext.uc_stack.ss_flags = 0;
  sigContext.uc_link = 0;
  sigfillset(&sigContext.uc_sigmask);

  makecontext(&sigContext, timerExpiredContext, 0);
  swapcontext(&posCurrentTask_g->ucontext, &sigContext);
}

#if NOSCFG_FEATURE_CONOUT == 1
/*
 * Console output.
 */

UVAR_t
p_putchar(char c)
{
  putchar(c);
  fflush(stdout);
  return 1;
}

#endif

#ifdef HAVE_PLATFORM_ASSERT
void p_pos_assert(const char* text, const char *file, int line)
{
  fprintf(stderr, "ASSERT FAILED %s %d %s\n", file, line, text);
  abort();
}
#endif
