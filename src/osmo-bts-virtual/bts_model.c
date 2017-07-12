/* (C) 2015 by Harald Welte <laforge@gnumonks.org>
 *
 * All Rights Reserved
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include <stdint.h>
#include <errno.h>

#include <osmocom/core/talloc.h>
#include <osmocom/core/utils.h>
#include <osmocom/codec/codec.h>

#include <osmo-bts/gsm_data.h>
#include <osmo-bts/phy_link.h>
#include <osmo-bts/logging.h>
#include <osmo-bts/oml.h>
#include <osmo-bts/rsl.h>
#include <osmo-bts/amr.h>
#include <osmo-bts/bts.h>
#include <osmo-bts/bts_model.h>
#include <osmo-bts/handover.h>
#include <osmo-bts/l1sap.h>

/* TODO: check if dummy method is sufficient, else implement */
int bts_model_lchan_deactivate(struct gsm_lchan *lchan)
{
	return -1;
}

/* TODO: check if dummy method is sufficient, else implement */
int osmo_amr_rtp_dec(const uint8_t *rtppayload, int payload_len, uint8_t *cmr,
		     int8_t *cmi, enum osmo_amr_type *ft, enum osmo_amr_quality *bfi, int8_t *sti)
{
	return -1;
}

int bts_model_trx_close(struct gsm_bts_trx *trx)
{
	return 0;
}

int bts_model_adjst_ms_pwr(struct gsm_lchan *lchan)
{
	return 0;
}

int bts_model_check_oml(struct gsm_bts *bts, uint8_t msg_type,
			struct tlv_parsed *old_attr, struct tlv_parsed *new_attr,
			void *obj)
{
	return 0;
}

static uint8_t vbts_set_bts(struct gsm_bts *bts)
{
	struct gsm_bts_trx *trx;
	uint8_t tn;

	llist_for_each_entry(trx, &bts->trx_list, list) {
		oml_mo_state_chg(&trx->mo, NM_OPSTATE_DISABLED, NM_AVSTATE_OK);
		oml_mo_state_chg(&trx->bb_transc.mo, -1, NM_AVSTATE_OK);

		for (tn = 0; tn < TRX_NR_TS; tn++)
			oml_mo_state_chg(&trx->ts[tn].mo, NM_OPSTATE_DISABLED, NM_AVSTATE_DEPENDENCY);

		/* report availability of trx to the bts. this will trigger the rsl connection */
		oml_mo_tx_sw_act_rep(&trx->mo);
		oml_mo_tx_sw_act_rep(&trx->bb_transc.mo);
	}
	return 0;
}

static uint8_t vbts_set_trx(struct gsm_bts_trx *trx)
{
	return 0;
}

static uint8_t vbts_set_ts(struct gsm_bts_trx_ts *ts)
{
	struct phy_instance *pinst = trx_phy_instance(ts->trx);
	int rc;

	rc = trx_sched_set_pchan(&pinst->u.virt.sched, ts->nr, ts->pchan);
	if (rc)
		return NM_NACK_RES_NOTAVAIL;

	return 0;
}

int bts_model_apply_oml(struct gsm_bts *bts, struct msgb *msg,
			struct tlv_parsed *new_attr, int kind, void *obj)
{
	struct abis_om_fom_hdr *foh = msgb_l3(msg);
	int cause = 0;

	switch (foh->msg_type) {
	case NM_MT_SET_BTS_ATTR:
		cause = vbts_set_bts(obj);
		break;
	case NM_MT_SET_RADIO_ATTR:
		cause = vbts_set_trx(obj);
		break;
	case NM_MT_SET_CHAN_ATTR:
		cause = vbts_set_ts(obj);
		break;
	}
	return oml_fom_ack_nack(msg, cause);
}

/* MO: TS 12.21 Managed Object */
int bts_model_opstart(struct gsm_bts *bts, struct gsm_abis_mo *mo, void *obj)
{
	int rc;

	switch (mo->obj_class) {
	case NM_OC_RADIO_CARRIER:
	case NM_OC_CHANNEL:
	case NM_OC_SITE_MANAGER:
	case NM_OC_BASEB_TRANSC:
	case NM_OC_BTS:
		oml_mo_state_chg(mo, NM_OPSTATE_ENABLED, NM_AVSTATE_OK);
		rc = oml_mo_opstart_ack(mo);
		break;
		/* TODO: gprs support */
	case NM_OC_GPRS_NSE:
	case NM_OC_GPRS_CELL:
	case NM_OC_GPRS_NSVC:
	default:
		rc = oml_mo_opstart_nack(mo, NM_NACK_OBJCLASS_NOTSUPP);
	}
	return rc;
}

int bts_model_chg_adm_state(struct gsm_bts *bts, struct gsm_abis_mo *mo,
			    void *obj, uint8_t adm_state)
{
	mo->nm_state.administrative = adm_state;
	return oml_mo_statechg_ack(mo);
}

int bts_model_trx_deact_rf(struct gsm_bts_trx *trx)
{
	return 0;
}


int bts_model_change_power(struct gsm_bts_trx *trx, int p_trxout_mdBm)
{
	return 0;
}

int bts_model_ctrl_cmds_install(struct gsm_bts *bts)
{
	return 0;
}
