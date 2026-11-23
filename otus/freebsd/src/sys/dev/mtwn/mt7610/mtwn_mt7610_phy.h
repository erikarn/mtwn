/*
 * Copyright (c) 2025, Adrian Chadd <adrian@FreeBSD.org>
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
#ifndef	__MTWN_MT7610_PHY_H__
#define	__MTWN_MT7610_PHY_H__

extern	int mtwn_mt7610_phy_ant_select(struct mtwn_softc *);
extern	int mtwn_mt7610_phy_rf_init(struct mtwn_softc *);
extern	int mtwn_mt7610_phy_set_rxpath(struct mtwn_softc *);
extern	int mtwn_mt7610_phy_set_txdac(struct mtwn_softc *);
extern	int mtwn_mt7610_phy_init(struct mtwn_softc *);
extern	int mtwn_mt7610_phy_read_rx_gain(struct mtwn_softc *,
	    const struct ieee80211_channel *);
extern	int mtwn_mt7612_phy_read_rx_gain(struct mtwn_softc *,
	    const struct ieee80211_channel *);
extern	int mtwn_mt7612_phy_set_txpower_regs(struct mtwn_softc *,
	    const struct ieee80211_channel *);
extern	int mtwn_mt7612_configure_tx_delay(struct mtwn_softc *,
    const struct ieee80211_channel *, uint8_t);
extern	int mtwn_mt7610_phy_set_bw(struct mtwn_softc *, int, uint8_t);
extern	int mtwn_mt7610_phy_bbp_set_bw(struct mtwn_softc *, int);
extern	int mtwn_mt7610_phy_set_band(struct mtwn_softc *,
	    const struct ieee80211_channel *);
extern	int mtwn_mt7610_phy_set_rf_band(struct mtwn_softc *,
	    const struct ieee80211_channel *);
extern	int mtwn_mt7610_phy_set_chan_rf_params(struct mtwn_softc *,
	    const struct ieee80211_channel *, uint16_t);
extern	int mtwn_mt7610_phy_get_txpower(struct mtwn_softc *,
    const struct ieee80211_channel *);
extern	int mtwn_mt7610_phy_write_txpower(struct mtwn_softc *, int, int);

int	mtwn_mt7610_phy_channel_calibrate(struct mtwn_softc *sc,
	    bool power_on);
int	mtwn_mt7610_set_channel(struct mtwn_softc *sc,
	    const struct ieee80211_channel *chan);

#endif	/* __MTWN_MT7610_PHY_H__ */
