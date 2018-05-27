/*
 * Licensed to the OpenAirInterface (OAI) Software Alliance under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.
 * The OpenAirInterface Software Alliance licenses this file to You under
 * the OAI Public License, Version 1.1  (the "License"); you may not use this file
 * except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.openairinterface.org/?page_id=698
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *-------------------------------------------------------------------------------
 * For more information about the OpenAirInterface (OAI) Software Alliance:
 *      contact@openairinterface.org
 */

/*! \file config.c
 * \brief UE and eNB configuration performed by RRC or as a consequence of RRC procedures
 * \author  Navid Nikaein and Raymond Knopp
 * \date 2010 - 2014
 * \version 0.1
 * \email: navid.nikaein@eurecom.fr
 * @ingroup _mac

 */

#include "COMMON/platform_types.h"
#include "COMMON/platform_constants.h"
#include "SCHED/defs.h"
#include "SystemInformationBlockType2.h"
//#include "RadioResourceConfigCommonSIB.h"
#include "RadioResourceConfigDedicated.h"
#ifdef Rel14
#include "PRACH-ConfigSIB-v1310.h"
#endif
#include "MeasGapConfig.h"
#include "MeasObjectToAddModList.h"
#include "TDD-Config.h"
#include "MAC-MainConfig.h"
#include "defs.h"
#include "proto.h"
#include "extern.h"
#include "UTIL/LOG/log.h"
#include "UTIL/LOG/vcd_signal_dumper.h"

#include "common/ran_context.h"
#if defined(Rel10) || defined(Rel14)
#include "MBSFN-AreaInfoList-r9.h"
#include "MBSFN-AreaInfo-r9.h"
#include "MBSFN-SubframeConfigList.h"
#include "PMCH-InfoList-r9.h"
#endif

extern void mac_init_cell_params(int Mod_idP,int CC_idP);
extern void phy_reset_ue(module_id_t Mod_id,uint8_t CC_id,uint8_t eNB_index);
extern uint32_t taus(void);

/* sec 5.9, 36.321: MAC Reset Procedure */
void ue_mac_reset(module_id_t module_idP, uint8_t eNB_index)
{

  //Resetting Bj
  UE_mac_inst[module_idP].scheduling_info.Bj[0] = 0;
  UE_mac_inst[module_idP].scheduling_info.Bj[1] = 0;
  UE_mac_inst[module_idP].scheduling_info.Bj[2] = 0;

  //Stopping all timers

  //timeAlignmentTimer expires

  // PHY changes for UE MAC reset
  phy_reset_ue(module_idP, 0, eNB_index);

  // notify RRC to relase PUCCH/SRS
  // cancel all pending SRs
  UE_mac_inst[module_idP].scheduling_info.SR_pending = 0;
  UE_mac_inst[module_idP].scheduling_info.SR_COUNTER = 0;

  //Set BSR Trigger Bmp and remove timer flags
  UE_mac_inst[module_idP].BSR_reporting_active = BSR_TRIGGER_NONE;

  // stop ongoing RACH procedure

  // discard explicitly signaled ra_PreambleIndex and ra_RACH_MaskIndex, if any
  UE_mac_inst[module_idP].RA_prach_resources.ra_PreambleIndex = 0;	// check!
  UE_mac_inst[module_idP].RA_prach_resources.ra_RACH_MaskIndex = 0;


  ue_init_mac(module_idP);	//This will hopefully do the rest of the MAC reset procedure

}

#ifdef Rel14
const uint32_t SC_Period[10] = {40,60,70,80,120,140,160,240,280,320};
const uint32_t SubframeBitmapSL[7] = {4,8,12,16,30,40,42};
#endif

int
rrc_mac_config_req_ue(module_id_t Mod_idP,
		      int CC_idP,
		      uint8_t eNB_index,
		      RadioResourceConfigCommonSIB_t *
		      radioResourceConfigCommon,
		      struct PhysicalConfigDedicated
		      *physicalConfigDedicated,
#if defined(Rel10) || defined(Rel14)
		      SCellToAddMod_r10_t * sCellToAddMod_r10,
		      //struct PhysicalConfigDedicatedSCell_r10 *physicalConfigDedicatedSCell_r10,
#endif
		      MeasObjectToAddMod_t ** measObj,
		      MAC_MainConfig_t * mac_MainConfig,
		      long logicalChannelIdentity,
		      LogicalChannelConfig_t * logicalChannelConfig,
		      MeasGapConfig_t * measGapConfig,
		      TDD_Config_t * tdd_Config,
		      MobilityControlInfo_t * mobilityControlInfo,
		      uint8_t * SIwindowsize,
		      uint16_t * SIperiod,
		      ARFCN_ValueEUTRA_t * ul_CarrierFreq,
		      long *ul_Bandwidth,
		      AdditionalSpectrumEmission_t *
		      additionalSpectrumEmission,
		      struct MBSFN_SubframeConfigList
		      *mbsfn_SubframeConfigList
#if defined(Rel10) || defined(Rel14)
		      , uint8_t MBMS_Flag,
		      MBSFN_AreaInfoList_r9_t * mbsfn_AreaInfoList,
		      PMCH_InfoList_r9_t * pmch_InfoList
#endif
#ifdef CBA
		      , uint8_t num_active_cba_groups, uint16_t cba_rnti
#endif
#if defined(Rel14)
		      ,config_action_t config_action,
		      const uint32_t * const sourceL2Id,
		      const uint32_t * const destinationL2Id,
                      const uint32_t * const groupL2Id,
		      SL_Preconfiguration_r12_t *SL_Preconfiguration_r12,
		      uint32_t directFrameNumber_r12,
		      long directSubframeNumber_r12,
		      long *sl_Bandwidth_r12
#endif
		      )
{

  int i;

  VCD_SIGNAL_DUMPER_DUMP_FUNCTION_BY_NAME
    (VCD_SIGNAL_DUMPER_FUNCTIONS_RRC_MAC_CONFIG, VCD_FUNCTION_IN);

  LOG_D(MAC, "[CONFIG][UE %d] Configuring MAC/PHY from eNB %d\n",
	Mod_idP, eNB_index);

  if (tdd_Config != NULL) {
    UE_mac_inst[Mod_idP].tdd_Config = tdd_Config;
  }


  if (tdd_Config && SIwindowsize && SIperiod) {
    phy_config_sib1_ue(Mod_idP, 0, eNB_index, tdd_Config,
		       *SIwindowsize, *SIperiod);
  }

  if (radioResourceConfigCommon != NULL) {
    UE_mac_inst[Mod_idP].radioResourceConfigCommon =
      radioResourceConfigCommon;
    phy_config_sib2_ue(Mod_idP, 0, eNB_index,
		       radioResourceConfigCommon, ul_CarrierFreq,
		       ul_Bandwidth, additionalSpectrumEmission,
		       mbsfn_SubframeConfigList);
  }
  // SRB2_lchan_config->choice.explicitValue.ul_SpecificParameters->logicalChannelGroup
  if (logicalChannelConfig != NULL) {
    LOG_I(MAC,
	  "[CONFIG][UE %d] Applying RRC logicalChannelConfig from eNB%d\n",
	  Mod_idP, eNB_index);
    UE_mac_inst[Mod_idP].logicalChannelConfig[logicalChannelIdentity] =
      logicalChannelConfig;
    UE_mac_inst[Mod_idP].scheduling_info.Bj[logicalChannelIdentity] = 0;	// initilize the bucket for this lcid

    AssertFatal(logicalChannelConfig->ul_SpecificParameters != NULL,
		"[UE %d] LCID %ld NULL ul_SpecificParameters\n",
		Mod_idP, logicalChannelIdentity);
    UE_mac_inst[Mod_idP].scheduling_info.bucket_size[logicalChannelIdentity] = logicalChannelConfig->ul_SpecificParameters->prioritisedBitRate * logicalChannelConfig->ul_SpecificParameters->bucketSizeDuration;	// set the max bucket size
    if (logicalChannelConfig->ul_SpecificParameters->
	logicalChannelGroup != NULL) {
      UE_mac_inst[Mod_idP].scheduling_info.
	LCGID[logicalChannelIdentity] =
	*logicalChannelConfig->ul_SpecificParameters->
	logicalChannelGroup;
      LOG_D(MAC,
	    "[CONFIG][UE %d] LCID %ld is attached to the LCGID %ld\n",
	    Mod_idP, logicalChannelIdentity,
	    *logicalChannelConfig->
	    ul_SpecificParameters->logicalChannelGroup);
    } else {
      UE_mac_inst[Mod_idP].scheduling_info.
	LCGID[logicalChannelIdentity] = MAX_NUM_LCGID;
    }
    UE_mac_inst[Mod_idP].
      scheduling_info.LCID_buffer_remain[logicalChannelIdentity] = 0;
  }

  if (mac_MainConfig != NULL) {
    LOG_I(MAC,
	  "[CONFIG][UE%d] Applying RRC macMainConfig from eNB%d\n",
	  Mod_idP, eNB_index);
    UE_mac_inst[Mod_idP].macConfig = mac_MainConfig;
    UE_mac_inst[Mod_idP].measGapConfig = measGapConfig;

    if (mac_MainConfig->ul_SCH_Config) {

      if (mac_MainConfig->ul_SCH_Config->periodicBSR_Timer) {
	UE_mac_inst[Mod_idP].scheduling_info.periodicBSR_Timer =
	  (uint16_t) *
	  mac_MainConfig->ul_SCH_Config->periodicBSR_Timer;
      } else {
	UE_mac_inst[Mod_idP].scheduling_info.periodicBSR_Timer =
#ifndef Rel14
	  (uint16_t)
	  MAC_MainConfig__ul_SCH_Config__periodicBSR_Timer_infinity
#else
	  (uint16_t) PeriodicBSR_Timer_r12_infinity;
#endif
	;
      }

      if (mac_MainConfig->ul_SCH_Config->maxHARQ_Tx) {
	UE_mac_inst[Mod_idP].scheduling_info.maxHARQ_Tx =
	  (uint16_t) * mac_MainConfig->ul_SCH_Config->maxHARQ_Tx;
      } else {
	UE_mac_inst[Mod_idP].scheduling_info.maxHARQ_Tx =
	  (uint16_t)
	  MAC_MainConfig__ul_SCH_Config__maxHARQ_Tx_n5;
      }
      phy_config_harq_ue(Mod_idP, 0, eNB_index,
			 UE_mac_inst[Mod_idP].
			 scheduling_info.maxHARQ_Tx);

      if (mac_MainConfig->ul_SCH_Config->retxBSR_Timer) {
	UE_mac_inst[Mod_idP].scheduling_info.retxBSR_Timer =
	  (uint16_t) mac_MainConfig->ul_SCH_Config->
	  retxBSR_Timer;
      } else {
#ifndef Rel14
	UE_mac_inst[Mod_idP].scheduling_info.retxBSR_Timer =
	  (uint16_t)
	  MAC_MainConfig__ul_SCH_Config__retxBSR_Timer_sf2560;
#else
	UE_mac_inst[Mod_idP].scheduling_info.retxBSR_Timer =
	  (uint16_t) RetxBSR_Timer_r12_sf2560;
#endif
      }
    }
#if defined(Rel10) || defined(Rel14)

    if (mac_MainConfig->ext1
	&& mac_MainConfig->ext1->sr_ProhibitTimer_r9) {
      UE_mac_inst[Mod_idP].scheduling_info.sr_ProhibitTimer =
	(uint16_t) * mac_MainConfig->ext1->sr_ProhibitTimer_r9;
    } else {
      UE_mac_inst[Mod_idP].scheduling_info.sr_ProhibitTimer = 0;
    }

    if (mac_MainConfig->ext2
	&& mac_MainConfig->ext2->mac_MainConfig_v1020) {
      if (mac_MainConfig->ext2->
	  mac_MainConfig_v1020->extendedBSR_Sizes_r10) {
	UE_mac_inst[Mod_idP].scheduling_info.
	  extendedBSR_Sizes_r10 =
	  (uint16_t) *
	  mac_MainConfig->ext2->
	  mac_MainConfig_v1020->extendedBSR_Sizes_r10;
      } else {
	UE_mac_inst[Mod_idP].scheduling_info.
	  extendedBSR_Sizes_r10 = (uint16_t) 0;
      }
      if (mac_MainConfig->ext2->mac_MainConfig_v1020->
	  extendedPHR_r10) {
	UE_mac_inst[Mod_idP].scheduling_info.extendedPHR_r10 =
	  (uint16_t) *
	  mac_MainConfig->ext2->mac_MainConfig_v1020->
	  extendedPHR_r10;
      } else {
	UE_mac_inst[Mod_idP].scheduling_info.extendedPHR_r10 =
	  (uint16_t) 0;
      }
    } else {
      UE_mac_inst[Mod_idP].scheduling_info.extendedBSR_Sizes_r10 =
	(uint16_t) 0;
      UE_mac_inst[Mod_idP].scheduling_info.extendedPHR_r10 =
	(uint16_t) 0;
    }
#endif
    UE_mac_inst[Mod_idP].scheduling_info.periodicBSR_SF =
      MAC_UE_BSR_TIMER_NOT_RUNNING;
    UE_mac_inst[Mod_idP].scheduling_info.retxBSR_SF =
      MAC_UE_BSR_TIMER_NOT_RUNNING;

    UE_mac_inst[Mod_idP].BSR_reporting_active = BSR_TRIGGER_NONE;

    LOG_D(MAC, "[UE %d]: periodic BSR %d (SF), retx BSR %d (SF)\n",
	  Mod_idP,
	  UE_mac_inst[Mod_idP].scheduling_info.periodicBSR_SF,
	  UE_mac_inst[Mod_idP].scheduling_info.retxBSR_SF);

    UE_mac_inst[Mod_idP].scheduling_info.drx_config =
      mac_MainConfig->drx_Config;
    UE_mac_inst[Mod_idP].scheduling_info.phr_config =
      mac_MainConfig->phr_Config;

    if (mac_MainConfig->phr_Config) {
      UE_mac_inst[Mod_idP].PHR_state =
	mac_MainConfig->phr_Config->present;
      UE_mac_inst[Mod_idP].PHR_reconfigured = 1;
      UE_mac_inst[Mod_idP].scheduling_info.periodicPHR_Timer =
	mac_MainConfig->phr_Config->choice.setup.periodicPHR_Timer;
      UE_mac_inst[Mod_idP].scheduling_info.prohibitPHR_Timer =
	mac_MainConfig->phr_Config->choice.setup.prohibitPHR_Timer;
      UE_mac_inst[Mod_idP].scheduling_info.PathlossChange =
	mac_MainConfig->phr_Config->choice.setup.dl_PathlossChange;
    } else {
      UE_mac_inst[Mod_idP].PHR_reconfigured = 0;
      UE_mac_inst[Mod_idP].PHR_state =
	MAC_MainConfig__phr_Config_PR_setup;
      UE_mac_inst[Mod_idP].scheduling_info.periodicPHR_Timer =
	MAC_MainConfig__phr_Config__setup__periodicPHR_Timer_sf20;
      UE_mac_inst[Mod_idP].scheduling_info.prohibitPHR_Timer =
	MAC_MainConfig__phr_Config__setup__prohibitPHR_Timer_sf20;
      UE_mac_inst[Mod_idP].scheduling_info.PathlossChange =
	MAC_MainConfig__phr_Config__setup__dl_PathlossChange_dB1;
    }

    UE_mac_inst[Mod_idP].scheduling_info.periodicPHR_SF =
      get_sf_perioidicPHR_Timer(UE_mac_inst[Mod_idP].
				scheduling_info.periodicPHR_Timer);
    UE_mac_inst[Mod_idP].scheduling_info.prohibitPHR_SF =
      get_sf_prohibitPHR_Timer(UE_mac_inst[Mod_idP].
			       scheduling_info.prohibitPHR_Timer);
    UE_mac_inst[Mod_idP].scheduling_info.PathlossChange_db =
      get_db_dl_PathlossChange(UE_mac_inst[Mod_idP].
			       scheduling_info.PathlossChange);
    UE_mac_inst[Mod_idP].PHR_reporting_active = 0;
    LOG_D(MAC,
	  "[UE %d] config PHR (%d): periodic %d (SF) prohibit %d (SF)  pathlosschange %d (db) \n",
	  Mod_idP,
	  (mac_MainConfig->phr_Config) ? mac_MainConfig->
	  phr_Config->present : -1,
	  UE_mac_inst[Mod_idP].scheduling_info.periodicPHR_SF,
	  UE_mac_inst[Mod_idP].scheduling_info.prohibitPHR_SF,
	  UE_mac_inst[Mod_idP].scheduling_info.PathlossChange_db);
  }


  if (physicalConfigDedicated != NULL) {
    phy_config_dedicated_ue(Mod_idP, 0, eNB_index,
			    physicalConfigDedicated);
    UE_mac_inst[Mod_idP].physicalConfigDedicated = physicalConfigDedicated;	// for SR proc
  }
#if defined(Rel10) || defined(Rel14)

  if (sCellToAddMod_r10 != NULL) {


    phy_config_dedicated_scell_ue(Mod_idP, eNB_index,
				  sCellToAddMod_r10, 1);
    UE_mac_inst[Mod_idP].physicalConfigDedicatedSCell_r10 = sCellToAddMod_r10->radioResourceConfigDedicatedSCell_r10->physicalConfigDedicatedSCell_r10;	// using SCell index 0
  }
#endif

  if (measObj != NULL) {
    if (measObj[0] != NULL) {
      UE_mac_inst[Mod_idP].n_adj_cells =
	measObj[0]->measObject.choice.
	measObjectEUTRA.cellsToAddModList->list.count;
      LOG_I(MAC, "Number of adjacent cells %d\n",
	    UE_mac_inst[Mod_idP].n_adj_cells);

      for (i = 0; i < UE_mac_inst[Mod_idP].n_adj_cells; i++) {
	UE_mac_inst[Mod_idP].adj_cell_id[i] =
	  measObj[0]->measObject.choice.
	  measObjectEUTRA.cellsToAddModList->list.array[i]->
	  physCellId;
	LOG_I(MAC, "Cell %d : Nid_cell %d\n", i,
	      UE_mac_inst[Mod_idP].adj_cell_id[i]);
      }

      phy_config_meas_ue(Mod_idP, 0, eNB_index,
			 UE_mac_inst[Mod_idP].n_adj_cells,
			 UE_mac_inst[Mod_idP].adj_cell_id);
    }
  }


  if (mobilityControlInfo != NULL) {

    LOG_D(MAC, "[UE%d] MAC Reset procedure triggered by RRC eNB %d \n",
	  Mod_idP, eNB_index);
    ue_mac_reset(Mod_idP, eNB_index);

    if (mobilityControlInfo->radioResourceConfigCommon.
	rach_ConfigCommon) {
      memcpy((void *)
	     &UE_mac_inst[Mod_idP].radioResourceConfigCommon->
	     rach_ConfigCommon,
	     (void *) mobilityControlInfo->
	     radioResourceConfigCommon.rach_ConfigCommon,
	     sizeof(RACH_ConfigCommon_t));
    }

    memcpy((void *) &UE_mac_inst[Mod_idP].
	   radioResourceConfigCommon->prach_Config.prach_ConfigInfo,
	   (void *) mobilityControlInfo->
	   radioResourceConfigCommon.prach_Config.prach_ConfigInfo,
	   sizeof(PRACH_ConfigInfo_t));
    UE_mac_inst[Mod_idP].radioResourceConfigCommon->
      prach_Config.rootSequenceIndex =
      mobilityControlInfo->radioResourceConfigCommon.
      prach_Config.rootSequenceIndex;

    if (mobilityControlInfo->radioResourceConfigCommon.
	pdsch_ConfigCommon) {
      memcpy((void *)
	     &UE_mac_inst[Mod_idP].radioResourceConfigCommon->
	     pdsch_ConfigCommon,
	     (void *) mobilityControlInfo->
	     radioResourceConfigCommon.pdsch_ConfigCommon,
	     sizeof(PDSCH_ConfigCommon_t));
    }
    // not a pointer: mobilityControlInfo->radioResourceConfigCommon.pusch_ConfigCommon
    memcpy((void *) &UE_mac_inst[Mod_idP].
	   radioResourceConfigCommon->pusch_ConfigCommon,
	   (void *) &mobilityControlInfo->
	   radioResourceConfigCommon.pusch_ConfigCommon,
	   sizeof(PUSCH_ConfigCommon_t));

    if (mobilityControlInfo->radioResourceConfigCommon.phich_Config) {
      /* memcpy((void *)&UE_mac_inst[Mod_idP].radioResourceConfigCommon->phich_Config,
	 (void *)mobilityControlInfo->radioResourceConfigCommon.phich_Config,
	 sizeof(PHICH_Config_t)); */
    }

    if (mobilityControlInfo->radioResourceConfigCommon.
	pucch_ConfigCommon) {
      memcpy((void *)
	     &UE_mac_inst[Mod_idP].radioResourceConfigCommon->
	     pucch_ConfigCommon,
	     (void *) mobilityControlInfo->
	     radioResourceConfigCommon.pucch_ConfigCommon,
	     sizeof(PUCCH_ConfigCommon_t));
    }

    if (mobilityControlInfo->
	radioResourceConfigCommon.soundingRS_UL_ConfigCommon) {
      memcpy((void *)
	     &UE_mac_inst[Mod_idP].radioResourceConfigCommon->
	     soundingRS_UL_ConfigCommon,
	     (void *) mobilityControlInfo->
	     radioResourceConfigCommon.soundingRS_UL_ConfigCommon,
	     sizeof(SoundingRS_UL_ConfigCommon_t));
    }

    if (mobilityControlInfo->
	radioResourceConfigCommon.uplinkPowerControlCommon) {
      memcpy((void *)
	     &UE_mac_inst[Mod_idP].radioResourceConfigCommon->
	     uplinkPowerControlCommon,
	     (void *) mobilityControlInfo->
	     radioResourceConfigCommon.uplinkPowerControlCommon,
	     sizeof(UplinkPowerControlCommon_t));
    }
    //configure antennaInfoCommon somewhere here..
    if (mobilityControlInfo->radioResourceConfigCommon.p_Max) {
      //to be configured
    }

    if (mobilityControlInfo->radioResourceConfigCommon.tdd_Config) {
      UE_mac_inst[Mod_idP].tdd_Config =
	mobilityControlInfo->radioResourceConfigCommon.tdd_Config;
    }

    if (mobilityControlInfo->
	radioResourceConfigCommon.ul_CyclicPrefixLength) {
      memcpy((void *)
	     &UE_mac_inst[Mod_idP].radioResourceConfigCommon->
	     ul_CyclicPrefixLength,
	     (void *) mobilityControlInfo->
	     radioResourceConfigCommon.ul_CyclicPrefixLength,
	     sizeof(UL_CyclicPrefixLength_t));
    }
    // store the previous rnti in case of failure, and set thenew rnti
    UE_mac_inst[Mod_idP].crnti_before_ho = UE_mac_inst[Mod_idP].crnti;
    UE_mac_inst[Mod_idP].crnti =
      ((mobilityControlInfo->
	newUE_Identity.buf[0]) | (mobilityControlInfo->
				  newUE_Identity.buf[1] << 8));
    LOG_I(MAC, "[UE %d] Received new identity %x from %d\n", Mod_idP,
	  UE_mac_inst[Mod_idP].crnti, eNB_index);
    UE_mac_inst[Mod_idP].rach_ConfigDedicated =
      malloc(sizeof(*mobilityControlInfo->rach_ConfigDedicated));

    if (mobilityControlInfo->rach_ConfigDedicated) {
      memcpy((void *) UE_mac_inst[Mod_idP].rach_ConfigDedicated,
	     (void *) mobilityControlInfo->rach_ConfigDedicated,
	     sizeof(*mobilityControlInfo->rach_ConfigDedicated));
    }

    phy_config_afterHO_ue(Mod_idP, 0, eNB_index, mobilityControlInfo,
			  0);
  }


  if (mbsfn_SubframeConfigList != NULL) {
    LOG_I(MAC,
	  "[UE %d][CONFIG] Received %d subframe allocation pattern for MBSFN\n",
	  Mod_idP, mbsfn_SubframeConfigList->list.count);
    UE_mac_inst[Mod_idP].num_sf_allocation_pattern =
      mbsfn_SubframeConfigList->list.count;

    for (i = 0; i < mbsfn_SubframeConfigList->list.count; i++) {
      LOG_I(MAC,
	    "[UE %d] Configuring MBSFN_SubframeConfig %d from received SIB2 \n",
	    Mod_idP, i);
      UE_mac_inst[Mod_idP].mbsfn_SubframeConfig[i] =
	mbsfn_SubframeConfigList->list.array[i];
      //  LOG_I("[UE %d] MBSFN_SubframeConfig[%d] pattern is  %ld\n", Mod_idP,
      //    UE_mac_inst[Mod_idP].mbsfn_SubframeConfig[i]->subframeAllocation.choice.oneFrame.buf[0]);
    }
  }
#if defined(Rel10) || defined(Rel14)

  if (mbsfn_AreaInfoList != NULL) {
    LOG_I(MAC, "[UE %d][CONFIG] Received %d MBSFN Area Info\n",
	  Mod_idP, mbsfn_AreaInfoList->list.count);
    UE_mac_inst[Mod_idP].num_active_mbsfn_area =
      mbsfn_AreaInfoList->list.count;

    for (i = 0; i < mbsfn_AreaInfoList->list.count; i++) {
      UE_mac_inst[Mod_idP].mbsfn_AreaInfo[i] =
	mbsfn_AreaInfoList->list.array[i];
      LOG_I(MAC,
	    "[UE %d] MBSFN_AreaInfo[%d]: MCCH Repetition Period = %ld\n",
	    Mod_idP, i,
	    UE_mac_inst[Mod_idP].mbsfn_AreaInfo[i]->
	    mcch_Config_r9.mcch_RepetitionPeriod_r9);
      phy_config_sib13_ue(Mod_idP, 0, eNB_index, i,
			  UE_mac_inst[Mod_idP].
			  mbsfn_AreaInfo[i]->mbsfn_AreaId_r9);
    }
  }

  if (pmch_InfoList != NULL) {

    //    LOG_I(MAC,"DUY: lcid when entering rrc_mac config_req is %02d\n",(pmch_InfoList->list.array[0]->mbms_SessionInfoList_r9.list.array[0]->logicalChannelIdentity_r9));

    LOG_I(MAC, "[UE %d] Configuring PMCH_config from MCCH MESSAGE \n",
	  Mod_idP);

    for (i = 0; i < pmch_InfoList->list.count; i++) {
      UE_mac_inst[Mod_idP].pmch_Config[i] =
	&pmch_InfoList->list.array[i]->pmch_Config_r9;
      LOG_I(MAC, "[UE %d] PMCH[%d]: MCH_Scheduling_Period = %ld\n",
	    Mod_idP, i,
	    UE_mac_inst[Mod_idP].
	    pmch_Config[i]->mch_SchedulingPeriod_r9);
    }

    UE_mac_inst[Mod_idP].mcch_status = 1;
  }
#endif
#ifdef CBA

  if (cba_rnti) {
    UE_mac_inst[Mod_idP].cba_rnti[num_active_cba_groups - 1] =
      cba_rnti;
    LOG_D(MAC,
	  "[UE %d] configure CBA group %d RNTI %x for eNB %d (total active cba group %d)\n",
	  Mod_idP, Mod_idP % num_active_cba_groups, cba_rnti,
	  eNB_index, num_active_cba_groups);
    phy_config_cba_rnti(Mod_idP, CC_idP, eNB_flagP, eNB_index,
			cba_rnti, num_active_cba_groups - 1,
			num_active_cba_groups);
  }
#endif

//for D2D
#if defined(Rel10) || defined(Rel14)
  int j = 0;
  int k = 0;
  switch (config_action) {
  case CONFIG_ACTION_ADD:
     if (sourceL2Id){
        UE_mac_inst[Mod_idP].sourceL2Id = *sourceL2Id;
        LOG_I(MAC,"[UE %d] Configure source L2Id 0x%08x \n", Mod_idP, *sourceL2Id );
     }

     //store list of (S,D,G,LCID) for SL
     if ((logicalChannelIdentity > 0) && (logicalChannelIdentity < MAX_NUM_LCID_DATA)) {
        if (groupL2Id){
           LOG_I(MAC,"[UE %d] Configure group L2Id 0x%08x\n", Mod_idP, *groupL2Id );
           j = 0;
           k = 0;
           for (k = 0; k< MAX_NUM_LCID_DATA; k++) {
              if ((UE_mac_inst[Mod_idP].sl_info[k].LCID == 0) && (UE_mac_inst[Mod_idP].sl_info[k].destinationL2Id == 0) && (UE_mac_inst[Mod_idP].sl_info[k].groupL2Id == 0) && (j == 0)) j = k+1;

              if ((UE_mac_inst[Mod_idP].sl_info[k].groupL2Id == *groupL2Id) && (UE_mac_inst[Mod_idP].sl_info[k].LCID == 0 )) {
                 UE_mac_inst[Mod_idP].sl_info[k].LCID = logicalChannelIdentity;
                 break; //(LCID, G) already exists!
              }

              if ((UE_mac_inst[Mod_idP].sl_info[k].LCID == logicalChannelIdentity) && (UE_mac_inst[Mod_idP].sl_info[k].groupL2Id == *groupL2Id)) break; //(LCID, G) already exists!
           }
           if ((k == MAX_NUM_LCID_DATA) && (j > 0)) {
              UE_mac_inst[Mod_idP].sl_info[j-1].LCID = logicalChannelIdentity;
              UE_mac_inst[Mod_idP].sl_info[j-1].groupL2Id = *groupL2Id;
              UE_mac_inst[Mod_idP].sl_info[j-1].sourceL2Id = *sourceL2Id;
              UE_mac_inst[Mod_idP].numCommFlows++;

           }
           for (k = 0; k < MAX_NUM_LCID_DATA; k++) {
              LOG_I(MAC,"[UE %d] logical channel %d channel id %d, groupL2Id %d\n", Mod_idP,k,UE_mac_inst[Mod_idP].sl_info[k].LCID, UE_mac_inst[Mod_idP].sl_info[k].groupL2Id );
           }
        }
        if (destinationL2Id){
           LOG_I(MAC,"[UE %d] Configure destination L2Id 0x%08x\n", Mod_idP, *destinationL2Id );
           j = 0;
           k = 0;
           for (k = 0; k< MAX_NUM_LCID_DATA; k++) {
              if ((UE_mac_inst[Mod_idP].sl_info[k].LCID == 0) &&  (UE_mac_inst[Mod_idP].sl_info[k].destinationL2Id == 0)  && (UE_mac_inst[Mod_idP].sl_info[k].groupL2Id == 0) && (j == 0)) j = k+1;
              if ((UE_mac_inst[Mod_idP].sl_info[k].destinationL2Id == *destinationL2Id) && (UE_mac_inst[Mod_idP].sl_info[k].LCID == 0 )) {
                 UE_mac_inst[Mod_idP].sl_info[k].LCID = logicalChannelIdentity;
                 break; //(LCID, D) already exists!
              }
              if ((UE_mac_inst[Mod_idP].sl_info[k].LCID == logicalChannelIdentity) && (UE_mac_inst[Mod_idP].sl_info[k].destinationL2Id == *destinationL2Id)) break; //(LCID, D) already exists!
           }
           if ((k == MAX_NUM_LCID_DATA) && (j > 0)) {
              UE_mac_inst[Mod_idP].sl_info[j-1].LCID = logicalChannelIdentity;
              UE_mac_inst[Mod_idP].sl_info[j-1].destinationL2Id = *destinationL2Id;
              UE_mac_inst[Mod_idP].sl_info[j-1].sourceL2Id = *sourceL2Id;
              UE_mac_inst[Mod_idP].numCommFlows++;

           }
           for (k = 0; k < MAX_NUM_LCID_DATA; k++) {
              LOG_I(MAC,"[UE %d] logical channel %d channel id %d, destinationL2Id %d\n", Mod_idP,k,UE_mac_inst[Mod_idP].sl_info[k].LCID, UE_mac_inst[Mod_idP].sl_info[k].destinationL2Id);
           }
        }
     } else if ((logicalChannelIdentity >= MAX_NUM_LCID_DATA) && (logicalChannelIdentity < MAX_NUM_LCID)) {
        LOG_I(MAC,"[UE %d] Configure LCID %d  for PC5S\n", Mod_idP, logicalChannelIdentity );
        j = 0;
        k = 0;
        for (k = MAX_NUM_LCID_DATA; k < MAX_NUM_LCID; k++) {
           if ((UE_mac_inst[Mod_idP].sl_info[k].LCID == 0) && (UE_mac_inst[Mod_idP].sl_info[k].destinationL2Id == 0) && (UE_mac_inst[Mod_idP].sl_info[k].groupL2Id == 0) && (j == 0)) j = k+1;
           if (destinationL2Id){
              if ((UE_mac_inst[Mod_idP].sl_info[k].LCID == 0) && (UE_mac_inst[Mod_idP].sl_info[k].destinationL2Id == *destinationL2Id )) {
                 UE_mac_inst[Mod_idP].sl_info[k].LCID = logicalChannelIdentity;
                 break;
              }
           }
           if ((UE_mac_inst[Mod_idP].sl_info[k].LCID == logicalChannelIdentity)) break;
           //&& (UE_mac_inst[Mod_idP].sl_info[k].destinationL2Id == *destinationL2Id)) break; //(LCID, D) already exists!
        }
        if ((k == MAX_NUM_LCID) && (j > 0)) {
           UE_mac_inst[Mod_idP].sl_info[j-1].LCID = logicalChannelIdentity;
           if (destinationL2Id) UE_mac_inst[Mod_idP].sl_info[j-1].destinationL2Id = *destinationL2Id;
           UE_mac_inst[Mod_idP].sl_info[j-1].sourceL2Id = *sourceL2Id;
           UE_mac_inst[Mod_idP].numCommFlows++;

        }
        for (k = MAX_NUM_LCID_DATA; k < MAX_NUM_LCID; k++) {
           LOG_I(MAC,"[UE %d] logical channel %d channel id %d, destinationL2Id %d\n", Mod_idP,k,UE_mac_inst[Mod_idP].sl_info[k].LCID, UE_mac_inst[Mod_idP].sl_info[k].destinationL2Id);
        }
     }


     break;
  case CONFIG_ACTION_REMOVE:
     // OK for the moment since LCID is unique per flow
     if ((logicalChannelIdentity > 0) && (logicalChannelIdentity < MAX_NUM_LCID_DATA)) {
        LOG_I(MAC,"[UE %d] Remove (logicalChannelIdentity %d)\n", Mod_idP, logicalChannelIdentity );
        k = 0;
        for (k = 0; k < MAX_NUM_LCID_DATA; k++) {
           if (UE_mac_inst[Mod_idP].sl_info[k].LCID == logicalChannelIdentity) {
              UE_mac_inst[Mod_idP].sl_info[k].LCID = 0;
              //UE_mac_inst[Mod_idP].sl_info[k].destinationL2Id = 0;
              //UE_mac_inst[Mod_idP].sl_info[k].groupL2Id = 0;
              UE_mac_inst[Mod_idP].numCommFlows--;
              break;
           }
        }

        for (k = 0; k < MAX_NUM_LCID_DATA; k++) {
           LOG_I(MAC,"[UE %d] channel id %d, destinationL2Id %d, groupL2Id %d\n", Mod_idP, UE_mac_inst[Mod_idP].sl_info[k].LCID, UE_mac_inst[Mod_idP].sl_info[k].destinationL2Id, UE_mac_inst[Mod_idP].sl_info[k].groupL2Id);
        }
     } else if ((logicalChannelIdentity >= MAX_NUM_LCID_DATA) && (logicalChannelIdentity < MAX_NUM_LCID)) {
        //remove RBID for PCS5
        LOG_I(MAC,"[UE %d] Remove (logicalChannelIdentity %d)\n", Mod_idP, logicalChannelIdentity );
        k = 0;
        for (k = MAX_NUM_LCID_DATA; k < MAX_NUM_LCID; k++) {
           if (UE_mac_inst[Mod_idP].sl_info[k].LCID == logicalChannelIdentity) {
              UE_mac_inst[Mod_idP].sl_info[k].LCID = 0;
              //UE_mac_inst[Mod_idP].sl_info[k].destinationL2Id = 0;
              //UE_mac_inst[Mod_idP].sl_info[k].groupL2Id = 0;
              UE_mac_inst[Mod_idP].numCommFlows--;
              break;
           }
        }

        for (k = MAX_NUM_LCID_DATA; k < MAX_NUM_LCID; k++) {
           LOG_I(MAC,"[UE %d] channel id %d, destinationL2Id %d, groupL2Id %d\n", Mod_idP, UE_mac_inst[Mod_idP].sl_info[k].LCID, UE_mac_inst[Mod_idP].sl_info[k].destinationL2Id, UE_mac_inst[Mod_idP].sl_info[k].groupL2Id);
        }
     }

     break;

  default:
     break;
  }

// SL Preconfiguration

  if (SL_Preconfiguration_r12){


    LOG_I(MAC,"Getting SL parameters\n");

    // SLSS

    UE_mac_inst[Mod_idP].slss.SL_OffsetIndicator   = SL_Preconfiguration_r12->preconfigSync_r12.syncOffsetIndicator1_r12;
    // Note: Other synch parameters are ignored for now
    UE_mac_inst[Mod_idP].slss.slss_id              = 170;//+(taus()%168);
    // PSCCH
    struct SL_PreconfigCommPool_r12 *preconfigpool = SL_Preconfiguration_r12->preconfigComm_r12.list.array[0];
    UE_mac_inst[Mod_idP].slsch.N_SL_RB_SC                = preconfigpool->sc_TF_ResourceConfig_r12.prb_Num_r12;
    UE_mac_inst[Mod_idP].slsch.prb_Start_SC              = preconfigpool->sc_TF_ResourceConfig_r12.prb_Start_r12;
    UE_mac_inst[Mod_idP].slsch.prb_End_SC                = preconfigpool->sc_TF_ResourceConfig_r12.prb_End_r12;
    UE_mac_inst[Mod_idP].slsch.N_SL_RB_data              = preconfigpool->data_TF_ResourceConfig_r12.prb_Num_r12;
    UE_mac_inst[Mod_idP].slsch.prb_Start_data            = preconfigpool->data_TF_ResourceConfig_r12.prb_Start_r12;
    UE_mac_inst[Mod_idP].slsch.prb_End_data              = preconfigpool->data_TF_ResourceConfig_r12.prb_End_r12;
    AssertFatal(preconfigpool->sc_Period_r12<10,"Maximum supported sc_Period is 320ms (sc_Period_r12=%d)\n",
		SL_PeriodComm_r12_sf320);
    UE_mac_inst[Mod_idP].slsch.SL_SC_Period = SC_Period[preconfigpool->sc_Period_r12];
    AssertFatal(preconfigpool->sc_TF_ResourceConfig_r12.offsetIndicator_r12.present == SL_OffsetIndicator_r12_PR_small_r12,
		"offsetIndicator is limited to smaller format\n");

    UE_mac_inst[Mod_idP].slsch.SL_OffsetIndicator      = preconfigpool->sc_TF_ResourceConfig_r12.offsetIndicator_r12.choice.small_r12;
    UE_mac_inst[Mod_idP].slsch.SL_OffsetIndicator_data = preconfigpool->data_TF_ResourceConfig_r12.offsetIndicator_r12.choice.small_r12;
    AssertFatal(preconfigpool->sc_TF_ResourceConfig_r12.subframeBitmap_r12.present <= SubframeBitmapSL_r12_PR_bs40_r12 ||
		preconfigpool->sc_TF_ResourceConfig_r12.subframeBitmap_r12.present > SubframeBitmapSL_r12_PR_NOTHING,
		"PSCCH bitmap limited to 42 bits\n");
    UE_mac_inst[Mod_idP].slsch.SubframeBitmapSL_length = SubframeBitmapSL[preconfigpool->sc_TF_ResourceConfig_r12.subframeBitmap_r12.present];
    UE_mac_inst[Mod_idP].slsch.bitmap1 = *((uint64_t*)preconfigpool->sc_TF_ResourceConfig_r12.subframeBitmap_r12.choice.bs40_r12.buf);

    AssertFatal(SL_Preconfiguration_r12->ext1!=NULL,"there is no Rel13 extension in SL preconfiguration\n"); 
    AssertFatal(SL_Preconfiguration_r12->ext1->preconfigDisc_r13!=NULL,"there is no SL discovery configuration\n");
    AssertFatal(SL_Preconfiguration_r12->ext1->preconfigDisc_r13->discRxPoolList_r13.list.count==1,"Discover RX pool list count %d != 1\n",
                SL_Preconfiguration_r12->ext1->preconfigDisc_r13->discRxPoolList_r13.list.count);
    SL_PreconfigDiscPool_r13_t *discrxpool=SL_Preconfiguration_r12->ext1->preconfigDisc_r13->discRxPoolList_r13.list.array[0];

  /// Discovery Type
    UE_mac_inst[Mod_idP].sldch.type     = disc_type1;
  /// Number of SL resource blocks (1-100)
    UE_mac_inst[Mod_idP].sldch.N_SL_RB = discrxpool->tf_ResourceConfig_r13.prb_Num_r12;
  /// prb-start (0-99)
    UE_mac_inst[Mod_idP].sldch.prb_Start= discrxpool->tf_ResourceConfig_r13.prb_Start_r12;
  /// prb-End (0-99)
    UE_mac_inst[Mod_idP].sldch.prb_End = discrxpool->tf_ResourceConfig_r13.prb_End_r12;
  /// SL-OffsetIndicator (0-10239)
    AssertFatal(discrxpool->tf_ResourceConfig_r13.offsetIndicator_r12.present  == SL_OffsetIndicator_r12_PR_small_r12,
                "offsetIndicator_r12 is not PR_small_r12\n");

    UE_mac_inst[Mod_idP].sldch.offsetIndicator = discrxpool->tf_ResourceConfig_r13.offsetIndicator_r12.choice.small_r12 ;

    AssertFatal(discrxpool->tf_ResourceConfig_r13.subframeBitmap_r12.present >  SubframeBitmapSL_r12_PR_NOTHING && 
                discrxpool->tf_ResourceConfig_r13.subframeBitmap_r12.present <= SubframeBitmapSL_r12_PR_bs42_r12,
                "illegal subframeBitmap %d\n",discrxpool->tf_ResourceConfig_r13.subframeBitmap_r12.present);
  	 
  /// PSDCH subframe bitmap (up to 100 bits, first 64)
    switch (discrxpool->tf_ResourceConfig_r13.subframeBitmap_r12.present) {
          case SubframeBitmapSL_r12_PR_NOTHING:
           AssertFatal(1==0,"Should never get here\n");
           break;
  	  case SubframeBitmapSL_r12_PR_bs4_r12:
           UE_mac_inst[Mod_idP].sldch.bitmap1 = *(uint64_t*)discrxpool->tf_ResourceConfig_r13.subframeBitmap_r12.choice.bs4_r12.buf;
           UE_mac_inst[Mod_idP].sldch.bitmap_length = 4;
	   break;
          case SubframeBitmapSL_r12_PR_bs8_r12:
           UE_mac_inst[Mod_idP].sldch.bitmap1 = *(uint64_t*)discrxpool->tf_ResourceConfig_r13.subframeBitmap_r12.choice.bs8_r12.buf;
           UE_mac_inst[Mod_idP].sldch.bitmap_length = 9;
	  break;
          case SubframeBitmapSL_r12_PR_bs12_r12:
           UE_mac_inst[Mod_idP].sldch.bitmap1 = *(uint64_t*)discrxpool->tf_ResourceConfig_r13.subframeBitmap_r12.choice.bs12_r12.buf;
           UE_mac_inst[Mod_idP].sldch.bitmap_length = 12;
	  break;
          case SubframeBitmapSL_r12_PR_bs16_r12:
           UE_mac_inst[Mod_idP].sldch.bitmap1 = *(uint64_t*)discrxpool->tf_ResourceConfig_r13.subframeBitmap_r12.choice.bs16_r12.buf;
           UE_mac_inst[Mod_idP].sldch.bitmap_length = 16;
	  break;
          case SubframeBitmapSL_r12_PR_bs30_r12:
           UE_mac_inst[Mod_idP].sldch.bitmap1 = *(uint64_t*)discrxpool->tf_ResourceConfig_r13.subframeBitmap_r12.choice.bs30_r12.buf;
           UE_mac_inst[Mod_idP].sldch.bitmap_length = 30;
	  break;
          case SubframeBitmapSL_r12_PR_bs40_r12:
           UE_mac_inst[Mod_idP].sldch.bitmap1 = *(uint64_t*)discrxpool->tf_ResourceConfig_r13.subframeBitmap_r12.choice.bs40_r12.buf;
           UE_mac_inst[Mod_idP].sldch.bitmap_length = 40;
	  break;
          case SubframeBitmapSL_r12_PR_bs42_r12:
           UE_mac_inst[Mod_idP].sldch.bitmap1 = *(uint64_t*)discrxpool->tf_ResourceConfig_r13.subframeBitmap_r12.choice.bs42_r12.buf;
           UE_mac_inst[Mod_idP].sldch.bitmap_length = 42;
	  break;
    } 

  /// PSDCH subframe bitmap (up to 100 bits, second 36)
    UE_mac_inst[Mod_idP].sldch.bitmap2 = 0;

  /// SL-Discovery Period
    AssertFatal(SL_PreconfigDiscPool_r13__discPeriod_r13_spare == 15, "specifications have changed, update table\n");
    int sldisc_period[SL_PreconfigDiscPool_r13__discPeriod_r13_spare] = {4,6,7,8,12,14,16,24,28,32,64,128,256,512,1024};
    UE_mac_inst[Mod_idP].sldch.discPeriod = sldisc_period[discrxpool->discPeriod_r13];

  /// Number of Repetitions (N_R)
    UE_mac_inst[Mod_idP].sldch.numRepetitions = discrxpool->numRepetition_r13;
  /// Number of retransmissions (numRetx-r12)
    UE_mac_inst[Mod_idP].sldch.numRetx = discrxpool->numRetx_r13;

    phy_config_SL(Mod_idP,&UE_mac_inst[Mod_idP].sldch,&UE_mac_inst[Mod_idP].slsch);

  }
  if (directFrameNumber_r12<1025) UE_mac_inst[Mod_idP].directFrameNumber_r12     = directFrameNumber_r12;
  if (sl_Bandwidth_r12) UE_mac_inst[Mod_idP].sl_Bandwidth_r12        = *sl_Bandwidth_r12;
#endif

  VCD_SIGNAL_DUMPER_DUMP_FUNCTION_BY_NAME
    (VCD_SIGNAL_DUMPER_FUNCTIONS_RRC_MAC_CONFIG, VCD_FUNCTION_OUT);

  return (0);
}
