/*
 * Copyright 2026 Adrian Chadd <adrian@FreeBSD.org>.
 *
 * Permission to use, copy, modify, and distribute this software for any
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

#include "opt_wlan.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/endian.h>
#include <sys/eventhandler.h>
#include <sys/firmware.h>
#include <sys/kdb.h>
#include <sys/kernel.h>
#include <sys/linker.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/module.h>
#include <sys/mutex.h>
#include <sys/socket.h>
#include <sys/sockio.h>
#include <sys/sysctl.h>

#include <net/bpf.h>
#include <net/ethernet.h>
#include <net/if.h>
#include <net/if_arp.h>
#include <net/if_dl.h>
#include <net/if_media.h>
#include <net/if_types.h>
#include <net/if_var.h>
#include <net80211/ieee80211_var.h>
#include <net80211/ieee80211_radiotap.h>
#include <net80211/ieee80211_ratectl.h>
#include <net80211/ieee80211_regdomain.h>
#ifdef	IEEE80211_SUPPORT_SUPERG
#include <net80211/ieee80211_superg.h>
#endif
#include <netinet/if_ether.h>
#include <netinet/in.h>
#include <netinet/in_systm.h>
#include <netinet/in_var.h>
#include <netinet/ip.h>

#include "../../if_mtwn_var.h"
#include "../../if_mtwn_debug.h"

#include "../mtwn_mt7610_var.h"
#include "../mtwn_mt7610_init.h"
#include "../mtwn_mt7610_mac.h"
#include "../mtwn_mt7610_mcu.h"
#include "../mtwn_mt7610_mcu_reg.h"
#include "../mtwn_mt7610_reg.h"
#include "../mtwn_mt7610_mcu.h"
#include "../mtwn_mt7610_dma.h"

#include "mtwn_mt7610u_mac.h"

/**
 * @brief Start the MAC.
 *
 * Based on mt76 mt76x02u_mac_start() .
 */
int
mtwn_mt7610u_mac_start(struct mtwn_softc *sc)
{
	MTWN_TODO_PRINTF(sc, "%s: mac_reset_counters\n", __func__);

	MTWN_REG_WRITE_4(sc, MT7610_REG_MAC_SYS_CTRL,
	    MT7610_REG_MAC_SYS_CTRL_ENABLE_TX);

	if (! mtwn_mt7610_wait_for_wpdma(sc, 200000))
		return (ETIMEDOUT);

	/* XXX TODO: turn this into a method? To mirror the read rxfilter method? */
	MTWN_REG_WRITE_4(sc, MT7610_REG_RX_FILTER_CFG,
	    sc->mac_state.sc_rx_filter);

	MTWN_REG_WRITE_4(sc, MT7610_REG_MAC_SYS_CTRL,
	    MT7610_REG_MAC_SYS_CTRL_ENABLE_TX |
	    MT7610_REG_MAC_SYS_CTRL_ENABLE_RX);

	if (! mtwn_mt7610_wait_for_wpdma(sc, 50))
		return (ETIMEDOUT);

	return (0);
}
