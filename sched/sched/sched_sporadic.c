/****************************************************************************
 * sched/sched/sched_sporadic.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.  The
 * ASF licenses this file to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance with the
 * License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.  See the
 * License for the specific language governing permissions and limitations
 * under the License.
 *
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <stdint.h>
#include <stdbool.h>
#include <inttypes.h>
#include <string.h>
#include <assert.h>
#include <debug.h>
#include <errno.h>

#include <nuttx/sched.h>
#include <nuttx/arch.h>
#include <nuttx/hrtimer.h>
#include <nuttx/clock.h>
#include <nuttx/list.h>
#include <nuttx/nuttx.h>

#include <arch/irq.h>

#include "clock/clock.h"
#include "sched/sched.h"

#ifdef CONFIG_SCHED_SPORADIC

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

static int sporadic_set_lowpriority(FAR struct tcb_s *tcb);
static int sporadic_set_hipriority(FAR struct tcb_s *tcb);
static int32_t sporadic_budget_check(FAR struct tcb_s *tcb, clock_t now);
static int32_t sporadic_merge_capacity(FAR struct sporadic_s *sporadic,
                                       clock_t now);
static uint64_t sporadic_timer_expire(FAR const struct hrtimer_s *timer,
                                      uint64_t expired);
static void sporadic_timer_start_background(FAR struct tcb_s *tcb,
                                            clock_t now, clock_t delay);

/****************************************************************************
 * Inline Functions
 ****************************************************************************/

/****************************************************************************
 * Name: sporadic_q_head
 *
 * Description:
 *   Get the head (earliest) chunk of the replenishment queue.
 *
 * Input Parameters:
 *   sporadic - The sporadic scheduling state.
 *
 * Returned Value:
 *   Pointer to the head replenishment chunk.  The queue must not be
 *   empty.
 *
 ****************************************************************************/

static inline_function
FAR struct sporadic_repl_s *sporadic_q_head(FAR struct sporadic_s *sporadic)
{
  DEBUGASSERT(!list_is_empty(&sporadic->repls));
  return list_first_entry(&sporadic->repls, struct sporadic_repl_s, node);
}

/* Delay until the head chunk becomes eligible */

#define sporadic_q_delay(sporadic, now) \
  (sporadic_q_head(sporadic)->time - (now))

/****************************************************************************
 * Name: sporadic_q_insert
 *
 * Description:
 *   Insert a replenishment chunk into the queue, ordered by eligibility
 *   time (earliest first).
 *
 * Input Parameters:
 *   sporadic - The sporadic scheduling state.
 *   repl     - The chunk to insert.  repl->amt must be greater than
 *              zero.
 *
 ****************************************************************************/

static inline_function
void sporadic_q_insert(FAR struct sporadic_s *sporadic,
                       FAR struct sporadic_repl_s *repl)
{
  FAR struct sporadic_repl_s *curr;

  DEBUGASSERT(repl->amt > 0);

  list_for_every_entry(&sporadic->repls, curr, struct sporadic_repl_s, node)
    {
      if (curr->time > repl->time)
        {
          break;
        }
    }

  list_add_before(&curr->node, &repl->node);
}

/****************************************************************************
 * Name: sporadic_q_alloc
 *
 * Description:
 *   Pop a chunk from the free stack (LIFO).
 *
 * Input Parameters:
 *   sporadic - The sporadic scheduling state.
 *
 * Returned Value:
 *   Pointer to the allocated chunk.  The free stack must not be empty.
 *
 ****************************************************************************/

static inline_function
FAR struct sporadic_repl_s *sporadic_q_alloc(FAR struct sporadic_s *sporadic)
{
  FAR struct sporadic_repl_s *repl = sporadic->free;

  DEBUGASSERT(repl != NULL);

  sporadic->free = container_of(repl->node.next,
                                struct sporadic_repl_s, node);
  return repl;
}

/****************************************************************************
 * Name: sporadic_q_free
 *
 * Description:
 *   Push a chunk back onto the free stack (LIFO).
 *
 * Input Parameters:
 *   sporadic - The sporadic scheduling state.
 *   repl     - The chunk to release.
 *
 ****************************************************************************/

static inline_function
void sporadic_q_free(FAR struct sporadic_s *sporadic,
                     FAR struct sporadic_repl_s *repl)
{
  repl->node.next  = (FAR struct list_node *)sporadic->free;
  sporadic->free   = repl;
}

/****************************************************************************
 * Name: sporadic_q_sum
 *
 * Description:
 *   Sum of all chunk amounts in the queue.  The invariant is that this
 *   always equals the budget.
 *
 * Input Parameters:
 *   sporadic - The sporadic scheduling state.
 *
 * Returned Value:
 *   The sum of amt across all chunks in the replenishment queue.
 *
 ****************************************************************************/

static inline_function
uint32_t sporadic_q_sum(FAR struct sporadic_s *sporadic)
{
  FAR struct sporadic_repl_s *repl;
  uint32_t sum = 0;

  list_for_every_entry(&sporadic->repls, repl, struct sporadic_repl_s, node)
    {
      sum += repl->amt;
    }

  return sum;
}

/****************************************************************************
 * Name: sporadic_capacity
 *
 * Description:
 *   Compute the currently available execution capacity:
 *
 *     head.amt - usage   if the head chunk is eligible (can be negative
 *                        on overrun),
 *     0                  if the head chunk is not yet eligible.
 *
 * Input Parameters:
 *   sporadic - The sporadic scheduling state.
 *   now      - The current system time in ticks.
 *
 * Returned Value:
 *   The available capacity in ticks.  May be negative on overrun.
 *
 ****************************************************************************/

static inline_function
int32_t sporadic_capacity(FAR struct sporadic_s *sporadic, clock_t now)
{
  FAR struct sporadic_repl_s *head;
  int32_t capacity = 0;

  DEBUGASSERT(!list_is_empty(&sporadic->repls));
  head = sporadic_q_head(sporadic);

  if (clock_compare(head->time, now))
    {
      capacity = (int32_t)(head->amt - sporadic->usage);
    }

  return capacity;
}

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: sporadic_set_lowpriority
 *
 * Description:
 *   Force the thread to lower priority.
 *
 * Input Parameters:
 *   tcb - TCB of task whose priority will be modified
 *
 * Returned Value:
 *   Returns zero (OK) on success or a negated errno value on failure.
 *
 ****************************************************************************/

static int sporadic_set_lowpriority(FAR struct tcb_s *tcb)
{
  FAR struct sporadic_s *sporadic;
  int ret = OK;

  DEBUGASSERT(tcb != NULL && tcb->sporadic != NULL);
  sporadic = tcb->sporadic;

#ifdef CONFIG_SPORADIC_INSTRUMENTATION
  /* Inform the monitor of this event */

  arch_sporadic_lowpriority(tcb);
#endif

#ifdef CONFIG_PRIORITY_INHERITANCE
  /* If the priority was boosted above the higher priority, than just
   * reset the base priority and continue to run at the boosted priority.
   *
   * REVISIT:  There is a logic flaw here... If the priority was NOT
   * boosted above the hi_priority, then it still may still need to
   * boosted with respect to the lo_priority.  If the highest priority
   * thread waiting on a semaphore held by the sporadic thread is greater
   * than the low priority (but less than the hi_priority), the new
   * sched_priority should be set to that priority, not to the lo_priority
   *
   * In order to do this we would need to know the highest priority from
   * among all tasks waiting for the all semaphores held by the sporadic
   * task.  That information could be retained by the priority inheritance
   * logic of sem_holder.c for use here.
   */

  if (tcb->sched_priority > tcb->base_priority)
    {
      /* Thread priority was boosted while we were in the high priority
       * state.
       */

      tcb->base_priority = sporadic->low_priority;
    }
  else
#endif
    {
      /* Otherwise drop the priority of thread, possible causing a context
       * switch.
       */

      ret = nxsched_reprioritize(tcb, sporadic->low_priority);
      if (ret < 0)
        {
          serr("ERROR: nxsched_reprioritize failed: %d\n", ret);
        }
    }

  return ret;
}

/****************************************************************************
 * Name: sporadic_set_hipriority
 *
 * Description:
 *   Force the thread to higher priority.
 *
 * Input Parameters:
 *   tcb - TCB of task whose priority will be modified
 *
 * Returned Value:
 *   Returns zero (OK) on success or a negated errno value on failure.
 *
 ****************************************************************************/

static int sporadic_set_hipriority(FAR struct tcb_s *tcb)
{
  FAR struct sporadic_s *sporadic;
  bool boosted = false;
  int  ret     = OK;

  DEBUGASSERT(tcb != NULL && tcb->sporadic != NULL);
  sporadic = tcb->sporadic;

#ifdef CONFIG_SPORADIC_INSTRUMENTATION
  /* Inform the monitor of this event */

  arch_sporadic_start(tcb);
#endif

#ifdef CONFIG_PRIORITY_INHERITANCE
  /* If the priority was boosted above the higher priority, then just
   * reset the base priority.
   *
   * First, was the priority boosted above the lo_priority which should be
   * the same as the base_priority here? (This is an unnecessary test.
   * sched_priority > hi_priority would be sufficient).
   */

  if (tcb->sched_priority > tcb->base_priority &&
      tcb->sched_priority > sporadic->hi_priority)
    {
      /* Boosted... Do we still need to reprioritize?  If we were boosted to
       * a priority above the hi_priority then we do not need to do anything
       * except to adjust the base_priority
       *
       * REVISIT:  This logic is probably okay.  But may lead to problems
       * when the hi_priority is resumed.  See REVISIT comments in
       * sporadic_set_lowpriority().
       */

      tcb->base_priority = sporadic->hi_priority;
      boosted = true;
    }

  /* The thread priority has not been boosted or it has been boosted to a
   * lower priority than the high priority.  So, in either case, we need to
   * reset the priority.
   */

#endif

  /* Then reprioritize to the higher priority */

  if (!boosted)
    {
      ret = nxsched_reprioritize(tcb, sporadic->hi_priority);
      if (ret < 0)
        {
          serr("ERROR: nxsched_reprioritize failed: %d\n", ret);
        }
    }

  return ret;
}

/****************************************************************************
 * Name: sporadic_merge_capacity
 *
 * Description:
 *   Merge all eligible (expired) chunks at the head of the queue into
 *   a single chunk and advance its eligibility to now (RTAS'10 Figure
 *   5, lines 4-11).  Then return the available capacity.
 *
 * Input Parameters:
 *   sporadic - The sporadic scheduling state.
 *   now      - The current system time in ticks.
 *
 * Returned Value:
 *   The available execution capacity: head.amt - usage if the head
 *   chunk is eligible, zero otherwise.
 *
 ****************************************************************************/

static int32_t sporadic_merge_capacity(FAR struct sporadic_s *sporadic,
                                       clock_t now)
{
  FAR struct sporadic_repl_s *head;
  FAR struct sporadic_repl_s *next;

  DEBUGASSERT(!list_is_empty(&sporadic->repls));
  head = sporadic_q_head(sporadic);

  /* Merge all eligible chunks into the head (RTAS'10 Figure 5, lines
   * 4-11).  The queue is sorted by time, so eligible chunks are
   * contiguous at the front.
   */

  if (clock_compare(head->time, now))
    {
      while (!list_is_tail(&sporadic->repls, &head->node))
        {
          next = list_next_entry(head, struct sporadic_repl_s, node);
          if (!clock_compare(next->time, now + head->amt - sporadic->usage))
            {
              break;
            }

          head->amt += next->amt;
          list_delete_fast(&next->node);
          sporadic_q_free(sporadic, next);
        }

      head->time = now;
    }

  DEBUGASSERT(sporadic_q_sum(sporadic) == (sporadic)->budget);

  return sporadic_capacity(sporadic, now);
}

/****************************************************************************
 * Name: sporadic_timer_start
 *
 * Description:
 *   Arm the per-task replenishment hrtimer.  In the foreground the
 *   timer fires when the remaining capacity is exhausted; in the
 *   background it fires when the head chunk becomes eligible.
 *
 * Input Parameters:
 *   tcb - The TCB of the sporadic thread.
 *   now - The current system time in ticks.
 *   delay - The delay to trigger the scheduler event.
 *
 ****************************************************************************/

static inline_function
void sporadic_timer_start_background(FAR struct tcb_s *tcb,
                                     clock_t now, clock_t delay)
{
  FAR struct sporadic_s *sporadic = tcb->sporadic;
  hrtimer_start(&sporadic->replenishment, sporadic_timer_expire,
                TICK2NSEC((uint64_t)now + delay), HRTIMER_MODE_ABS);
}

#ifdef CONFIG_SCHED_TICKLESS
#  define sporadic_timer_cancel_foreground(tcb) \
     hrtimer_cancel(&(tcb)->sporadic->replenishment)
#else
#  define sporadic_timer_cancel_foreground(tcb)
#endif

#ifdef CONFIG_SCHED_TICKLESS
static void sporadic_timer_start_foreground(FAR struct tcb_s *tcb,
                                            clock_t now, clock_t delay)
{
  sporadic_timer_start_background(tcb, now, delay);
}
#else
#  define sporadic_timer_start_foreground(tcb, now, delay)
#endif

/****************************************************************************
 * Name: sporadic_budget_check
 *
 * Description:
 *   BUDGET CHECK (RTAS'10 Figure 3).  Called when the available
 *   capacity has dropped to zero: on budget exhaustion, on suspension
 *   and on scheduler unlock.
 *
 *   - Fully consumed chunks are recycled one replenishment period into
 *     the future (lines 2-7).
 *   - A remaining overrun is charged against the next chunk by
 *     postponing its eligibility (lines 8-9).
 *   - If the postponed head time overlaps the next chunk, the two
 *     are merged (lines 10-13).  This restores the sorted order of
 *     the queue after the postponement and bounds fragmentation.
 *   - If no chunk is eligible now, the thread is dropped to the
 *     background priority and the replenishment timer is armed
 *     (lines 14-17).
 *
 * Input Parameters:
 *   tcb        - The TCB of the sporadic thread.
 *   now        - The current system time in ticks.
 *   noswitches - True to defer the priority drop (scheduler locked).
 *
 * Returned Value:
 *   The new timeslice: remaining capacity if positive, zero if the
 *   budget is exhausted, or -1 if the priority drop is deferred.
 *
 ****************************************************************************/

static int32_t sporadic_budget_check(FAR struct tcb_s *tcb, clock_t now)
{
  FAR struct sporadic_s  *sporadic = tcb->sporadic;
  FAR struct sporadic_repl_s *head = sporadic_q_head(sporadic);
  int32_t capacity;

  /* Recycle fully consumed chunks one period into the future.  The
   * invariant sum(amt) == budget guarantees that at least one chunk
   * remains when the loop exits.
   */

  while (head->amt <= sporadic->usage)
    {
      sporadic->usage -= head->amt;
      head->time      += sporadic->repl_period;

      /* Move head to the correct position (sorted by time) */

      list_delete_fast(&head->node);
      sporadic_q_insert(sporadic, head);

      head = sporadic_q_head(sporadic);
    }

  /* Charge the overrun (borrowed from the next chunk) by postponing the
   * eligibility of the head.
   */

  if (sporadic->usage > 0)
    {
      sinfo("overrun: head.time=%" PRIu64 " += usage=%" PRIu32 "\n",
            (uint64_t)head->time, sporadic->usage);

      /* Postpone the head eligibility by the overrun amount and merge
       * every chunk that becomes eligible by then into the head (RTAS'10
       * Figure 3, lines 10-13).  Merging at the postponed time keeps the
       * queue sorted by construction, and never makes the budget
       * available earlier than the sporadic constraint allows.
       */

      sporadic_merge_capacity(sporadic, head->time + sporadic->usage);
      sporadic->usage = 0;
    }

  capacity = sporadic_capacity(sporadic, now);
  DEBUGASSERT(sporadic_q_sum(sporadic) == (sporadic)->budget);

  return capacity;
}

/****************************************************************************
 * Name: sporadic_split_check
 *
 * Description:
 *   SPLIT CHECK (RTAS'10 Figure 4).  Called when the thread suspends
 *   with a partially consumed, eligible head chunk: the consumed part
 *   becomes a distinct chunk replenished one period after the head
 *   eligibility, the un-consumed remnant stays available at the
 *   original time.
 *
 *   When the queue is full the split is skipped; the consumed amount
 *   will be charged when the chunk fully depletes via budget check.
 *   The queue-full merge fallback is deferred to a follow-up patch.
 *
 * Input Parameters:
 *   tcb - The TCB of the sporadic thread.
 *   now - The current system time in ticks.
 *
 ****************************************************************************/

static void sporadic_split_check(FAR struct tcb_s *tcb, clock_t now)
{
  FAR struct sporadic_s *sporadic = tcb->sporadic;
  FAR struct sporadic_repl_s *head;
  FAR struct sporadic_repl_s *newrepl;

  if (sporadic->usage > 0 && sporadic->free != NULL)
    {
      head = sporadic_q_head(sporadic);
      if (clock_compare(head->time, now))
        {
          newrepl = sporadic_q_alloc(sporadic);

          /* Split: consumed part becomes a new chunk replenished one
           * period after the head eligibility; the remnant stays at
           * the original time.
           */

          newrepl->time = head->time + sporadic->repl_period;
          newrepl->amt  = sporadic->usage;
          head->amt    -= sporadic->usage;
          sporadic_q_insert(sporadic, newrepl);

          sporadic->usage = 0;
          tcb->timeslice = sporadic_capacity(sporadic, now);
        }
    }

  DEBUGASSERT(sporadic_q_sum(sporadic) == (sporadic)->budget);
}

/****************************************************************************
 * Name: sporadic_timer_expire
 *
 * Description:
 *   hrtimer callback for the per-task replenishment timer.  Handles
 *   two events, distinguished by the current timeslice value:
 *
 *   - Background (timeslice == 0): the head chunk just became
 *     eligible.  Restore the foreground priority if capacity is
 *     available.
 *   - Foreground (timeslice > 0): budget exhaustion.  Charge the
 *     elapsed execution time and run BUDGET CHECK.
 *
 * Input Parameters:
 *   timer   - The hrtimer that expired.
 *   expired - The expiration time in nanoseconds.
 *
 * Returned Value:
 *   Zero (no re-arm).
 *
 ****************************************************************************/

static uint64_t sporadic_timer_expire(FAR const struct hrtimer_s *timer,
                                      uint64_t expired)
{
  FAR struct sporadic_s *sporadic =
    container_of(timer, struct sporadic_s, replenishment);
  FAR struct tcb_s *tcb = sporadic->tcb;
  clock_t          now  = NSEC2TICK(expired);
  clock_t         delay = 0u;
  irqstate_t      flags = enter_critical_section();
  int32_t      capacity;

  /* Skip stale timers: the expected expiration must match. */

  if (expired == timer->expired)
    {
      if (tcb->timeslice == 0)
        {
          /* Background: replenishment event. Restore foreground priority
           * if the head chunk is now eligible. Otherwise re-arm the
           * timer for when it becomes eligible.
           */

          capacity = sporadic_capacity(sporadic, now);
          if (capacity > 0)
            {
              tcb->starttime = now;
              tcb->timeslice = capacity;

#ifdef CONFIG_SCHED_TICKLESS
              if (tcb->task_state == TSTATE_TASK_RUNNING)
                {
                  /* Restart the foreground timer. */

                  delay = capacity;
                }
#endif

              sporadic_set_hipriority(tcb);
            }
          else
            {
              delay = sporadic_q_delay(sporadic, now);
            }
        }
      else
        {
          /* Foreground: budget exhaustion event. Charge the elapsed time
           * and run BUDGET CHECK.
           */

          sporadic->usage += now - tcb->starttime;
          tcb->starttime   = now;
          capacity         = sporadic_budget_check(tcb, now);

          if (capacity > 0)
            {
              tcb->timeslice = capacity;
#ifdef CONFIG_SCHED_TICKLESS

              /* Restart the foreground timer. */

              delay = capacity;
#endif
            }
          else
            {
              /* Budget exhausted: drop to low priority or defer. */

              if (!nxsched_islocked_tcb(tcb))
                {
                  tcb->timeslice = 0;
                  sporadic_set_lowpriority(tcb);
                  delay = sporadic_q_delay(sporadic, now);
                }
              else
                {
                  tcb->timeslice = -1;
                }
            }
        }
    }

  leave_critical_section(flags);

  return TICK2NSEC((uint64_t)delay);
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: nxsched_validate_sporadic
 *
 * Description:
 *   Validate sporadic scheduling parameters and convert the replenishment
 *   period and initial budget to system clock ticks.
 *
 * Input Parameters:
 *   param        - Sporadic scheduling parameters to validate.
 *   repl_ticks   - Location to return the replenishment period in ticks.
 *   budget_ticks - Location to return the initial budget in ticks.
 *
 * Returned Value:
 *   Zero (OK) is returned on success.  A negated errno value is returned
 *   on failure.
 *
 ****************************************************************************/

int nxsched_validate_sporadic(FAR const struct sched_param *param,
                              FAR clock_t *repl_ticks,
                              FAR clock_t *budget_ticks)
{
  clock_t repl;
  clock_t budget;
  int ret = OK;

  if (param->sched_ss_low_priority < SCHED_PRIORITY_MIN ||
      param->sched_ss_low_priority > SCHED_PRIORITY_MAX ||
      param->sched_ss_max_repl < 1 ||
      param->sched_ss_max_repl > SS_REPL_MAX)
    {
      ret = -EINVAL;
    }
  else
    {
      /* Convert timespec values to system clock ticks, avoiding zero */

      repl = clock_time2ticks(&param->sched_ss_repl_period);
      if (repl < 1)
        {
          repl = 1;
        }

      budget = clock_time2ticks(&param->sched_ss_init_budget);
      if (budget < 1)
        {
          budget = 1;
        }

      /* The budget may not exceed the replenishment period.  This also
       * guarantees that the execution of any chunk after an unblock fits
       * within one replenishment period, which the replenishment rules
       * rely on.
       */

      if (budget > repl)
        {
          ret = -EINVAL;
        }
      else
        {
          *repl_ticks   = repl;
          *budget_ticks = budget;
        }
    }

  return ret;
}

/****************************************************************************
 * Name: nxsched_initialize_sporadic
 *
 * Description:
 *   Allocate the sporadic add-on data structure for a thread.  The
 *   replenishment pool is allocated separately in
 *   nxsched_start_sporadic().
 *
 * Input Parameters:
 *   tcb - The TCB of the thread to initialize.
 *
 * Returned Value:
 *   Zero (OK) on success; -ENOMEM if allocation fails.
 *
 ****************************************************************************/

int nxsched_initialize_sporadic(FAR struct tcb_s *tcb)
{
  FAR struct sporadic_s *sporadic;
  int ret = OK;

  DEBUGASSERT(tcb != NULL && tcb->sporadic == NULL);

  /* Allocate the sporadic add-on data structure that will hold the
   * sporadic scheduling parameters and state data.  The replenishment
   * pool will be allocated separately in nxsched_start_sporadic().
   */

  sporadic = kmm_zalloc(sizeof(struct sporadic_s));
  if (sporadic == NULL)
    {
      serr("ERROR: Failed to allocate sporadic data structure\n");
      ret = -ENOMEM;
    }
  else
    {
      list_initialize(&sporadic->repls);
      hrtimer_init(&sporadic->replenishment);
      tcb->sporadic = sporadic;
    }

  return ret;
}

/****************************************************************************
 * Name: nxsched_start_sporadic
 *
 * Description:
 *   Called to initialize sporadic scheduling on a given thread.  This
 *   function is called in the following circumstances:
 *
 *     - When starting a pthread with sporadic scheduling specified in
 *       the pthread attributes.
 *     - When establishing sporadic scheduling policy via
 *       sched_setscheduler()
 *     - When the sporadic scheduling parameters are changed via
 *       sched_setparam().
 *
 *   The full budget starts as a single eligible chunk at the head of
 *   the replenishment queue.  The high priority is applied and the
 *   replenishment timer is armed for budget exhaustion.
 *
 * Input Parameters:
 *   tcb - The TCB of the thread to start.
 *
 * Returned Value:
 *   Zero (OK) on success; -ENOMEM if pool allocation fails, or a
 *   negated errno value from nxsched_reprioritize().
 *
 ****************************************************************************/

int nxsched_start_sporadic(FAR struct tcb_s *tcb)
{
  FAR struct sporadic_s      *sporadic;
  FAR struct sporadic_repl_s *repl;
  clock_t                     now;
  int                         ret = OK;

  DEBUGASSERT(tcb && tcb->sporadic);
  sporadic = tcb->sporadic;
  now      = clock_systime_ticks();

  DEBUGASSERT(sporadic->budget > 0 && sporadic->budget <=
              sporadic->repl_period);
  DEBUGASSERT(sporadic->max_repl > 0);

  /* Allocate the replenishment pool */

  sporadic->pool = kmm_zalloc(sporadic->max_repl *
                              sizeof(struct sporadic_repl_s));
  if (sporadic->pool == NULL)
    {
      serr("ERROR: Failed to allocate replenishment pool\n");
      ret = -ENOMEM;
    }
  else
    {
      int idx;
      tstate_t state;

      atomic_or(&tcb->flags, TCB_FLAG_SCHED_SPORADIC);

      sporadic->tcb = tcb;

      /* Build the free stack (LIFO): push all entries in reverse
       * order so that pool[0] is popped first.
       */

      sporadic->free = NULL;
      for (idx = sporadic->max_repl - 1; idx >= 0; idx--)
        {
          sporadic_q_free(sporadic, &sporadic->pool[idx]);
        }

      /* Pop initial chunk from the free stack */

      repl = sporadic_q_alloc(sporadic);
      repl->time = now;
      repl->amt  = sporadic->budget;
      list_add_tail(&sporadic->repls, &repl->node);

      sporadic->usage = 0;
      tcb->timeslice  = sporadic->budget;
      state           = tcb->task_state;

      ret = sporadic_set_hipriority(tcb);

      /* Arm the timer for budget exhaustion if the task is running. */

      if (state == TSTATE_TASK_RUNNING)
        {
          sporadic_timer_start_foreground(tcb, now, tcb->timeslice);
        }
    }

  return ret;
}

/****************************************************************************
 * Name: nxsched_stop_sporadic
 *
 * Description:
 *   Terminate sporadic scheduling on a given thread and free all
 *   resources associated with the policy.
 *
 * Input Parameters:
 *   tcb - The TCB of the thread to stop.
 *
 * Returned Value:
 *   Zero (OK).
 *
 ****************************************************************************/

int nxsched_stop_sporadic(FAR struct tcb_s *tcb)
{
  DEBUGASSERT(tcb && tcb->sporadic);

  nxsched_reset_sporadic(tcb);

  /* Ensure there is no reference for the timer. */

  hrtimer_cancel_sync(&tcb->sporadic->replenishment);

  kmm_free(tcb->sporadic);
  tcb->sporadic = NULL;
  return OK;
}

/****************************************************************************
 * Name: nxsched_reset_sporadic
 *
 * Description:
 *   Reset the sporadic scheduling state to idle: cancel the
 *   replenishment timer, drain the replenishment queue and free the
 *   pool.  Called when the sporadic scheduling parameters are changed
 *   via sched_setparam(), or from nxsched_stop_sporadic().
 *
 * Input Parameters:
 *   tcb - The TCB of the thread to reset.
 *
 * Returned Value:
 *   Zero (OK).
 *
 ****************************************************************************/

int nxsched_reset_sporadic(FAR struct tcb_s *tcb)
{
  FAR struct sporadic_s *sporadic;

  DEBUGASSERT(tcb && tcb->sporadic);
  sporadic = tcb->sporadic;

  /* Cancel the replenishment timer asynchronously. */

  hrtimer_cancel(&sporadic->replenishment);

  list_initialize(&sporadic->repls);

  atomic_and(&tcb->flags, ~TCB_FLAG_POLICY_MASK);

  kmm_free(sporadic->pool);
  sporadic->pool = NULL;
  sporadic->free = NULL;

  sporadic->hi_priority  = 0;
  sporadic->low_priority = 0;
  sporadic->max_repl     = 0;
  sporadic->repl_period  = 0;
  sporadic->budget       = 0;
  sporadic->usage        = 0;
  return OK;
}

/****************************************************************************
 * Name: nxsched_resume_sporadic
 *
 * Description:
 *   UNBLOCK CHECK (RTAS'10 Figure 5).  Called when a thread using the
 *   sporadic scheduling policy is resumed:
 *
 *   - Merge all eligible chunks into one and advance its eligibility
 *     to now (lines 4-11).
 *   - If capacity remains, restore the foreground priority (lines
 *     1-3).
 *   - Arm the replenishment timer for the next event (line 13).
 *
 * Input Parameters:
 *   tcb - The TCB of the resumed thread.
 *
 * Returned Value:
 *   Zero (OK).
 *
 ****************************************************************************/

void nxsched_resume_sporadic(FAR struct tcb_s *tcb, clock_t now)
{
  FAR struct sporadic_s *sporadic;
  int32_t                capacity;

  DEBUGASSERT(tcb && tcb->sporadic &&
              sporadic_q_sum(tcb->sporadic) == tcb->sporadic->budget);

  sporadic = tcb->sporadic;

#ifdef CONFIG_SPORADIC_INSTRUMENTATION
  arch_sporadic_resume(tcb);
#endif

  capacity = sporadic_merge_capacity(sporadic, now);

  tcb->timeslice = capacity;
  tcb->starttime = now;

  if (capacity > 0)
    {
      /* Start the foreground timer for the budget exhaustion */

      sporadic_timer_start_foreground(tcb, now, capacity);
    }
}

/****************************************************************************
 * Name: nxsched_suspend_sporadic
 *
 * Description:
 *   Charge the elapsed execution time of the closing window against
 *   the head chunk, then settle the queue: BUDGET CHECK when the
 *   capacity is exhausted, SPLIT CHECK otherwise.  The replenishment
 *   timer is cancelled because the thread is no longer executing.
 *
 * Input Parameters:
 *   tcb     - The TCB of the suspended thread.
 *   now     - The current tick
 *
 ****************************************************************************/

void nxsched_suspend_sporadic(FAR struct tcb_s *tcb, clock_t now)
{
  FAR struct sporadic_s *sporadic;

  DEBUGASSERT(tcb && tcb->sporadic);
  sporadic = tcb->sporadic;

#ifdef CONFIG_SPORADIC_INSTRUMENTATION
  arch_sporadic_suspend(tcb);
#endif

  /* Cancel the foreground timer: the thread is no longer executing */

  sporadic_timer_cancel_foreground(tcb);

  if (tcb->timeslice != 0)
    {
      DEBUGASSERT(clock_compare(tcb->starttime, now));
      sporadic->usage += now - tcb->starttime;
    }

  if (sporadic_capacity(sporadic, now) <= 0)
    {
      int32_t capacity = sporadic_budget_check(tcb, now);

      if (capacity <= 0)
        {
          tcb->timeslice = 0;
          sporadic_set_lowpriority(tcb);

          /* Start the background timer. */

          sporadic_timer_start_background(tcb, now,
                               sporadic_q_delay(sporadic, now));
        }
    }
  else
    {
      sporadic_split_check(tcb, now);
    }
}

/****************************************************************************
 * Name: nxsched_process_sporadic
 *
 * Description:
 *   Charge the elapsed execution time against the head chunk.  Called:
 *
 *   - From the replenishment timer handler while the thread with
 *     sporadic scheduling is running (tickless), with the elapsed
 *     time of the closed window.
 *   - From the periodic timer interrupt, with one tick.
 *
 * Input Parameters:
 *   tcb        - The TCB of the running sporadic thread.
 *   ticks      - The elapsed execution time in ticks.
 *   noswitches - True to defer the priority drop (scheduler locked).
 *
 * Returned Value:
 *   The number of ticks remaining until the budget is exhausted, or
 *   CLOCK_MAX if the thread is running at the background priority.
 *
 ****************************************************************************/

void nxsched_process_sporadic(FAR struct tcb_s *tcb, clock_t ticks,
                              bool noswitches)
{
  FAR struct sporadic_s *sporadic;
  clock_t                now;

  DEBUGASSERT(tcb != NULL && tcb->sporadic != NULL && ticks > 0);
  sporadic = tcb->sporadic;

  if (tcb->timeslice != 0)
    {
      int32_t capacity;

      now = clock_systime_ticks();
      tcb->starttime = now;

      /* Usage may exceed budget: timing jitter between the timer
       * expiration and the accounting must not be silently discarded,
       * otherwise the server gains free execution time (budget
       * amplification).  The overrun is charged by budget_check.
       */

      sporadic->usage += ticks;

      capacity = sporadic_capacity(sporadic, now);
      if (capacity > 0)
        {
          tcb->timeslice = capacity;
        }
      else
        {
          capacity = sporadic_budget_check(tcb, now);

          if (capacity > 0)
            {
              tcb->timeslice = capacity;
            }
          else if (!noswitches)
            {
              tcb->timeslice = 0;
              sporadic_set_lowpriority(tcb);

              /* Start the background timer. */

              sporadic_timer_start_background(tcb, now,
                                   sporadic_q_delay(sporadic, now));
            }
          else
            {
              /* Scheduler Locked. */

              tcb->timeslice = -1;
            }
        }
    }
}

/****************************************************************************
 * Name: nxsched_sporadic_lowpriority
 *
 * Description:
 *   Complete a deferred BUDGET CHECK.  Called from sched_unlock() when
 *   the budget expired while the thread had the scheduler locked.
 *
 * Input Parameters:
 *   tcb - The TCB of the sporadic thread.
 *
 ****************************************************************************/

void nxsched_sporadic_lowpriority(FAR struct tcb_s *tcb)
{
  clock_t now;
  int32_t capacity;

  DEBUGASSERT(tcb && tcb->sporadic);

  now      = clock_systime_ticks();
  capacity = sporadic_budget_check(tcb, now);

  if (capacity > 0)
    {
      tcb->timeslice = capacity;

      /* Start the foreground timer. */

      sporadic_timer_start_foreground(tcb, now, capacity);
    }
  else
    {
      tcb->timeslice = 0;
      sporadic_set_lowpriority(tcb);

      /* Start the background timer. */

      sporadic_timer_start_background(tcb, now,
                           sporadic_q_delay(tcb->sporadic, now));
    }
}

#endif /* CONFIG_SCHED_SPORADIC */
