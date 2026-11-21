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
#include "mtwn_mt7610_eeprom.h"
#include "mtwn_mt7610_eeprom_reg.h"

/**
 * @brief read 16 bytes from the efuse/eeprom at the given offset.
 */
int
mtwn_mt7610_efuse_read(struct mtwn_softc *sc, uint16_t addr,
    char *data, uint32_t mode)
{
	uint32_t val;
	int i;

	MTWN_LOCK_ASSERT(sc, MA_OWNED);

	/* Prepare for the transfer */
	val = MTWN_REG_READ_4(sc, MT7610_REG_EFUSE_CTRL);
	val &= ~(MT7610_REG_EFUSE_CTRL_AIN | MT7610_REG_EFUSE_CTRL_MODE);
	val |= _IEEE80211_SHIFTMASK(addr & ~0x0f, MT7610_REG_EFUSE_CTRL_AIN);
	val |= _IEEE80211_SHIFTMASK(mode, MT7610_REG_EFUSE_CTRL_MODE);
	val |= MT7610_REG_EFUSE_CTRL_KICK;
	MTWN_REG_WRITE_4(sc, MT7610_REG_EFUSE_CTRL, val);

	/* Poll for KICK going low */
	if (! mtwn_reg_poll(sc, MT7610_REG_EFUSE_CTRL,
	    MT7610_REG_EFUSE_CTRL_KICK, 0, 1000 * 1000)) {
		MTWN_ERR_PRINTF(sc, "%s: efuse read timeout\n", __func__);
		return (ETIMEDOUT);
	}

	MTWN_UDELAY(sc, 100);

	/* Check whether the contents are valid */
	val = MTWN_REG_READ_4(sc, MT7610_REG_EFUSE_CTRL);
	if ((val & MT7610_REG_EFUSE_CTRL_AOUT) == MT7610_REG_EFUSE_CTRL_AOUT) {
		/*
		 * Note: this happens during normal operation, so don't treat it
		 * as an error.
		 */
		memset(data, 0xff, 16);
		return (0);
	}

	/* Read 16 bytes of EFUSE */
	for (i = 0; i < 4; i++) {
		val = MTWN_REG_READ_4(sc, MT7610_REG_EFUSE_DATA(i));
		memcpy(data + (4 * i), &val, sizeof(val));
	}

	return (0);
}

/**
 * @brief read a range of EFUSE data.
 *
 * This requires a buffer of a multiple of 16 bytes.
 */
int
mtwn_mt7610_efuse_read_range(struct mtwn_softc *sc, uint16_t base,
    char *buf, int len, uint32_t mode)
{
	int i, ret;

	MTWN_LOCK_ASSERT(sc, MA_OWNED);

	/* Verify the size is a multiple of 16 bytes */
	if (len % 16 != 0) {
		MTWN_ERR_PRINTF(sc, "%s: invalid read size\n", __func__);
		return (EINVAL);
	}

	for (i = 0; i + 16 <= len; i+= 16) {
		ret = mtwn_mt7610_efuse_read(sc, base + i, buf + i, mode);
		if (ret != 0) {
			MTWN_ERR_PRINTF(sc, "%s: invalid read (err %d)\n",
			    __func__, ret);
			return (ret);
		}
	}
	return (0);
}

/**
 * @brief validate the EEPROM size is valid.
 *
 * This validates some usage map information from the EEPROM.
 */
int
mtwn_mt7610_efuse_physical_size_check(struct mtwn_softc *sc)
{
	uint8_t data[roundup(MT7610_EEPROM_USAGE_MAP_SIZE, 16)];
	int i, ret;
	uint32_t cnt_free, start = 0, end = 0;

	/* Fetch the usage map */
	ret = mtwn_mt7610_efuse_read_range(sc, MT7610_EEPROM_USAGE_MAP_START,
	    data, sizeof(data), MT7610_REG_EFUSE_CTRL_MODE_EE_PHYS_READ);
	if (ret != 0) {
		MTWN_ERR_PRINTF(sc, "%s: failed to read efuse (err %d)\n",
		    __func__, ret);
		return (ret);
	}

	(void) start; (void) end; (void) cnt_free; (void) i;

	MTWN_TODO_PRINTF(sc, "%s: TODO: dump/validate EEPROM usage map\n",
	    __func__);

	return (0);
}

/**
 * @brief Read the EFUSE contents into the in-memory EEPROM
 * buffer.
 */
int
mtwn_mt7610_efuse_populate(struct mtwn_softc *sc, char *buf, uint32_t len)
{
	int ret;

	ret = mtwn_mt7610_efuse_read_range(sc, 0, buf, len,
	    MT7610_REG_EFUSE_CTRL_MODE_EE_READ);
	if (ret != 0) {
		MTWN_ERR_PRINTF(sc, "%s: failed to read efuse (err %d)\n",
		    __func__, ret);
		return (ret);
	}

	return (0);
}

int
mtwn_mt7610_eeprom_macaddr_read(struct mtwn_softc *sc, uint8_t *mac)
{
	if (sc->sc_eepromops_priv == NULL)
		return (ENXIO);

	memcpy(mac, (char *) sc->sc_eepromops_priv + MT7610_EEPROM_MAC_ADDR,
	    ETHER_ADDR_LEN);

	return (0);
}

/**
 * @brief read two bytes at the given offset, or return -1 upon failure.
 */
int
mtwn_mt7610_eeprom_read_2(struct mtwn_softc *sc, uint16_t offset)
{
	if (sc->sc_eepromops_priv == NULL)
		return (-1);

	/*
	 * Check the request fits in the eeprom range, remembering
	 * we are reading two bytes.
	 */
	if (offset > MT7610_EEPROM_SIZE-2)
		return (-1);
	if ((offset & 1) != 0)
		return (-1);

	return (le16dec(((const char *) sc->sc_eepromops_priv) + offset));
}

/**
 * @brief read one byte at any offset, or return -1 upon failure.
 */
int
mtwn_mt7610_eeprom_read_1(struct mtwn_softc *sc, uint16_t offset)
{
	uint16_t addr;
	uint16_t val;

	if (sc->sc_eepromops_priv == NULL)
		return (-1);

	/* Check the request fits in the eeprom range */
	if (offset > MT7610_EEPROM_SIZE-1)
		return (-1);

	/* We're reading two bytes, and masking off one as needed */
	addr = offset & 0xfffe;

	memcpy(&val, ((const char *) sc->sc_eepromops_priv) + addr,
	    sizeof(uint16_t));

	if ((offset & 1) == 0)
		return (val & 0xff);
	else
		return (val >> 8);
}

/**
 * @brief Validate that a field is "valid".
 *
 * Validating a field is "valid" here is that it is not 0x0 or 0xff.
 * This is from mt76; my guess is to capture field validity based on
 * whether it's populated (not 0x0) and not invalid/missing from
 * efuse (0xff).
 */
bool
mtwn_mt7610_eeprom_field_valid_1(struct mtwn_softc *sc, uint8_t field)
{
	return ((field != 0) && (field != 0xff));
}

/**
 * @brief Sign extend a value.
 *
 * size is the number of bits the value can have.
 */
int32_t
mtwn_mt7610_eeprom_field_sign_extend(struct mtwn_softc *sc, uint32_t val,
    uint32_t size)
{
#if 0
	bool sign = (val & (1 << (size - 1)));

	val &= (1 << (size - 1)) - 1;
	return (sign ? val : -val);
#else
	uint32_t sign = val & (1 << (size - 1));
	uint32_t val2 = val & (1 << (size - 1)) - 1;
	return (val2 | -sign);
#endif
}

/**
 * @brief Sign extend a value, or return 0.
 *
 * this will sign extend based on the number of bits in size,
 * however if BIT(size) is 0, then 0 is returned.
 *
 * Eg if the number is 0x, and size is 8, then bits 0..7 are checked
 * for the value (and not sign extended) but then since bit 8 isn't set,
 * 0 is returned.
 */
int32_t
mtwn_mt7610_eeprom_field_sign_extend_optional(struct mtwn_softc *sc,
    uint32_t val, uint32_t size)
{
	bool enable;

	enable = val & (1 << size);
	return (enable ? mtwn_mt7610_eeprom_field_sign_extend(sc, val, size)
	    : 0);
}

/**
 * @brief Get the channel group for the given channel.
 *
 * Note: The mt76 driver does this based on the primary channel in the
 * mac/nl80211 channel representation (chan->hw_value), rather than
 * the true centre channel "number" for 40/80MHz wide channels.
 */
mtwn_mt7610_eeprom_cal_channel_group_t
mtwn_mt7610_eeprom_get_cal_channel_group(const struct ieee80211_channel *c)
{
	/* XXX TODO: verify c->ic_ieee matches the mt76 hw_value channel */
	if (c->ic_ieee >= 184 && c->ic_ieee <= 196)
		return MTWN_MT7610_EEPROM_CAL_CHANNEL_GROUP_5G_JAPAN;
	if (c->ic_ieee <= 48)
		return MTWN_MT7610_EEPROM_CAL_CHANNEL_GROUP_5G_UNII_1;
	if (c->ic_ieee <= 64)
		return MTWN_MT7610_EEPROM_CAL_CHANNEL_GROUP_5G_UNII_2;
	if (c->ic_ieee <= 114)
		return MTWN_MT7610_EEPROM_CAL_CHANNEL_GROUP_5G_UNII_2E_1;
	if (c->ic_ieee <= 144)
		return MTWN_MT7610_EEPROM_CAL_CHANNEL_GROUP_5G_UNII_2E_2;
	return MTWN_MT7610_EEPROM_CAL_CHANNEL_GROUP_5G_UNII_3;
}

/**
 * @brief Get the relevant calibration information for a 5G channel.
 *
 * Note: The mt76 driver does this based on the primary channel in the
 * mac/nl80211 channel representation (chan->hw_value), rather than
 * the true centre channel "number" for 40/80MHz wide channels.
 */
uint8_t
mtwn_mt7610_eeprom_get_5g_rx_gain(struct mtwn_softc *sc,
    const struct ieee80211_channel *c)
{
	mtwn_mt7610_eeprom_cal_channel_group_t group;

	group = mtwn_mt7610_eeprom_get_cal_channel_group(c);

	MTWN_TODO_PRINTF(sc,
	    "%s: Turn these into one byte EEPROM field reads?\n", __func__);

	switch (group) {
	case MTWN_MT7610_EEPROM_CAL_CHANNEL_GROUP_5G_JAPAN:
		return MTWN_EEPROM_READ_2(sc,
		    MT7610_EEPROM_RF_5G_GRP0_1_RX_HIGH_GAIN);

	case MTWN_MT7610_EEPROM_CAL_CHANNEL_GROUP_5G_UNII_1:
		return MTWN_EEPROM_READ_2(sc,
		    MT7610_EEPROM_RF_5G_GRP0_1_RX_HIGH_GAIN) >> 8;

	case MTWN_MT7610_EEPROM_CAL_CHANNEL_GROUP_5G_UNII_2:
		return MTWN_EEPROM_READ_2(sc,
		    MT7610_EEPROM_RF_5G_GRP2_3_RX_HIGH_GAIN);

	case MTWN_MT7610_EEPROM_CAL_CHANNEL_GROUP_5G_UNII_2E_1:
		return MTWN_EEPROM_READ_2(sc,
		    MT7610_EEPROM_RF_5G_GRP2_3_RX_HIGH_GAIN) >> 8;

	case MTWN_MT7610_EEPROM_CAL_CHANNEL_GROUP_5G_UNII_2E_2:
		return MTWN_EEPROM_READ_2(sc,
		    MT7610_EEPROM_RF_5G_GRP4_5_RX_HIGH_GAIN);

	default:
		return MTWN_EEPROM_READ_2(sc,
		    MT7610_EEPROM_RF_5G_GRP4_5_RX_HIGH_GAIN) >> 8;
	}

	return (0);
}

/**
 * @brief Return the RX gain group to use based on the provided value.
 */
void
mtwn_mt7610_eeprom_get_rx_gain_group(struct mtwn_softc *sc, uint8_t val,
    int8_t *dest)
{
	if (mtwn_mt7610_eeprom_field_valid_1(sc, val) == false) {
		dest[0] = dest[1] = 0;
		return;
	}

	dest[0] = mtwn_mt7610_eeprom_field_sign_extend(sc, val, 4);
	dest[1] = mtwn_mt7610_eeprom_field_sign_extend(sc, val >> 4, 4);
}

int
mtwn_mt7610_eeprom_get_rx_gain(struct mtwn_softc *sc,
    const struct ieee80211_channel *c, uint16_t *rssi_offset,
    int8_t *lna_2g, int8_t *lna_5g)
{
	uint16_t val;

	/* TODO: figure out how to just do 1 byte reads here for each field? */

	/* Fetch LNA gain */
	val = MTWN_EEPROM_READ_2(sc, MT7610_EEPROM_LNA_GAIN);
	*lna_2g = val & 0xff;

	/* Extract 5g gain from high byte from LNA gain parameter */
	lna_5g[0] = val >> 8;

	/* Fetch 2g/5g gain */
	val = MTWN_EEPROM_READ_2(sc, MT7610_EEPROM_RSSI_OFFSET_2G_1);
	lna_5g[1] = val >> 8;

	val = MTWN_EEPROM_READ_2(sc, MT7610_EEPROM_RSSI_OFFSET_5G_1);
	lna_5g[2] = val >> 8;

	/* Verify they're valid */
	if (!mtwn_mt7610_eeprom_field_valid_1(sc, lna_5g[1]))
		lna_5g[1] = lna_5g[0];
	if (!mtwn_mt7610_eeprom_field_valid_1(sc, lna_5g[2]))
		lna_5g[2] = lna_5g[0];

	/* Fetch gain/rssi offset */
	if (IEEE80211_IS_CHAN_2GHZ(c))
		*rssi_offset = MTWN_EEPROM_READ_2(sc,
		    MT7610_EEPROM_RSSI_OFFSET_2G_0);
	else
		*rssi_offset = MTWN_EEPROM_READ_2(sc,
		    MT7610_EEPROM_RSSI_OFFSET_2G_1);

	return (0);
}

uint8_t
mtwn_mt7610_get_lna_gain(struct mtwn_softc *sc, int8_t *lna_2g, int8_t *lna_5g,
    const struct ieee80211_channel *c)
{
	uint8_t lna;

	if (IEEE80211_IS_CHAN_2GHZ(c))
		lna = *lna_2g;
	else if (c->ic_ieee <= 64)
		lna = lna_5g[0];
	else if (c->ic_ieee <= 128)
		lna = lna_5g[1];
	else
		lna = lna_5g[2];

	return (lna != 0xff) ? lna : 0;
}
