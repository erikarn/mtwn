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
#include "../mtwn_mt7610_bbp.h"
#include "../mtwn_mt7610_reg.h"
#include "../mtwn_mt7610_mcu.h"
#include "../mtwn_mt7610_phy.h"

#include "mtwn_mt7610u_phy.h"

#if 0

/*
 * These are notionally USB specific versions of these routines.
 * The linux mt76 driver has these split (usb_phy.c versus pci_phy.c.)
 * However, things look eerily similar!  So it's possible they could
 * be unified.
 */

static uint32_t ext_cca_chan[4] = {
	[0] = _IEEE80211_SHIFTMASK(0, MT7610_REG_EXT_CCA_CFG_CCA0) |
	      _IEEE80211_SHIFTMASK(1, MT7610_REG_EXT_CCA_CFG_CCA1) |
	      _IEEE80211_SHIFTMASK(2, MT7610_REG_EXT_CCA_CFG_CCA2) |
	      _IEEE80211_SHIFTMASK(3, MT7610_REG_EXT_CCA_CFG_CCA3) |
	      _IEEE80211_SHIFTMASK((1 << 0), MT7610_REG_EXT_CCA_CFG_CCA_MASK),

	[1] = _IEEE80211_SHIFTMASK(1, MT7610_REG_EXT_CCA_CFG_CCA0) |
	      _IEEE80211_SHIFTMASK(0, MT7610_REG_EXT_CCA_CFG_CCA1) |
	      _IEEE80211_SHIFTMASK(2, MT7610_REG_EXT_CCA_CFG_CCA2) |
	      _IEEE80211_SHIFTMASK(3, MT7610_REG_EXT_CCA_CFG_CCA3) |
	      _IEEE80211_SHIFTMASK((1 << 1), MT7610_REG_EXT_CCA_CFG_CCA_MASK),

	[2] = _IEEE80211_SHIFTMASK(2, MT7610_REG_EXT_CCA_CFG_CCA0) |
	      _IEEE80211_SHIFTMASK(3, MT7610_REG_EXT_CCA_CFG_CCA1) |
	      _IEEE80211_SHIFTMASK(1, MT7610_REG_EXT_CCA_CFG_CCA2) |
	      _IEEE80211_SHIFTMASK(0, MT7610_REG_EXT_CCA_CFG_CCA3) |
	      _IEEE80211_SHIFTMASK((1 << 2), MT7610_REG_EXT_CCA_CFG_CCA_MASK),

	[3] = _IEEE80211_SHIFTMASK(3, MT7610_REG_EXT_CCA_CFG_CCA0) |
	      _IEEE80211_SHIFTMASK(2, MT7610_REG_EXT_CCA_CFG_CCA1) |
	      _IEEE80211_SHIFTMASK(1, MT7610_REG_EXT_CCA_CFG_CCA2) |
	      _IEEE80211_SHIFTMASK(0, MT7610_REG_EXT_CCA_CFG_CCA3) |
	      _IEEE80211_SHIFTMASK((1 << 3), MT7610_REG_EXT_CCA_CFG_CCA_MASK)
};
#endif

#if 0
int
mtwn_mt7612u_phy_channel_calibrate(struct mtwn_softc *sc, bool mac_stopped)
{
	const struct ieee80211com *ic = IF_MTWN_SC_TO_NET80211(sc);
	bool is_5ghz;
	int ret = 0;

	MTWN_TODO_PRINTF(sc, "%s: called\n", __func__);

	is_5ghz = !! IEEE80211_IS_CHAN_5GHZ(ic->ic_curchan);

	/* Check if cal is done */

	/* channel_silent */

	/* stop mac if required */

	/* if 5GHz, mcu calibrate MCL_CAL_LC */
	if (is_5ghz) {
		ret = mtwn_mt7612_mcu_calibrate(sc,
		    MT7612_MCU_CALIBRATE_FUNC_LC, 0);
		if (ret != 0)
			goto error;
	}

	/* rest of cal */
	ret = mtwn_mt7612_mcu_calibrate(sc, MT7612_MCU_CALIBRATE_FUNC_TX_LOFT,
	    is_5ghz);
	if (ret != 0)
		goto error;
	ret = mtwn_mt7612_mcu_calibrate(sc, MT7612_MCU_CALIBRATE_FUNC_TXIQ,
	    is_5ghz);
	if (ret != 0)
		goto error;
	ret = mtwn_mt7612_mcu_calibrate(sc, MT7612_MCU_CALIBRATE_FUNC_RXIQC_FI,
	    is_5ghz);
	if (ret != 0)
		goto error;
	ret = mtwn_mt7612_mcu_calibrate(sc,
	    MT7612_MCU_CALIBRATE_FUNC_TEMP_SENSOR, 0);
	if (ret != 0)
		goto error;
	ret = mtwn_mt7612_mcu_calibrate(sc,
	    MT7612_MCU_CALIBRATE_FUNC_TX_SHAPING, 0);
	if (ret != 0)
		goto error;

	/* mac resume if needed */

	/* apply gain adj */

	/* edcca init */

	/* mark channel cal as done */

	return (0);
error:
	return (ret);
}
#endif

#if 0
int
mtwn_mt7610u_phy_calibrate(struct mtwn_softc *sc)
{
	MTWN_TODO_PRINTF(sc, "%s: called\n", __func__);

	/* phy channel calibrate */
	/* tssi compensate */
	/* update channel gain */

	return (0);
}
#endif

#if 0
int
mtwn_mt7612u_set_channel(struct mtwn_softc *sc,
    const struct ieee80211_channel *chan)
{
	MTWN_TODO_PRINTF(sc, "%s: called\n", __func__);
	uint8_t bw, bw_index;
	int ch_group_index;

	/* clear channel cal flag; we're about to change it */

	/*
	 * figure out the bw_index and ch_group_index to index
	 * inside various phy tables
	 *
	 * Just handle 20MHz for now; reject 40/80/160MHz.
	 *
	 * TODO: it would be nice if net80211 had a way to get the channel
	 * width directly versus this mess...
	 */
	if (IEEE80211_IS_CHAN_HT40(chan) || IEEE80211_IS_CHAN_VHT40(chan) ||
	    IEEE80211_IS_CHAN_VHT80(chan)) {
		MTWN_TODO_PRINTF(sc, "%s: >20MHz width not yet supported\n",
		    __func__);
		return (ENXIO);
	}
	bw = 0;
	bw_index = 0;
	ch_group_index = 0;

	/* read rx gain */
	(void) mtwn_mt7612_phy_read_rx_gain(sc, chan);

	/* phy set txpower regs */
	(void) mtwn_mt7612_phy_set_txpower_regs(sc, chan);

	/* configure tx delay */
	(void) mtwn_mt7612_configure_tx_delay(sc, chan, bw);

	/* set tx power */

	/* phy set band */
	/* phy set bw */
	/* rmw MT_EXT_CCA_CFG */
	MTWN_REG_RMW_4(sc, MT7610_REG_EXT_CCA_CFG,
	    (MT7610_REG_EXT_CCA_CFG_CCA0 | MT7610_REG_EXT_CCA_CFG_CCA1 |
	     MT7610_REG_EXT_CCA_CFG_CCA2 | MT7610_REG_EXT_CCA_CFG_CCA3 |
	     MT7610_REG_EXT_CCA_CFG_CCA_MASK),
	    ext_cca_chan[ch_group_index]);

	/* mcu set channel */
	(void) bw; (void) bw_index;

	/* mcu init gain */

	/* enable ldpc rx */

	/* cal_r cal */

	/* rxdcoc calibrate */
	/*
	 * XXX double check in mt76 that the channel here (which in mt76 is
	 * chan->hw_value) matches IEEE channel values.
	 */
	(void) mtwn_mt7612_mcu_calibrate(sc, MT7610_MCU_CALIBRATE_FUNC_RC,
	    chan->ic_ieee);

	/* rx lpf cal */

	/* agc reg programming */

	/* txop ctrl programming */
	/* txo programming */
	/* rxo programming */

	/* If we're scanning then we can leave now */

	/* phy channel calibrate */

	/* init agc gain */

	/* if tssi enabled; do temp / tssi calibration */

	return (0);
}
#endif
