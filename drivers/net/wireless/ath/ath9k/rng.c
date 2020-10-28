/*
 * Copyright (c) 2015 Qualcomm Atheros, Inc.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <linux/hw_random.h>
#include <linux/kthread.h>

#include "ath9k.h"
#include "hw.h"

#define ATH9K_RNG_BUF_SIZE	320
#define ATH9K_RNG_ENTROPY(x)	(((x) * 8 * 10) >> 5) /* quality: 10/32 */

static DECLARE_WAIT_QUEUE_HEAD(rng_queue);

static inline int ath9k_hw_get_adc_entropy(struct ath_hw *ah,
	u32 *buf, const u32 buf_size, u32 *rng_last)
{
	return ath9k_hw_ops(ah)->get_adc_entropy(ah, buf, buf_size, rng_last);
}

static int ath9k_rng_data_read(struct ath_softc *sc, u32 *buf, u32 buf_size)
{
	struct ath_hw *ah = sc->sc_ah;
	int bytes_read;

	ath9k_ps_wakeup(sc);

	bytes_read = ath9k_hw_get_adc_entropy(ah, buf, buf_size, &sc->rng_last);

	ath9k_ps_restore(sc);

	return bytes_read;
}

static u32 ath9k_rng_delay_get(u32 fail_stats)
{
	u32 delay;

	if (fail_stats < 100)
		delay = 10;
	else if (fail_stats < 105)
		delay = 1000;
	else
		delay = 10000;

	return delay;
}

static int ath9k_rng_kthread(void *data)
{
	int bytes_read;
	struct ath_softc *sc = data;
	u32 *rng_buf;
	u32 delay, fail_stats = 0;

	rng_buf = kmalloc_array(ATH9K_RNG_BUF_SIZE, sizeof(u32), GFP_KERNEL);
	if (!rng_buf)
		goto out;

	while (!kthread_should_stop()) {
		bytes_read = ath9k_rng_data_read(sc, rng_buf,
						 ATH9K_RNG_BUF_SIZE);
		if (unlikely(!bytes_read)) {
			delay = ath9k_rng_delay_get(++fail_stats);
			wait_event_interruptible_timeout(rng_queue,
							 kthread_should_stop(),
							 msecs_to_jiffies(delay));
			continue;
		}

		fail_stats = 0;

		/* sleep until entropy bits under write_wakeup_threshold */
		add_hwgenerator_randomness((void *)rng_buf, bytes_read,
					   ATH9K_RNG_ENTROPY(bytes_read));
	}

	kfree(rng_buf);
out:
	sc->rng_task = NULL;

	return 0;
}

void ath9k_rng_start(struct ath_softc *sc)
{
	if (sc->rng_task)
		return;

	sc->rng_task = kthread_run(ath9k_rng_kthread, sc, "ath9k-hwrng");
	if (IS_ERR(sc->rng_task))
		sc->rng_task = NULL;
}

void ath9k_rng_stop(struct ath_softc *sc)
{
	if (sc->rng_task) {
		kthread_stop(sc->rng_task);
		sc->rng_task = NULL;
	}
}
