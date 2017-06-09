
#include <stdint.h>
#include <errno.h>

#include <osmocom/gsm/gsm_utils.h>

#include <osmo-bts/gsm_data.h>
#include <osmo-bts/logging.h>
#include <osmo-bts/measurement.h>

/* Measurment reporting period and mapping of SACCH message block for TCHF
 * and TCHH chan As per in 3GPP TS 45.008, secton 8.4.1.
 *
 *             Timeslot number (TN)        TDMA frame number (FN) modulo 104
 *             Half rate,    Half rate,     Reporting    SACCH
 * Full Rate   subch.0       subch.1        period       Message block
 * 0           0 and 1                      0 to 103     12,  38,  64,  90
 * 1                         0 and 1        13 to 12     25,  51,  77,  103
 * 2           2 and 3                      26 to 25     38,  64,  90,  12
 * 3                         2 and 3        39 to 38     51,  77,  103, 25
 * 4           4 and 5                      52 to 51     64,  90,  12,  38
 * 5                         4 and 5        65 to 64     77,  103, 25,  51
 * 6           6 and 7                      78 to 77     90,  12,  38,  64
 * 7                         6 and 7        91 to 90     103, 25,  51,  77 */

/* measurement period ends at fn % 104 == ? */
static const uint8_t tchf_meas_rep_fn104[] = {
	[0] =	103,
	[1] =	12,
	[2] =	25,
	[3] =	38,
	[4] =	51,
	[5] =	64,
	[6] =	77,
	[7] =	90,
};
static const uint8_t tchh0_meas_rep_fn104[] = {
	[0] =	103,
	[1] =	103,
	[2] =	25,
	[3] =	25,
	[4] =	51,
	[5] =	51,
	[6] =	77,
	[7] =	77,
};
static const uint8_t tchh1_meas_rep_fn104[] = {
	[0] =	12,
	[1] =	12,
	[2] =	38,
	[3] =	38,
	[4] =	64,
	[5] =	64,
	[6] =	90,
	[7] =	90,
};

/* Measurment reporting period for SDCCH8 and SDCCH4 chan
 * As per in 3GPP TS 45.008, section 8.4.2.
 *
 * Logical Chan		TDMA frame number
 *			(FN) modulo 102
 *
 * SDCCH/8		12 to 11
 * SDCCH/4		37 to 36
 */

/* Added interleve offset to Meas period end Fn which
 * would reduce the Meas Res msg load at Abis */

static const uint8_t sdcch8_meas_rep_fn102[] = {
	[0] = 11 + 7,
	[1] = 11 + 11,
	[2] = 11 + 15,
	[3] = 11 + 19,
	[4] = 11 + 23,
	[5] = 11 + 27,
	[6] = 11 + 31,
	[7] = 11 + 35
};

static const uint8_t sdcch4_meas_rep_fn102[] = {
	[0] = 36 + 4,
	[1] = 36 + 8,
	[2] = 36 + 14,
	[3] = 36 + 18
};


/* determine if a measurement period ends at the given frame number */
static int is_meas_complete(enum gsm_phys_chan_config pchan, unsigned int ts,
			    unsigned int subch, uint32_t fn)
{
	unsigned int fn_mod = -1;
	const uint8_t *tbl;
	int rc = 0;

	if (ts >= 8)
		return -EINVAL;
	if (pchan >= _GSM_PCHAN_MAX)
		return -EINVAL;

	switch (pchan) {
	case GSM_PCHAN_TCH_F:
		fn_mod = fn % 104;
		if (tchf_meas_rep_fn104[ts] == fn_mod)
			rc = 1;
		break;
	case GSM_PCHAN_TCH_H:
		fn_mod = fn % 104;
		if (subch == 0)	
			tbl = tchh0_meas_rep_fn104;
		else
			tbl = tchh1_meas_rep_fn104;
		if (tbl[ts] == fn_mod)
			rc = 1;
		break;
	case GSM_PCHAN_SDCCH8_SACCH8C:
	case GSM_PCHAN_SDCCH8_SACCH8C_CBCH:
		fn_mod = fn % 102;
		if (sdcch8_meas_rep_fn102[subch] == fn_mod)
			rc = 1;
		break;
	case GSM_PCHAN_CCCH_SDCCH4:
	case GSM_PCHAN_CCCH_SDCCH4_CBCH:
		fn_mod = fn % 102;
		if (sdcch4_meas_rep_fn102[subch] == fn_mod)
			rc = 1;
		break;
	default:
		rc = 0;
		break;
	}
	return rc;
}

/* receive a L1 uplink measurement from L1 */
int lchan_new_ul_meas(struct gsm_lchan *lchan, struct bts_ul_meas *ulm)
{
	DEBUGP(DMEAS, "%s adding measurement, num_ul_meas=%d\n",
		gsm_lchan_name(lchan), lchan->meas.num_ul_meas);

	if (lchan->state != LCHAN_S_ACTIVE) {
		LOGP(DMEAS, LOGL_NOTICE, "%s measurement during state: %s\n",
			gsm_lchan_name(lchan), gsm_lchans_name(lchan->state));
	}

	if (lchan->meas.num_ul_meas >= ARRAY_SIZE(lchan->meas.uplink)) {
		LOGP(DMEAS, LOGL_NOTICE, "%s no space for uplink measurement\n",
			gsm_lchan_name(lchan));
		return -ENOSPC;
	}

	memcpy(&lchan->meas.uplink[lchan->meas.num_ul_meas++], ulm,
		sizeof(*ulm));

	return 0;
}

/* input: BER in steps of .01%, i.e. percent/100 */
static uint8_t ber10k_to_rxqual(uint32_t ber10k)
{
	/* Eight levels of Rx quality are defined and are mapped to the
	 * equivalent BER before channel decoding, as per in 3GPP TS 45.008,
	 * secton 8.2.4.
	 *
	 * RxQual:				BER Range:
	 * RXQUAL_0	     BER <  0,2 %       Assumed value = 0,14 %
	 * RXQUAL_1  0,2 % < BER <  0,4 %	Assumed value = 0,28 %
	 * RXQUAL_2  0,4 % < BER <  0,8 %	Assumed value = 0,57 %
	 * RXQUAL_3  0,8 % < BER <  1,6 %	Assumed value = 1,13 %
	 * RXQUAL_4  1,6 % < BER <  3,2 %	Assumed value = 2,26 %
	 * RXQUAL_5  3,2 % < BER <  6,4 %	Assumed value = 4,53 %
	 * RXQUAL_6  6,4 % < BER < 12,8 %	Assumed value = 9,05 %
	 * RXQUAL_7 12,8 % < BER		Assumed value = 18,10 % */

	if (ber10k < 20)
		return 0;
	if (ber10k < 40)
		return 1;
	if (ber10k < 80)
		return 2;
	if (ber10k < 160)
		return 3;
	if (ber10k < 320)
		return 4;
	if (ber10k < 640)
		return 5;
	if (ber10k < 1280)
		return 6;
	return 7;
}

/* Update order  TA at end of meas period */
static void lchan_meas_update_ordered_TA(struct gsm_lchan *lchan,
					 int32_t taqb_sum)
{
	int32_t ms_timing_offset = 0;
	uint8_t l1_info_valid;

	l1_info_valid = lchan->meas.flags & LC_UL_M_F_L1_VALID;

	if (l1_info_valid) {
		DEBUGP(DMEAS,
		       "%s Update TA TimingOffset_Mean:%d, UL RX TA:%d, DL ordered TA:%d, flags:%d \n",
		       gsm_lchan_name(lchan), taqb_sum, lchan->meas.l1_info[1],
		       lchan->rqd_ta, lchan->meas.flags);

		ms_timing_offset =
		    taqb_sum + (lchan->meas.l1_info[1] - lchan->rqd_ta);

		if (ms_timing_offset > 0) {
			if (lchan->rqd_ta < MEAS_MAX_TIMING_ADVANCE) {
				/* MS is moving away from BTS.
				 * So increment Ordered TA by 1 */
				lchan->rqd_ta++;
			}
		} else if (ms_timing_offset < 0) {
			if (lchan->rqd_ta > MEAS_MIN_TIMING_ADVANCE) {
				/* MS is moving toward BTS. So decrement
				 * Ordered TA by 1 */
				lchan->rqd_ta--;
			}
		}

		DEBUGP(DMEAS,
		       "%s New Update TA--> TimingOff_diff:%d, UL RX TA:%d, DL ordered TA:%d \n",
		       gsm_lchan_name(lchan), ms_timing_offset,
		       lchan->meas.l1_info[1], lchan->rqd_ta);
	}

	/* Clear L1 INFO valid flag at Meas period end */
	lchan->meas.flags &= ~LC_UL_M_F_L1_VALID;

	return;
}

int lchan_meas_check_compute(struct gsm_lchan *lchan, uint32_t fn)
{
	struct gsm_meas_rep_unidir *mru;
	uint32_t ber_full_sum = 0;
	uint32_t irssi_full_sum = 0;
	uint32_t ber_sub_sum = 0;
	uint32_t irssi_sub_sum = 0;
	int32_t taqb_sum = 0;
	unsigned int num_meas_sub = 0;
	int i;
	int32_t ms_timing_offset = 0;

	/* if measurement period is not complete, abort */
	if (!is_meas_complete(ts_pchan(lchan->ts), lchan->ts->nr,
			      lchan->nr, fn))
		return 0;

	/* if there are no measurements, skip computation */
	if (lchan->meas.num_ul_meas == 0)
		return 0;

	/* compute the actual measurements */

	/* step 1: add up */
	for (i = 0; i < lchan->meas.num_ul_meas; i++) {
		struct bts_ul_meas *m = &lchan->meas.uplink[i];

		ber_full_sum += m->ber10k;
		irssi_full_sum += m->inv_rssi;
		taqb_sum += m->ta_offs_qbits;

		if (m->is_sub) {
			num_meas_sub++;
			ber_sub_sum += m->ber10k;
			irssi_sub_sum += m->inv_rssi;
		}
	}

	/* step 2: divide */
	ber_full_sum = ber_full_sum / lchan->meas.num_ul_meas;
	irssi_full_sum = irssi_full_sum / lchan->meas.num_ul_meas;
	taqb_sum = taqb_sum / lchan->meas.num_ul_meas;

	if (num_meas_sub) {
		ber_sub_sum = ber_sub_sum / num_meas_sub;
		irssi_sub_sum = irssi_sub_sum / num_meas_sub;
	} else {
		ber_sub_sum = ber_full_sum;
		irssi_sub_sum = irssi_full_sum;
	}

	DEBUGP(DMEAS, "%s Computed TA(% 4dqb) BER-FULL(%2u.%02u%%), RSSI-FULL(-%3udBm), "
		"BER-SUB(%2u.%02u%%), RSSI-SUB(-%3udBm)\n", gsm_lchan_name(lchan),
		taqb_sum, ber_full_sum/100,
		ber_full_sum%100, irssi_full_sum, ber_sub_sum/100, ber_sub_sum%100,
		irssi_sub_sum);

	/* Update ordered TA for DL SACCH L1 Header */
	lchan_meas_update_ordered_TA(lchan, taqb_sum);

	/* store results */
	mru = &lchan->meas.ul_res;
	mru->full.rx_lev = dbm2rxlev((int)irssi_full_sum * -1);
	mru->sub.rx_lev = dbm2rxlev((int)irssi_sub_sum * -1);
	mru->full.rx_qual = ber10k_to_rxqual(ber_full_sum);
	mru->sub.rx_qual = ber10k_to_rxqual(ber_sub_sum);

	lchan->meas.flags |= LC_UL_M_F_RES_VALID;
	lchan->meas.num_ul_meas = 0;

	/* send a signal indicating computation is complete */

	return 1;
}

static int ts_meas_check_compute(struct gsm_bts_trx_ts *ts, uint32_t fn)
{
	int i;
	const int num_subslots = ts_subslots(ts);

	for (i = 0; i < num_subslots; ++i) {
		struct gsm_lchan *lchan = &ts->lchan[i];

		if (lchan->state != LCHAN_S_ACTIVE)
			continue;

		switch (lchan->type) {
		case GSM_LCHAN_SDCCH:
		case GSM_LCHAN_TCH_F:
		case GSM_LCHAN_TCH_H:
		case GSM_LCHAN_PDTCH:
			lchan_meas_check_compute(lchan, fn);
			break;
		default:
			break;
		}
	}
	return 0;
}

/* needs to be called once every TDMA frame ! */
int trx_meas_check_compute(struct gsm_bts_trx *trx, uint32_t fn)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(trx->ts); i++) {
		struct gsm_bts_trx_ts *ts = &trx->ts[i];
		ts_meas_check_compute(ts, fn);
	}
	return 0;
}
