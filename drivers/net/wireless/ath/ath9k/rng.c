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

static int ath9k_rng_read(struct hwrng *rng, void *buf, size_t max, bool wait)
{
	struct ath_softc *sc = container_of(rng, struct ath_softc, rng_ops);
	u32 fail_stats = 0, word;
	int bytes_read = 0;

	for (;;) {
		if (max & ~3UL)
			bytes_read = ath9k_rng_data_read(sc, buf, max >> 2);
		if ((max & 3UL) && ath9k_rng_data_read(sc, &word, 1)) {
			memcpy(buf + bytes_read, &word, max & 3UL);
			bytes_read += max & 3UL;
			memzero_explicit(&word, sizeof(word));
		}
		if (!wait || !max || likely(bytes_read) || fail_stats > 110)
			break;

		if (hwrng_msleep(rng, ath9k_rng_delay_get(++fail_stats)))
			break;
	}

	if (wait && !bytes_read && max)
		bytes_read = -EIO;
	return bytes_read;
}

void ath9k_rng_start(struct ath_softc *sc)
{
	static atomic_t serial = ATOMIC_INIT(0);

	if (sc->rng_ops.read)
		return;

	snprintf(sc->rng_name, sizeof(sc->rng_name), "ath9k_%u",
		 (atomic_inc_return(&serial) - 1) & U16_MAX);
	sc->rng_ops.name = sc->rng_name;
	sc->rng_ops.read = ath9k_rng_read;
	sc->rng_ops.quality = 320;

	if (devm_hwrng_register(sc->dev, &sc->rng_ops))
		sc->rng_ops.read = NULL;
}

void ath9k_rng_stop(struct ath_softc *sc)
{
	if (sc->rng_ops.read) {
		devm_hwrng_unregister(sc->dev, &sc->rng_ops);
		sc->rng_ops.read = NULL;
	}
}
