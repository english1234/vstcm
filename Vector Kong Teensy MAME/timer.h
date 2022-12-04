/***************************************************************************

  timer.c

  Functions needed to generate timing and synchronization between several
  CPUs.

***************************************************************************/

#ifndef __TIMER_H__
#define __TIMER_H__

extern double cycles_to_sec[];
extern double sec_to_cycles[];

#define TIME_IN_HZ(hz)        (1.0 / (double)(hz))
#define TIME_IN_CYCLES(c,cpu) ((double)(c) * cycles_to_sec[cpu])
#define TIME_IN_SEC(s)        ((double)(s))
#define TIME_IN_MSEC(ms)      ((double)(ms) * (1.0 / 1000.0))
#define TIME_IN_USEC(us)      ((double)(us) * (1.0 / 1000000.0))
#define TIME_IN_NSEC(us)      ((double)(us) * (1.0 / 1000000000.0))

#define TIME_NOW              (0.0)
#define TIME_NEVER            (1.0e30)

#define TIME_TO_CYCLES(cpu,t) ((int)((t) * sec_to_cycles[cpu]))


void timer_init (void);
void *timer_pulse (double period, int param, void (*callback)(int));
void *timer_set (double duration, int param, void (*callback)(int));
void timer_reset (void *which, double duration);
void timer_remove (void *which);
int timer_enable (void *which, int enable);
double timer_timeelapsed (void *which);
double timer_timeleft (void *which);
double timer_get_time (void);
double timer_starttime (void *which);
double timer_firetime (void *which);
int timer_schedule_cpu (int *cpu, int *cycles);
void timer_update_cpu (int cpu, int ran);
void timer_suspendcpu (int cpu, int suspend);
void timer_holdcpu (int cpu, int hold);
int timer_iscpususpended (int cpu);
void timer_suspendcpu_trigger (int cpu, int trigger);
void timer_holdcpu_trigger (int cpu, int trigger);
void timer_trigger (int trigger);


#endif
