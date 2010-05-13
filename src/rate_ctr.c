/* utility routines for keeping conters about events and the event rates */

/* (C) 2009-2010 by Harald Welte <laforge@gnumonks.org>
 *
 * All Rights Reserved
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 */

#include <stdint.h>
#include <inttypes.h>
#include <string.h>

#include <osmocore/linuxlist.h>
#include <osmocore/talloc.h>
#include <osmocore/timer.h>
#include <osmocore/rate_ctr.h>

static LLIST_HEAD(rate_ctr_groups);

static void *tall_rate_ctr_ctx;

struct rate_ctr_group *rate_ctr_group_alloc(void *ctx,
					    const struct rate_ctr_group_desc *desc,
					    unsigned int idx)
{
	unsigned int size;
	struct rate_ctr_group *group;

	size = sizeof(struct rate_ctr_group) +
			desc->num_ctr * sizeof(struct rate_ctr);

	if (!ctx)
		ctx = tall_rate_ctr_ctx;

	group = talloc_zero_size(ctx, size);
	if (!group)
		return NULL;

	group->desc = desc;
	group->idx = idx;
	/* Generate the Group prefix from the user-specified index */
	group->name_prefix = talloc_size(group, strlen(desc->group_prefix_fmt) + 20);
	sprintf(group->name_prefix, desc->group_prefix_fmt, idx);

	llist_add(&group->list, &rate_ctr_groups);

	return group;
}

void rate_ctr_group_free(struct rate_ctr_group *grp)
{
	llist_del(&grp->list);
	talloc_free(grp);
}

void rate_ctr_add(struct rate_ctr *ctr, int inc)
{
	ctr->current += inc;
}

static void interval_expired(struct rate_ctr *ctr, enum rate_ctr_intv intv)
{
	/* calculate rate over last interval */
	ctr->intv[intv].rate = ctr->current - ctr->intv[intv].last;
	/* save current counter for next interval */
	ctr->intv[intv].last = ctr->current;
}

static struct timer_list rate_ctr_timer;
static uint64_t timer_ticks;

/* The one-second interval has expired */
static void rate_ctr_group_intv(struct rate_ctr_group *grp)
{
	unsigned int i;

	for (i = 0; i < grp->desc->num_ctr; i++) {
		struct rate_ctr *ctr = &grp->ctr[i];

		interval_expired(ctr, RATE_CTR_INTV_SEC);
		if ((timer_ticks % 60) == 0)
			interval_expired(ctr, RATE_CTR_INTV_MIN);
		if ((timer_ticks % (60*60)) == 0)
			interval_expired(ctr, RATE_CTR_INTV_HOUR);
		if ((timer_ticks % (24*60*60)) == 0)
			interval_expired(ctr, RATE_CTR_INTV_DAY);
	}
}

static void rate_ctr_timer_cb(void *data)
{
	struct rate_ctr_group *ctrg;

	/* Increment number of ticks before we calculate intervals,
	 * as a counter value of 0 would already wrap all counters */
	timer_ticks++;

	llist_for_each_entry(ctrg, &rate_ctr_groups, list)
		rate_ctr_group_intv(ctrg);

	bsc_schedule_timer(&rate_ctr_timer, 1, 0);
}

int rate_ctr_init(void *tall_ctx)
{
	tall_rate_ctr_ctx = tall_ctx;
	rate_ctr_timer.cb = rate_ctr_timer_cb;
	bsc_schedule_timer(&rate_ctr_timer, 1, 0);

	return 0;
}