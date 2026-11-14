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
#include "../mtwn_mt7610_bbp.h"
#include "../mtwn_mt7610_reg.h"
#include "../mtwn_mt7610_mcu.h"
#include "../mtwn_mt7610_phy.h"
#include "../mtwn_mt7610_eeprom.h"
#include "../mtwn_mt7610_eeprom_reg.h" /* XXX for eeprom size */

#include "mtwn_mt7610u_phy.h"

/*
 * These are notionally USB specific versions of these routines.
 * The linux mt76 driver has these split (usb_phy.c versus pci_phy.c.)
 * However, things look eerily similar!  So it's possible they could
 * be unified.
 */

int
mtwn_mt7610u_phy_channel_calibrate(struct mtwn_softc *sc, bool mac_stopped)
{
	MTWN_TODO_PRINTF(sc, "%s: called\n", __func__);
	return (0);
}

int
mtwn_mt7610u_phy_calibrate(struct mtwn_softc *sc)
{
	MTWN_TODO_PRINTF(sc, "%s: called\n", __func__);
	return (0);
}

int
mtwn_mt7610u_set_channel(struct mtwn_softc *sc,
    const struct ieee80211_channel *chan)
{
	MTWN_TODO_PRINTF(sc, "%s: called\n", __func__);
	return (0);
}

