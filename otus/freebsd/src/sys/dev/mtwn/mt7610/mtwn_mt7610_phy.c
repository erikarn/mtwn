/*-
 * Copyright 2025 Adrian Chadd <adrian@FreeBSD.org>.
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

#include "../if_mtwn_var.h"
#include "../if_mtwn_debug.h"
#include "../if_mtwn_util.h"

#include "mtwn_mt7610_reg.h"
#include "mtwn_mt7610_mac.h"
#include "mtwn_mt7610_mcu_reg.h" /* for MT7610_MCU_MEMMAP_WLAN */
#include "mtwn_mt7610_eeprom_reg.h" /* for EEPROM defs */
#include "mtwn_mt7610_eeprom.h"

#include "mtwn_mt7610_var.h"
#include "mtwn_mt7610_phy_reg.h"
#include "mtwn_mt7610_phy.h"
#include "mtwn_mt7610_mcu.h"
#include "mtwn_mt7610_bbp.h"

#include "mtwn_mt7610_phy_initvals.h"

/**
 * @brief Initialise the initial antenna configuration.
 */
int
mtwn_mt7610_phy_ant_select(struct mtwn_softc *sc)
{
	uint16_t ee_ant, ee_cfg1, nic_conf2;
	uint32_t wlan, coex3;
	bool ant_div;

	MTWN_LOCK_ASSERT(sc, MA_OWNED);

	ee_ant = MTWN_EEPROM_READ_2(sc, MT7610_EEPROM_ANTENNA);
	ee_cfg1 = MTWN_EEPROM_READ_2(sc, MT7610_EEPROM_CFG1_INIT);
	nic_conf2 = MTWN_EEPROM_READ_2(sc, MT7610_EEPROM_NIC_CONF_2);

	wlan = MTWN_REG_READ_4(sc, MT76_REG_WLAN_FUN_CTRL);
	coex3 = MTWN_REG_READ_4(sc, MT7610_REG_COEXCFG3);

	MTWN_TODO_PRINTF(sc,
	    "%s: TODO: does the reference driver define these bit fields?\n",
	    __func__);

	/* Mask out bits that we will configure */
	ee_ant &= ~0x00005000;	/* Bit 12, bit 14 */
	wlan &= ~(MT76_REG_WLAN_FUN_CTRL_FRC_WL_ANT_SEL |
	    MT76_REG_WLAN_FUN_CTRL_INV_ANT_SEL);
	coex3 &= 0x00000003c; /* bits 2..5 */

	if (ee_ant & MT7610_EEPROM_ANTENNA_DUAL) {
		/* Dual antenna */
		ant_div = !(nic_conf2 & MT7610_EEPROM_NIC_CONF_2_ANT_OPT) &&
		    (nic_conf2 & MT7610_EEPROM_NIC_CONF_2_ANT_DIV);
		if (ant_div)
			ee_ant |= (1 << 12);
		else
			coex3 |= (1 << 4);
		coex3 |= (1 << 3);
		if (sc->sc_phy_cap.sb.has_2ghz)
			wlan |= MT76_REG_WLAN_FUN_CTRL_INV_ANT_SEL;
	} else {
		/* Single antenna */
		if (sc->sc_phy_cap.sb.has_5ghz)
			coex3 |= (1 << 3) | (1 << 4);
		else {
			wlan |= MT76_REG_WLAN_FUN_CTRL_INV_ANT_SEL;
			coex3 |= (1 << 1);
		}
	}

	if (MTWN_MT7610_CHIP_IS_MT7630(sc))
		ee_ant |= (1 << 14) | (1 << 11);

	MTWN_REG_WRITE_4(sc, MT76_REG_WLAN_FUN_CTRL, wlan);
	MTWN_REG_RMW_4(sc, MT76_REG_CMB_CTRL, 0x0000ffff, ee_ant);
	MTWN_REG_RMW_4(sc, MT7610_REG_CSR_EE_CFG1, 0x0000ffff, ee_cfg1);
	MTWN_REG_CLEAR_4(sc, MT7610_REG_COEXCFG0, (1 << 2));
	MTWN_REG_WRITE_4(sc, MT7610_REG_COEXCFG3, coex3);

	return (0);
}

static int
mtwn_mt7610_phy_rf_patch_reg_array(struct mtwn_softc *sc,
    const struct mtwn_reg_pair *rp, int n)
{
	int i;

	for (i = 0; i < n; i++) {
		uint32_t reg, val;
		int ret;

		reg = rp[i].reg;
		val = rp[i].val;

		switch (reg) {
		case MT7610_REG_RF(0, 3):
			if (MTWN_IS_BUS_MMIO(sc)) {
				if (MTWN_MT7610_CHIP_IS_MT7630(sc))
					val = 0x70;
				else
					val = 0x63;
			} else {
				val = 0x73;
			}
			break;
		case MT7610_REG_RF(0, 21):
			if (MTWN_MT7610_CHIP_IS_MT7610E(sc))
				val = 0x10;
			else
				val = 0x12;
		case MT7610_REG_RF(5, 2):
			if (MTWN_MT7610_CHIP_IS_MT7630(sc))
				val = 0x1d;
			else if (MTWN_MT7610_CHIP_IS_MT7610E(sc))
				val = 0x00;
			else
				val = 0x0c;
		default:
			break;
		}

		ret = MTWN_RF_REG_WRITE_4(sc, reg, val);
		if (ret != 0)
			return (ret);
	}
	return (0);
}

static int
mtwn_mt760_phy_rf_init_switch_table(struct mtwn_softc *sc)
{
	int ret = 0;
	int i;

	for (i = 0; i < nitems(mtwn_mt7610_rf_bw_switch_tab); i++) {
		const struct mtwn_mt7610_rf_switch_item *e =
		    &mtwn_mt7610_rf_bw_switch_tab[i];

		ret = 0;
		if (e->bw_band == MTWN_MT7610_PHY_RF_BW_20)
			ret = MTWN_RF_REG_WRITE_4(sc, e->rf_bank_reg, e->value);
		else if ((e->bw_band &
		    (MTWN_MT7610_PHY_RF_BW_20 | MTWN_MT7610_PHY_RF_G_BAND)) ==
		    (MTWN_MT7610_PHY_RF_BW_20 | MTWN_MT7610_PHY_RF_G_BAND))
			ret = MTWN_RF_REG_WRITE_4(sc, e->rf_bank_reg, e->value);

		if (ret != 0)
			goto finish;
	}

	for (i = 0; i < nitems(mtwn_mt7610_rf_band_switch_tab); i++) {
		const struct mtwn_mt7610_rf_switch_item *e =
		    &mtwn_mt7610_rf_band_switch_tab[i];

		ret = 0;
		if (e->bw_band & MTWN_MT7610_PHY_RF_G_BAND)
			ret = MTWN_RF_REG_WRITE_4(sc, e->rf_bank_reg, e->value);
		if (ret != 0)
			goto finish;
	}
finish:
	return (ret);
}

int
mtwn_mt7610_phy_rf_init(struct mtwn_softc *sc)
{
	const struct mtwn_mt7610_chip_priv *psc = MTWN_MT7610_CHIP_SOFTC(sc);
	int ret;

	MTWN_LOCK_ASSERT(sc, MA_OWNED);

	ret = mtwn_mt7610_phy_rf_patch_reg_array(sc,
	    mtwn_mt7610_rf_central_tab, nitems(mtwn_mt7610_rf_central_tab));
	if (ret != 0) {
		MTWN_ERR_PRINTF(sc,
		    "%s: failed to write rf_central_tab (err %d)\n",
		    __func__, ret);
		return (ret);
	}

	ret = mtwn_mt7610_phy_rf_patch_reg_array(sc,
	    mtwn_mt761x0_rf_2g_channel_0_tab,
	    nitems(mtwn_mt761x0_rf_2g_channel_0_tab));
	if (ret != 0) {
		MTWN_ERR_PRINTF(sc,
		    "%s: failed to write 2g_channel_0_tab (err %d)\n",
		    __func__, ret);
		return (ret);
	}

	/* 5g_channel_0_tab */
	ret = MTWN_RF_REG_PAIR_WRITE_4(sc, mtwn_mt7610_rf_5g_channel_0_tab,
	    nitems(mtwn_mt7610_rf_5g_channel_0_tab));
	if (ret != 0) {
		MTWN_ERR_PRINTF(sc,
		    "%s: failed to write 5g_channel_0_tab (err %d)\n",
		    __func__, ret);
		return (ret);
	}

	/* vga_channel_0_tab */
	ret = MTWN_RF_REG_PAIR_WRITE_4(sc, mtwn_mt7610_rf_vga_channel_0_tab,
	    nitems(mtwn_mt7610_rf_vga_channel_0_tab));
	if (ret != 0) {
		MTWN_ERR_PRINTF(sc,
		    "%s: failed to write vga_channel_0_tab (err %d)\n",
		    __func__, ret);
		return (ret);
	}

	ret = mtwn_mt760_phy_rf_init_switch_table(sc);
	if (ret != 0) {
		MTWN_ERR_PRINTF(sc,
		    "%s: failed to write switch table (err %d)\n",
		    __func__, ret);
		return (ret);
	}

	/* frequency calibration */
	MTWN_DEBUG_PRINTF(sc, "%s: CAL before: 0x%08x\n",
	    __func__, MTWN_RF_REG_READ_4(sc, MT7610_REG_RF(0, 22)));
	MTWN_RF_REG_WRITE_4(sc, MT7610_REG_RF(0, 22),
	    MIN((uint8_t) psc->rx_freq_cal.freq_offset, 0xbf));
	MTWN_RF_REG_READ_4(sc, MT7610_REG_RF(0, 22));

	MTWN_DEBUG_PRINTF(sc, "%s: CAL after: 0x%08x\n",
	    __func__, MTWN_RF_REG_READ_4(sc, MT7610_REG_RF(0, 22)));

	/* DAC reset */
	ret = MTWN_RF_REG_SET_4(sc, MT7610_REG_RF(0, 74), 0x80);
	ret = MTWN_RF_REG_CLEAR_4(sc, MT7610_REG_RF(0, 74), 0x80);
	ret = MTWN_RF_REG_SET_4(sc, MT7610_REG_RF(0, 74), 0x80);

	/* VCO calibration */
	ret = MTWN_RF_REG_SET_4(sc, MT7610_REG_RF(0, 4), 0x80);

	return (0);
}

/*
 * Note: mt76x02_phy_set_rxpath() in mt76
 */
int
mtwn_mt7610_phy_set_rxpath(struct mtwn_softc *sc)
{
	uint32_t val;

	MTWN_LOCK_ASSERT(sc, MA_OWNED);

	val = MTWN_REG_READ_4(sc, MT7610_REG_BBP(AGC, 0));
	val &= ~ (1 << 4);

	/*
	 * Note: this code in mt76 is run after the
	 * init code establishes which chipset and thus
	 * how many tx/rx chains there are.
	 */

#if 0
	MTWN_DEBUG_PRINTF(sc, "%s: streams tx=%d rx=%d\n",
	    __func__,
	    sc->sc_phy_cap.ss.num_tx_streams,
	    sc->sc_phy_cap.ss.num_rx_streams);
#endif

	switch (sc->sc_phy_cap.ss.num_rx_streams) {
	case 2:
		val |= (1 << 3);
		break;
	default:
		val &= ~(1 << 3);
		break;
	}

	MTWN_REG_WRITE_4(sc, MT7610_REG_BBP(AGC, 0), val);
	/* TODO: ensure MTWN_REG_READ_4 has a barrier, or we add a barrier here */
	val = MTWN_REG_READ_4(sc, MT7610_REG_BBP(AGC, 0));

	return (0);
}

int
mtwn_mt7610_phy_set_txdac(struct mtwn_softc *sc)
{
	MTWN_LOCK_ASSERT(sc, MA_OWNED);

	switch (sc->sc_phy_cap.ss.num_tx_streams) {
	case 2:
		MTWN_REG_SET_4(sc, MT7610_REG_BBP(TXBE, 5), 0x3);
		break;
	default:
		MTWN_REG_CLEAR_4(sc, MT7610_REG_BBP(TXBE, 5), 0x3);
		break;
	}

	return (0);
}

int
mtwn_mt7610_phy_init(struct mtwn_softc *sc)
{
	int ret;

	MTWN_LOCK_ASSERT(sc, MA_OWNED);

	ret = mtwn_mt7610_phy_ant_select(sc);
	if (ret != 0) {
		MTWN_ERR_PRINTF(sc, "%s: phy_ant_select failed (err %d)\n",
		    __func__, ret);
		return (ret);
	}

	ret = mtwn_mt7610_phy_rf_init(sc);
	if (ret != 0) {
		MTWN_ERR_PRINTF(sc, "%s: phy_rf_init failed (err %d)\n",
		    __func__, ret);
		return (ret);
	}

	ret = mtwn_mt7610_phy_set_rxpath(sc);
	if (ret != 0) {
		MTWN_ERR_PRINTF(sc, "%s: phy_set_rxpath failed (err %d)\n",
		    __func__, ret);
		return (ret);
	}

	ret = mtwn_mt7610_phy_set_txdac(sc);
	if (ret != 0) {
		MTWN_ERR_PRINTF(sc, "%s: phy_set_txdac failed (err %d)\n",
		    __func__, ret);
		return (ret);
	}

	return (0);
}

/**
 * @brief Update the RSSI value in the RX calibration field.
 *
 * This is based on mt76x2_set_rssi_offset().
 */
static void
mtwn_mt7612_set_rssi_offset(struct mtwn_softc *sc, int chain, uint8_t val)
{
	struct mtwn_mt7610_chip_priv *psc = MTWN_MT7610_CHIP_SOFTC(sc);

	if (mtwn_mt7610_eeprom_field_valid_1(sc, val))
		psc->rx_freq_cal.rssi_offset[chain] =
		    mtwn_mt7610_eeprom_field_sign_extend_optional(sc, val, 7);
	else
		psc->rx_freq_cal.rssi_offset[chain] = 0;
}

/**
 * @brief Update the RX gain information for the given channel from EEPROM
 *
 * Read the EEPROM RX gain and program in the RX gain, RSSI offset and
 * LNA settings for the provided channel.
 *
 * This is based off of mt76x2_read_rx_gain() in the mt76 driver.
 *
 * XXX TODO: this may be the wrong one, I need to port the mt76x0
 * one, not the mt76x2 one!
 */
int
mtwn_mt7612_phy_read_rx_gain(struct mtwn_softc *sc,
    const struct ieee80211_channel *c)
{
	struct mtwn_mt7610_chip_priv *psc = MTWN_MT7610_CHIP_SOFTC(sc);
	bool use_lna;
	uint16_t val;
	uint8_t lna = 0;
	int8_t lna_5g[3], lna_2g;

	MTWN_TODO_PRINTF(sc, "%s: TODO!\n", __func__);

	/* Read 2g or 5x RX gain */
	if (IEEE80211_IS_CHAN_2GHZ(c))
		val = MTWN_EEPROM_READ_2(sc, MT7610_EEPROM_RF_2G_RX_HIGH_GAIN);
	else
		val = mtwn_mt7612_eeprom_get_5g_rx_gain(sc, c);

	/* set rx gain group */
	/* TODO: Note: this only uses the low 8 bits of the provided val */
	mtwn_mt7610_eeprom_get_rx_gain_group(sc, val,
	    psc->rx_freq_cal.high_gain);

	/* get rx gain */
	mtwn_mt7610_eeprom_get_rx_gain(sc, c, &val, &lna_2g, lna_5g);

	/* set rssi offset */
	mtwn_mt7612_set_rssi_offset(sc, 0, val);
	mtwn_mt7612_set_rssi_offset(sc, 1, val >> 8);

	/* populate MCU gain based on LNA configs */
	psc->rx_freq_cal.mcu_gain = (lna_2g & 0xff);
	psc->rx_freq_cal.mcu_gain |= (lna_5g[0] & 0xff) << 8;
	psc->rx_freq_cal.mcu_gain |= (lna_5g[1] & 0xff) << 16;
	psc->rx_freq_cal.mcu_gain |= (lna_5g[2] & 0xff) << 24;

	/* Get LNA gain if required */
	val = MTWN_EEPROM_READ_2(sc, MT7610_EEPROM_NIC_CONF_1);
	if (IEEE80211_IS_CHAN_2GHZ(c)) {
		use_lna = ! (val & MT7610_EEPROM_NIC_CONF_1_LNA_EXT_2G);
	} else
		use_lna = ! (val & MT7610_EEPROM_NIC_CONF_1_LNA_EXT_5G);

	if (use_lna)
		lna = mtwn_mt7610_get_lna_gain(sc, &lna_2g, lna_5g, c);

	psc->rx_freq_cal.lna_gain =
	    mtwn_mt7610_eeprom_field_sign_extend(sc, lna, 8);

	return (0);
}

/**
 * @brief Read the RX gain table from EEPROM and calculate offsets.
 *
 * This is based on mt76x0_read_rx_gain() .
 */
int
mtwn_mt7610_phy_read_rx_gain(struct mtwn_softc *sc,
    const struct ieee80211_channel *c)
{
	struct mtwn_mt7610_chip_priv *psc = MTWN_MT7610_CHIP_SOFTC(sc);
	int i;
	uint16_t rssi_offset;
	int8_t val, lna_5g[3], lna_2g;

	/* get rx gain */
	mtwn_mt7610_eeprom_get_rx_gain(sc, c, &rssi_offset, &lna_2g, lna_5g);
	psc->rx_freq_cal.lna_gain = mtwn_mt7610_get_lna_gain(sc, &lna_2g,
	    lna_5g, c);


	/* offset and clamp */
	for (i = 0; i < nitems(psc->rx_freq_cal.rssi_offset); i++) {
		val = rssi_offset >> (8 * i);
		if (val < -10 || val > 10)
			val = 0;
		psc->rx_freq_cal.rssi_offset[i] = val;
	}

	return (0);
}

/**
 * @brief Set the tx power / PA configuration
 *
 * This is based on mt76x2_phy_set_txpower_regs().
 *
 * XXX TODO: this may be the wrong one, I need to port the mt76x0
 * one, not the mt76x2 one!
 */
int
mtwn_mt7612_phy_set_txpower_regs(struct mtwn_softc *sc,
    const struct ieee80211_channel *c)
{
	uint32_t pa_mode[2];
	uint32_t pa_mode_adj;

	if (IEEE80211_IS_CHAN_2GHZ(c)) {
		pa_mode[0] = 0x010055ff;
		pa_mode[1] = 0x00550055;

		MTWN_REG_WRITE_4(sc, MT7610_REG_TX_ALC_CFG_2, 0x35160a00);
		MTWN_REG_WRITE_4(sc, MT7610_REG_TX_ALC_CFG_3, 0x35160a06);

		if (mtwn_mt7610_ext_pa_enabled(sc, c)) {
			MTWN_REG_WRITE_4(sc, MT7610_REG_RF_PA_MODE_ADJ0,
			    0x0000ec00);
			MTWN_REG_WRITE_4(sc, MT7610_REG_RF_PA_MODE_ADJ1,
			    0x0000ec00);
		} else {
			MTWN_REG_WRITE_4(sc, MT7610_REG_RF_PA_MODE_ADJ0,
			    0xf4000200);
			MTWN_REG_WRITE_4(sc, MT7610_REG_RF_PA_MODE_ADJ1,
			    0xfa000200);
		}
	} else {
		pa_mode[0] = 0x0000ffff;
		pa_mode[1] = 0x00ff00ff;

		if (mtwn_mt7610_ext_pa_enabled(sc, c))
			pa_mode_adj = 0x0f000000;
		else
			pa_mode_adj = 0;

		MTWN_REG_WRITE_4(sc, MT7610_REG_RF_PA_MODE_ADJ0, pa_mode_adj);
		MTWN_REG_WRITE_4(sc, MT7610_REG_RF_PA_MODE_ADJ1, pa_mode_adj);
	}

	/* Write pa mode */
	MTWN_REG_WRITE_4(sc, MT7610_REG_BB_PA_MODE_CFG0, pa_mode[0]);
	MTWN_REG_WRITE_4(sc, MT7610_REG_BB_PA_MODE_CFG1, pa_mode[1]);
	MTWN_REG_WRITE_4(sc, MT7610_REG_RF_PA_MODE_CFG0, pa_mode[0]);
	MTWN_REG_WRITE_4(sc, MT7610_REG_RF_PA_MODE_CFG1, pa_mode[1]);

	if (mtwn_mt7610_ext_pa_enabled(sc, c)) {
		uint32_t val;

		if (IEEE80211_IS_CHAN_2GHZ(c))
			val = 0x3c3c023c;
		else
			val = 0x363c023c;

		MTWN_REG_WRITE_4(sc, MT7610_REG_TX0_RF_GAIN_CORR, val);
		MTWN_REG_WRITE_4(sc, MT7610_REG_TX1_RF_GAIN_CORR, val);
		MTWN_REG_WRITE_4(sc, MT7610_REG_TX_ALC_CFG_4, 0x00001818);
	} else {

		if (IEEE80211_IS_CHAN_2GHZ(c)) {
			uint32_t val = 0x0f3c3c3c;

			MTWN_REG_WRITE_4(sc, MT7610_REG_TX0_RF_GAIN_CORR, val);
			MTWN_REG_WRITE_4(sc, MT7610_REG_TX1_RF_GAIN_CORR, val);
			MTWN_REG_WRITE_4(sc, MT7610_REG_TX_ALC_CFG_4,
			    0x00000606);
		} else {
			MTWN_REG_WRITE_4(sc, MT7610_REG_TX0_RF_GAIN_CORR,
			    0x383c023c);
			MTWN_REG_WRITE_4(sc, MT7610_REG_TX1_RF_GAIN_CORR,
			    0x24282e28);
			MTWN_REG_WRITE_4(sc, MT7610_REG_TX_ALC_CFG_4, 0);
		}
	}

	return (0);
}

/**
 * @brief Configure TX delay
 *
 * This configures both RX/TX delay and SIFS timing.
 *
 * @param bw 0 - 20MHz; 1 - 40MHz; 2 - 80MHz
 *
 * This is based on mt76x2_configure_tx_delay()
 *
 * XXX TODO: this may be the wrong one, I need to port the mt76x0
 * one, not the mt76x2 one!
 */
int
mtwn_mt7612_configure_tx_delay(struct mtwn_softc *sc,
    const struct ieee80211_channel *c, uint8_t bw)
{
	uint32_t cfg_0, cfg_1;

	if (mtwn_mt7610_ext_pa_enabled(sc, c)) {
		cfg_0 = bw ? 0x000b0c01 : 0x00101101;
		cfg_1 = 0x00011414;
	} else {
		cfg_0 = bw ? 0x000b0b01 : 0x00101001;
		cfg_1 = 0x00021414;
	}

	MTWN_REG_WRITE_4(sc, MT7610_REG_TX_SW_CFG0, cfg_0);
	MTWN_REG_WRITE_4(sc, MT7610_REG_TX_SW_CFG1, cfg_1);

	MTWN_REG_RMW_4(sc, MT7610_REG_XIFS_TIME_CFG,
	    MT7610_REG_XIFS_TIME_CFG_OFDM_SIFS, 15);

	return (0);
}

/**
 * @brief Calculate the TX power mask for the given four TX power targets.
 *
 * This is based on mt76x02_tx_power_mask() .
 */
static uint32_t
mtwn_mt7610_calc_tx_power_mask(uint8_t v1, uint8_t v2, uint8_t v3, uint8_t v4)
{
	uint32_t val = 0;

	val |= (v1 & 0x3f) << 0;
	val |= (v2 & 0x3f) << 8;
	val |= (v3 & 0x3f) << 16;
	val |= (v4 & 0x3f) << 24;

	return (val);
}

/**
 * @brief Write the TX power array into the PHY.
 *
 * This writes the rate_power, txp_0, txp_1 values into the
 * PHY.
 *
 * This is based on mt76x02_phy_set_txpower() .
 */
int
mtwn_mt7610_phy_write_txpower(struct mtwn_softc *sc, int txp_0, int txp_1)
{
	const struct mtwn_mt7610_chip_priv *psc = MTWN_MT7610_CHIP_SOFTC(sc);
	const struct mtwn_mt7610_rate_power *t = &psc->rate_power;

	MTWN_REG_RMW_4(sc, MT7610_REG_TX_ALC_CFG_0,
	    MT7610_REG_TX_ALC_CFG_0_CH_INIT_0, txp_0);
	MTWN_REG_RMW_4(sc, MT7610_REG_TX_ALC_CFG_0,
	    MT7610_REG_TX_ALC_CFG_0_CH_INIT_1, txp_1);

	MTWN_REG_WRITE_4(sc, MT7610_REG_TX_PWR_CFG_0,
	    mtwn_mt7610_calc_tx_power_mask(t->cck[0], t->cck[2], t->ofdm[0],
	    t->ofdm[2]));

	MTWN_REG_WRITE_4(sc, MT7610_REG_TX_PWR_CFG_1,
	    mtwn_mt7610_calc_tx_power_mask(t->ofdm[4], t->ofdm[6], t->ht[0],
	    t->ht[2]));

	MTWN_REG_WRITE_4(sc, MT7610_REG_TX_PWR_CFG_2,
	    mtwn_mt7610_calc_tx_power_mask(t->ht[4], t->ht[6], t->ht[8],
	    t->ht[10]));

	MTWN_REG_WRITE_4(sc, MT7610_REG_TX_PWR_CFG_3,
	    mtwn_mt7610_calc_tx_power_mask(t->ht[12], t->ht[14], t->ht[0],
	    t->ht[2]));

	MTWN_REG_WRITE_4(sc, MT7610_REG_TX_PWR_CFG_4,
	    mtwn_mt7610_calc_tx_power_mask(t->ht[4], t->ht[6], 0, 0));

	MTWN_REG_WRITE_4(sc, MT7610_REG_TX_PWR_CFG_7,
	    mtwn_mt7610_calc_tx_power_mask(t->ofdm[7], t->vht[0], t->ht[7],
	    t->vht[1]));

	MTWN_REG_WRITE_4(sc, MT7610_REG_TX_PWR_CFG_8,
	    mtwn_mt7610_calc_tx_power_mask(t->ht[14], 0, t->vht[0],
	    t->vht[1]));

	MTWN_REG_WRITE_4(sc, MT7610_REG_TX_PWR_CFG_9,
	    mtwn_mt7610_calc_tx_power_mask(t->ht[7], 0, t->vht[0],
	    t->vht[1]));

	return (0);
}

/**
 * @brief Calculate the TX power values to program in via set_txpower
 *
 * This is based on mt76x0_phy_set_txpower() .
 */
int
mtwn_mt7610_phy_get_txpower(struct mtwn_softc *sc,
    const struct ieee80211_channel *c)
{

	/* Get power info per rate */
	/* Get power info from eeprom */

	/* Add power rate offset */

	/* Limit rate power */
	/* Get global max rate power */
	/* Save target power */

	return (0);
}

int
mtwn_mt7610_phy_set_bw(struct mtwn_softc *sc, int bw, uint8_t ctrl)
{
	int agc_val, core_val;

	switch (bw) {
	case 80:
		core_val = 3;
		agc_val = 7;
		break;
	case 40:
		core_val = 2;
		agc_val = 3;
		break;
	default:
		core_val = 0;
		agc_val = 1;
		break;
	}

	MTWN_REG_RMW_4(sc, MT7610_REG_BBP(CORE, 1), MT7610_REG_BBP_CORE_R1_BW,
	    core_val);
	MTWN_REG_RMW_4(sc, MT7610_REG_BBP(AGC, 0), MT7610_REG_BBP_AGC_R0_BW,
	    agc_val);
	MTWN_REG_RMW_4(sc, MT7610_REG_BBP(AGC, 0),
	    MT7610_REG_BBP_AGC_R0_CTRL_CHAN, ctrl);
	MTWN_REG_RMW_4(sc, MT7610_REG_BBP(TXBE, 0),
	    MT7610_REG_BBP_TXBE_R0_CTRL_CHAN, ctrl);

	return (0);
}

/*
 * based on mt76x0_phy_bbp_set_bw()
 */
int
mtwn_mt7610_phy_bbp_set_bw(struct mtwn_softc *sc, int chw)
{
	int bw, ret;

	switch (chw) {
	case 10:
		bw = 4;
		break;
	case 20:
		bw = 0;
		break;
	case 40:
		bw = 1;
		break;
	case 80:
		bw = 2;
		break;
	default:
		return (EINVAL);
	}

	ret = mtwn_mt7610_mcu_function_select(sc, MT7610_MCU_FUNC_BW_SETTING,
	    bw);
	if (ret != 0)
		return (ret);

	return (0);
}

/**
 * @brief Program the BB/PHY band and primary upper/lower flags
 *
 * based on mt76x02_phy_set_band()
 */
int
mtwn_mt7610_phy_set_band(struct mtwn_softc *sc,
    const struct ieee80211_channel *c)
{
	int primary_upper = 0;

	/* XXX TODO: this deviates from mt76; it's possible I got it wrong! */

	if (IEEE80211_IS_CHAN_2GHZ(c)) {
		MTWN_REG_SET_4(sc, MT7610_REG_TX_BAND_CFG,
		    MT7610_REG_TX_BAND_CFG_2G);
		MTWN_REG_CLEAR_4(sc, MT7610_REG_TX_BAND_CFG,
		    MT7610_REG_TX_BAND_CFG_5G);
	} else {
		MTWN_REG_CLEAR_4(sc, MT7610_REG_TX_BAND_CFG,
		    MT7610_REG_TX_BAND_CFG_2G);
		MTWN_REG_SET_4(sc, MT7610_REG_TX_BAND_CFG,
		    MT7610_REG_TX_BAND_CFG_5G);

	}

	if (IEEE80211_IS_CHAN_HT40D(c) || IEEE80211_IS_CHAN_VHT40D(c))
		primary_upper = 1;

	MTWN_REG_RMW_4(sc, MT7610_REG_TX_BAND_CFG,
	    MT7610_REG_TX_BAND_CFG_UPPER_40M, primary_upper);

	return (0);
}

/**
 * @brief Set the band config for the RF side of the baseband.
 *
 * This is based on mt76x0_phy_set_band().
 */
int
mtwn_mt7610_phy_set_rf_band(struct mtwn_softc *sc,
    const struct ieee80211_channel *c)
{
	int ret;

	if (IEEE80211_IS_CHAN_2GHZ(c)) {
		ret = MTWN_RF_REG_PAIR_WRITE_4(sc,
		    mtwn_mt761x0_rf_2g_channel_0_tab,
		    nitems(mtwn_mt761x0_rf_2g_channel_0_tab));
		if (ret != 0)
			return (ret);

		MTWN_RF_REG_WRITE_4(sc, MT7610_REG_RF(5, 0), 0x45);
		MTWN_RF_REG_WRITE_4(sc, MT7610_REG_RF(6, 0), 0x44);

		MTWN_REG_WRITE_4(sc, MT7610_REG_TX_ALC_VGA3, 0x00050007);
		MTWN_REG_WRITE_4(sc, MT7610_REG_TX0_RF_GAIN_CORR, 0x003E0002);
	} else {
		ret = MTWN_RF_REG_PAIR_WRITE_4(sc,
		    mtwn_mt7610_rf_5g_channel_0_tab,
		    nitems(mtwn_mt7610_rf_5g_channel_0_tab));
		if (ret != 0)
			return (ret);

		MTWN_RF_REG_WRITE_4(sc, MT7610_REG_RF(5, 0), 0x44);
		MTWN_RF_REG_WRITE_4(sc, MT7610_REG_RF(6, 0), 0x45);

		MTWN_REG_WRITE_4(sc, MT7610_REG_TX_ALC_VGA3, 0x00000005);
		MTWN_REG_WRITE_4(sc, MT7610_REG_TX0_RF_GAIN_CORR, 0x01010102);
	}
	return (0);
}

/**
 * @brief Program the RF frequency/configuration parameters.
 *
 * This is based on mt76x0_phy_set_chan_rf_params().
 */
int
mtwn_mt7610_phy_set_chan_rf_params(struct mtwn_softc *sc,
    const struct ieee80211_channel *c, uint16_t rf_bw_band)
{
	const struct mtwn_mt7610_freq_item *freq_item;
	uint16_t rf_band = rf_bw_band & 0xff00;
	uint16_t rf_bw = rf_bw_band & 0x00ff;
	bool b_sdm = false;
	uint32_t mac_reg;
	int i;
	const uint8_t channel = MTWN_CHANNEL_TO_HW_VALUE(c);

	for (i = 0; i < nitems(mtwn_mt7610_sdm_channel); i++) {
		if (channel == mtwn_mt7610_sdm_channel[i]) {
			b_sdm = true;
			break;
		}
	}

	for (i = 0; i < nitems(mtwn_mt7610_frequency_plan); i++) {
		if (channel == mtwn_mt7610_frequency_plan[i].channel) {
			rf_band = mtwn_mt7610_frequency_plan[i].band;

			if (b_sdm)
				freq_item = &mtwn_mt7610_sdm_frequency_plan[i];
			else
				freq_item = &mtwn_mt7610_frequency_plan[i];

			MTWN_RF_REG_WRITE_4(sc, MT7610_REG_RF(0, 37), freq_item->pllR37);
			MTWN_RF_REG_WRITE_4(sc, MT7610_REG_RF(0, 36), freq_item->pllR36);
			MTWN_RF_REG_WRITE_4(sc, MT7610_REG_RF(0, 35), freq_item->pllR35);
			MTWN_RF_REG_WRITE_4(sc, MT7610_REG_RF(0, 34), freq_item->pllR34);
			MTWN_RF_REG_WRITE_4(sc, MT7610_REG_RF(0, 33), freq_item->pllR33);

			MTWN_RF_REG_RMW_4(sc, MT7610_REG_RF(0, 32), 0xe0,
				      freq_item->pllR32_b7b5);

			/* R32<4:0> pll_den: (Denomina - 8) */
			MTWN_RF_REG_RMW_4(sc, MT7610_REG_RF(0, 32),
			    MT7610_REG_RF_PLL_DEN_MASK,
			    freq_item->pllR32_b4b0);

			/* R31<7:5> */
			MTWN_RF_REG_RMW_4(sc, MT7610_REG_RF(0, 31), 0xe0,
			    freq_item->pllR31_b7b5);

			/* R31<4:0> pll_k(Nominator) */
			MTWN_RF_REG_RMW_4(sc, MT7610_REG_RF(0, 31),
			    MT7610_REG_RF_PLL_K_MASK, freq_item->pllR31_b4b0);

			/* R30<7> sdm_reset_n */
			if (b_sdm) {
				MTWN_RF_REG_CLEAR_4(sc, MT7610_REG_RF(0, 30),
						MT7610_REG_RF_SDM_RESET_MASK);
				MTWN_RF_REG_SET_4(sc, MT7610_REG_RF(0, 30),
					      MT7610_REG_RF_SDM_RESET_MASK);
			} else {
				MTWN_RF_REG_RMW_4(sc, MT7610_REG_RF(0, 30),
					      MT7610_REG_RF_SDM_RESET_MASK,
					      freq_item->pllR30_b7);
			}

			/* R30<6:2> sdmmash_prbs,sin */
			MTWN_RF_REG_RMW_4(sc, MT7610_REG_RF(0, 30),
				      MT7610_REG_RF_SDM_MASH_PRBS_MASK,
				      freq_item->pllR30_b6b2);

			/* R30<1> sdm_bp */
			MTWN_RF_REG_RMW_4(sc, MT7610_REG_RF(0, 30),
			    MT7610_REG_RF_SDM_BP_MASK,
			    freq_item->pllR30_b1 << 1);

			/* R30<0> R29<7:0> (hex) pll_n */
			MTWN_RF_REG_WRITE_4(sc, MT7610_REG_RF(0, 29),
			    freq_item->pll_n & 0xff);

			MTWN_RF_REG_RMW_4(sc, MT7610_REG_RF(0, 30), 0x1,
			      (freq_item->pll_n >> 8) & 0x1);

			/* R28<7:6> isi_iso */
			MTWN_RF_REG_RMW_4(sc, MT7610_REG_RF(0, 28),
			    MT7610_REG_RF_ISI_ISO_MASK,
			    freq_item->pllR28_b7b6);

			/* R28<5:4> pfd_dly */
			MTWN_RF_REG_RMW_4(sc, MT7610_REG_RF(0, 28),
			    MT7610_REG_RF_PFD_DLY_MASK,
			    freq_item->pllR28_b5b4);

			/* R28<3:2> clksel option */
			MTWN_RF_REG_RMW_4(sc, MT7610_REG_RF(0, 28),
			    MT7610_REG_RF_CLK_SEL_MASK,
			    freq_item->pllR28_b3b2);

			/* R28<1:0> R27<7:0> R26<7:0> (hex) sdm_k */
			MTWN_RF_REG_WRITE_4(sc, MT7610_REG_RF(0, 26),
				     freq_item->pll_sdm_k & 0xff);
			MTWN_RF_REG_WRITE_4(sc, MT7610_REG_RF(0, 27),
				     (freq_item->pll_sdm_k >> 8) & 0xff);

			MTWN_RF_REG_RMW_4(sc, MT7610_REG_RF(0, 28), 0x3,
				      (freq_item->pll_sdm_k >> 16) & 0x3);

			/* R24<1:0> xo_div */
			MTWN_RF_REG_RMW_4(sc, MT7610_REG_RF(0, 24),
			    MT7610_REG_RF_XO_DIV_MASK,
			    freq_item->pllR24_b1b0);

			break;
		}
	}

	for (i = 0; i < nitems(mtwn_mt7610_rf_bw_switch_tab); i++) {
		if (rf_bw == mtwn_mt7610_rf_bw_switch_tab[i].bw_band) {
			MTWN_RF_REG_WRITE_4(sc,
				     mtwn_mt7610_rf_bw_switch_tab[i].rf_bank_reg,
				     mtwn_mt7610_rf_bw_switch_tab[i].value);
		} else if ((rf_bw == (mtwn_mt7610_rf_bw_switch_tab[i].bw_band & 0xFF)) &&
			   (rf_band & mtwn_mt7610_rf_bw_switch_tab[i].bw_band)) {
			MTWN_RF_REG_WRITE_4(sc,
				     mtwn_mt7610_rf_bw_switch_tab[i].rf_bank_reg,
				     mtwn_mt7610_rf_bw_switch_tab[i].value);
		}
	}

	for (i = 0; i < nitems(mtwn_mt7610_rf_band_switch_tab); i++) {
		if (mtwn_mt7610_rf_band_switch_tab[i].bw_band & rf_band) {
			MTWN_RF_REG_WRITE_4(sc,
				     mtwn_mt7610_rf_band_switch_tab[i].rf_bank_reg,
				     mtwn_mt7610_rf_band_switch_tab[i].value);
		}
	}

	MTWN_REG_CLEAR_4(sc, MT7610_REG_RF_MISC, 0xc);

	//band = (rf_band & RF_G_BAND) ? NL80211_BAND_2GHZ : NL80211_BAND_5GHZ;
	if (mtwn_mt7610_ext_pa_enabled(sc, c)) {
		/* MT_RF_MISC (offset: 0x0518)
		 * [2]1'b1: enable external A band PA
		 *    1'b0: disable external A band PA
		 * [3]1'b1: enable external G band PA
		 *    1'b0: disable external G band PA
		 */
		if (rf_band &  MTWN_MT7610_PHY_RF_A_BAND)
			MTWN_REG_SET_4(sc, MT7610_REG_RF_MISC, (1 << 2));
		else
			MTWN_REG_SET_4(sc, MT7610_REG_RF_MISC, (1 << 3));

		/* External PA */
		for (i = 0; i < nitems(mtwn_mt7610_rf_ext_pa_tab); i++)
			if (mtwn_mt7610_rf_ext_pa_tab[i].bw_band & rf_band)
				MTWN_RF_REG_WRITE_4(sc,
					mtwn_mt7610_rf_ext_pa_tab[i].rf_bank_reg,
					mtwn_mt7610_rf_ext_pa_tab[i].value);
	}

	if (rf_band & MTWN_MT7610_PHY_RF_G_BAND) {
		MTWN_REG_WRITE_4(sc, MT7610_REG_TX0_RF_GAIN_ATTEN, 0x63707400);
		/* Set Atten mode = 2 For G band, Disable Tx Inc dcoc. */
		mac_reg = MTWN_REG_READ_4(sc, MT7610_REG_TX_ALC_CFG_1);
		mac_reg &= 0x896400FF;
		MTWN_REG_WRITE_4(sc, MT7610_REG_TX_ALC_CFG_1, mac_reg);
	} else {
		MTWN_REG_WRITE_4(sc, MT7610_REG_TX0_RF_GAIN_ATTEN, 0x686A7800);
		/* Set Atten mode = 0
		 * For Ext A band, Disable Tx Inc dcoc Cal.
		 */
		mac_reg = MTWN_REG_READ_4(sc, MT7610_REG_TX_ALC_CFG_1);
		mac_reg &= 0x890400FF;
		MTWN_REG_WRITE_4(sc, MT7610_REG_TX_ALC_CFG_1, mac_reg);
	}

	return (0);
}

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

int
mtwn_mt7610_phy_channel_calibrate(struct mtwn_softc *sc, bool power_on)
{
	const struct ieee80211com *ic = IF_MTWN_SC_TO_NET80211(sc);
	uint32_t ibi, tx_alc, val;
	const int is_5ghz = (IEEE80211_IS_CHAN_5GHZ(ic->ic_curchan)) ? 1 : 0;

	if (MTWN_MT7610_CHIP_IS_MT7630(sc))
		return (0);

	/* Do power on configuration / calibration */
	if (power_on) {
		(void) mtwn_mt7610_mcu_calibrate(sc,
		    MT7610_MCU_CALIBRATE_FUNC_R, 0);
		(void) mtwn_mt7610_mcu_calibrate(sc,
		    MT7610_MCU_CALIBRATE_FUNC_VCO,
		    MTWN_CHANNEL_TO_HW_VALUE(ic->ic_curchan));
		MTWN_UDELAY(sc, 20);

		MTWN_TODO_PRINTF(sc, "%s: tssi checks\n", __func__);
		/* TODO: tssi_enabled, tssi_dc_calibrate, etc */

	}

	/* save TX ALC config; zero it */
	tx_alc = MTWN_REG_READ_4(sc, MT7610_REG_TX_ALC_CFG_0);
	MTWN_REG_WRITE_4(sc, MT7610_REG_TX_ALC_CFG_0, 0);
	MTWN_UDELAY(sc, 700);

	/* save IBI */
	ibi = MTWN_REG_READ_4(sc, MT7610_REG_BBP(IBI, 9));
	MTWN_REG_WRITE_4(sc, MT7610_REG_BBP(IBI, 9), 0xffffff7e);

	if (IEEE80211_IS_CHAN_5GHZ(ic->ic_curchan)) {
		if (MTWN_CHANNEL_TO_HW_VALUE(ic->ic_curchan) < 100)
			val = 0x701;
		else if (MTWN_CHANNEL_TO_HW_VALUE(ic->ic_curchan) < 140)
			val = 0x801;
		else
			val = 0x901;
	} else
		val = 0x600;

	/* Full cal */
	(void) mtwn_mt7610_mcu_calibrate(sc, MT7610_MCU_CALIBRATE_FUNC_FULL,
	    val);

	/* LC cal */
	(void) mtwn_mt7610_mcu_calibrate(sc, MT7610_MCU_CALIBRATE_FUNC_LC,
	    is_5ghz);

	MTWN_MDELAY(sc, 20);

	/* Restore IBI */
	MTWN_REG_WRITE_4(sc, MT7610_REG_BBP(IBI, 9), ibi);

	/* Restore TX ALC */
	MTWN_REG_WRITE_4(sc, MT7610_REG_TX_ALC_CFG_0, tx_alc);

	/* RXDCOC calibrate */
	(void) mtwn_mt7610_mcu_calibrate(sc, MT7610_MCU_CALIBRATE_FUNC_RXDCOC,
	    1);

	return (0);
}

int
mtwn_mt7610_set_channel(struct mtwn_softc *sc,
    const struct ieee80211_channel *chan)
{
	MTWN_TODO_PRINTF(sc, "%s: called\n", __func__);
	int ch_group_index, ret;
	uint8_t channel;
	uint32_t val;
	uint16_t rf_bw_band;
	int chw;

	channel = MTWN_CHANNEL_TO_HW_VALUE(chan);
	if (channel <= 14)
		rf_bw_band = MTWN_MT7610_PHY_RF_G_BAND;
	else
		rf_bw_band = MTWN_MT7610_PHY_RF_A_BAND;

	/* XXX TODO: only do 20MHz channels */
	MTWN_TODO_PRINTF(sc, "%s: need to handle 40/80MHz channels\n", __func__);
	ch_group_index = 0;
	rf_bw_band |= MTWN_MT7610_PHY_RF_BW_20;
	chw = 20;

	/* Set bandwidth */
	if (MTWN_IS_BUS_USB(sc)) {
		ret = mtwn_mt7610_phy_bbp_set_bw(sc, chw);
		if (ret != 0)
			goto error;
	} else {
		if (chw == 40 || chw == 80)
			val = 0x201;
		else
			val = 0x601;
		MTWN_REG_WRITE_4(sc, MT7610_REG_TX_SW_CFG0, val);
	}
	ret = mtwn_mt7610_phy_set_bw(sc, chw, ch_group_index);
	if (ret != 0)
		goto error;

	/* Set PHY bw/band */
	ret = mtwn_mt7610_phy_set_band(sc, chan);
	if (ret != 0)
		goto error;

	/* Setup CCA */
	MTWN_REG_RMW_4(sc,
	    MT7610_REG_EXT_CCA_CFG,
	    (MT7610_REG_EXT_CCA_CFG_CCA0 | MT7610_REG_EXT_CCA_CFG_CCA1 |
	     MT7610_REG_EXT_CCA_CFG_CCA2 | MT7610_REG_EXT_CCA_CFG_CCA3 |
	     MT7610_REG_EXT_CCA_CFG_CCA_MASK),
	    ext_cca_chan[ch_group_index]);

	/* Setup RF band/parameters */
	ret = mtwn_mt7610_phy_set_rf_band(sc, chan);
	if (ret != 0)
		goto error;
	ret = mtwn_mt7610_phy_set_chan_rf_params(sc, chan, rf_bw_band);
	if (ret != 0)
		goto error;

	/* Set japan TX filter at channel 14 */
	if (channel == 14)
		MTWN_REG_SET_4(sc, MT7610_REG_BBP(CORE, 1), 0x20);
	else
		MTWN_REG_CLEAR_4(sc, MT7610_REG_BBP(CORE, 1), 0x20);

	/* Read RX gain */
	(void) mtwn_mt7610_phy_read_rx_gain(sc, chan);

	/* Set BBP */
	ret = mtwn_mt7610_bbp_set_switch_table(sc, rf_bw_band, true);
	if (ret != 0)
		goto error;

	/* Enable VCO */
	MTWN_RF_REG_SET_4(sc, MT7610_REG_RF(0, 4), 0x80);

	/* Done if scan */

	/* Init AGC gain */
	MTWN_TODO_PRINTF(sc, "%s: init agc gain\n", __func__);

	/* PHY calibrate */
	ret = mtwn_mt7610_phy_channel_calibrate(sc, false);
	if (ret != 0)
		goto error;

	/* get and write TX power parameters */
	ret = mtwn_mt7610_phy_get_txpower(sc, chan);
	if (ret != 0)
		goto error;
	MTWN_TODO_PRINTF(sc, "%s: set/write txpower\n", __func__);

	return (0);
error:
	return (ret);
}
