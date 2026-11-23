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
#ifndef	__MTWN_MT7610_EEPROM_H__
#define	__MTWN_MT7610_EEPROM_H__

typedef enum {
	MTWN_MT7610_EEPROM_CAL_CHANNEL_GROUP_5G_JAPAN = 0,
	MTWN_MT7610_EEPROM_CAL_CHANNEL_GROUP_5G_UNII_1,
	MTWN_MT7610_EEPROM_CAL_CHANNEL_GROUP_5G_UNII_2,
	MTWN_MT7610_EEPROM_CAL_CHANNEL_GROUP_5G_UNII_2E_1,
	MTWN_MT7610_EEPROM_CAL_CHANNEL_GROUP_5G_UNII_2E_2,
	MTWN_MT7610_EEPROM_CAL_CHANNEL_GROUP_5G_UNII_3,
	MTWN_MT7610_EEPROM_CAL_CHANNEL_GROUP_5G_UNII_NUM
} mtwn_mt7610_eeprom_cal_channel_group_t;

extern	int mtwn_mt7610_efuse_read(struct mtwn_softc *, uint16_t,
	    char *, uint32_t);
extern	int mtwn_mt7610_efuse_read_range(struct mtwn_softc *, uint16_t,
	    char *, int, uint32_t);
extern	int mtwn_mt7610_efuse_physical_size_check(struct mtwn_softc *);
extern	int mtwn_mt7610_efuse_populate(struct mtwn_softc *, char *, uint32_t);

extern	int mtwn_mt7610_eeprom_macaddr_read(struct mtwn_softc *, uint8_t *);
extern	int mtwn_mt7610_eeprom_read_2(struct mtwn_softc *, uint16_t);
extern	int mtwn_mt7610_eeprom_read_1(struct mtwn_softc *, uint16_t);

extern	bool mtwn_mt7610_eeprom_field_valid_1(struct mtwn_softc *, uint8_t);
extern	int32_t mtwn_mt7610_eeprom_field_sign_extend(struct mtwn_softc *,
	    uint32_t, uint32_t);
extern	int32_t
	    mtwn_mt7610_eeprom_field_sign_extend_optional(struct mtwn_softc *,
	    uint32_t, uint32_t);

extern	mtwn_mt7610_eeprom_cal_channel_group_t
	    mtwn_mt7612_eeprom_get_cal_channel_group(
	      const struct ieee80211_channel *);
extern	uint8_t mtwn_mt7612_eeprom_get_5g_rx_gain(struct mtwn_softc *,
	    const struct ieee80211_channel *);
extern	void mtwn_mt7610_eeprom_get_rx_gain_group(struct mtwn_softc *, uint8_t,
	    int8_t *);
extern	int mtwn_mt7610_eeprom_get_rx_gain(struct mtwn_softc *,
	    const struct ieee80211_channel *, uint16_t *, int8_t *, int8_t *);
extern	uint8_t mtwn_mt7610_get_lna_gain(struct mtwn_softc *, int8_t *,
	    int8_t *, const struct ieee80211_channel *);
extern	bool mtwn_mt7610_ext_pa_enabled(struct mtwn_softc *,
	    const struct ieee80211_channel *);

#endif	/* __MTWN_MT7610_EEPROM_H__ */
