/*
 * Licensed to the OpenAirInterface (OAI) Software Alliance under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.
 * The OpenAirInterface Software Alliance licenses this file to You under
 * the OAI Public License, Version 1.0  (the "License"); you may not use this file
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

/*! \file lte-enb.c
 * \brief Top-level threads for eNodeB
 * \author R. Knopp, F. Kaltenberger, Navid Nikaein
 * \date 2012
 * \version 0.1
 * \company Eurecom
 * \email: knopp@eurecom.fr,florian.kaltenberger@eurecom.fr, navid.nikaein@eurecom.fr
 * \note
 * \warning
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <sched.h>
#include <linux/sched.h>
#include <signal.h>
#include <execinfo.h>
#include <getopt.h>
#include <sys/sysinfo.h>
#include "rt_wrapper.h"

//#include "time_utils.h"

#undef MALLOC //there are two conflicting definitions, so we better make sure we don't use it at all

#include "assertions.h"
#include "msc.h"

#include "PHY/types.h"

#undef MALLOC //there are two conflicting definitions, so we better make sure we don't use it at all
//#undef FRAME_LENGTH_COMPLEX_SAMPLES //there are two conflicting definitions, so we better make sure we don't use it at all

#include "../../ARCH/COMMON/common_lib.h"

//#undef FRAME_LENGTH_COMPLEX_SAMPLES //there are two conflicting definitions, so we better make sure we don't use it at all

#include "PHY/LTE_TRANSPORT/if4_tools.h"
#include "PHY/LTE_TRANSPORT/if5_tools.h"

#include "PHY/phy_extern.h"
//#include "SCHED/extern.h"
#include "SCHED/sched_eNB.h"

#include "../../SIMU/USER/init_lte.h"

//NB-IoT 
#include "PHY/defs_eNB.h"
#include "PHY/defs_L1_NB_IoT.h"
//#include "PHY/defs_L1_NB_IoT.h"
#include "SCHED_NBIOT/defs_NB_IoT.h"
#include "SCHED/sched_common.h" // for calling prach_procedures_NB_IoT()
#include "PHY_INTERFACE/IF_Module_NB_IoT.h"
#include "LAYER2/MAC/extern_NB_IoT.h"
#include "PHY/extern_NB_IoT.h"
#include "LAYER2/MAC/defs.h"
#include "PHY_INTERFACE/phy_interface_extern.h"
//#include "PHY/LTE_TRANSPORT/transport_proto.h"
#include "PHY/NBIoT_TRANSPORT/proto_NB_IoT.h"

#ifdef SMBV
#include "PHY/TOOLS/smbv.h"
unsigned short config_frames[4] = {2,9,11,13};
#endif
#include "UTIL/OTG/otg_tx.h"
#include "UTIL/OTG/otg_externs.h"
#include "UTIL/MATH/oml.h"
#include "common/utils/LOG/vcd_signal_dumper.h"
#include "UTIL/OPT/opt.h"
#include "enb_config.h"
//#include "PHY/TOOLS/time_meas.h"

#ifndef OPENAIR2
#include "UTIL/OTG/otg_extern.h"
#endif

#if defined(ENABLE_ITTI)
# if defined(ENABLE_USE_MME)
#   include "s1ap_eNB.h"
#ifdef PDCP_USE_NETLINK
#   include "SIMULATION/ETH_TRANSPORT/proto.h"
#endif
# endif
#endif

#include "T.h"

extern volatile int             start_eNB;
extern volatile int                    oai_exit;
extern int oaisim_flag;
extern openair0_config_t openair0_cfg[MAX_CARDS];

uint8_t seqno; //sequence number
static int                      time_offset[4] = {0,0,0,0};

static int recv_if_count = 0;

static struct {
  pthread_mutex_t  mutex_phy_proc_tx;
  pthread_cond_t   cond_phy_proc_tx;
  volatile uint8_t phy_proc_CC_id;
} sync_phy_proc;

struct timespec start_rf_new, start_rf_prev, start_rf_prev2, end_rf;
openair0_timestamp start_rf_new_ts, start_rf_prev_ts, start_rf_prev2_ts, end_rf_ts;

extern struct timespec start_fh, start_fh_prev;
extern int start_fh_sf, start_fh_prev_sf;
struct timespec end_fh;
int end_fh_sf;

void init_eNB_NB_IoT(eNB_func_NB_IoT_t node_function[], eNB_timing_NB_IoT_t node_timing[],int nb_inst,eth_params_t *,int,int);

/**********************************************************Other structure***************************************************************/

void do_OFDM_mod_rt_NB_IoT(int subframe,PHY_VARS_eNB_NB_IoT *phy_vars_eNB)
{

  int CC_id = phy_vars_eNB->proc.CC_id;
  unsigned int aa,slot_offset;
  //int dummy_tx_b[7680*4] __attribute__((aligned(32)));
  int i, tx_offset;
  //int slot_sizeF = (phy_vars_eNB->frame_parms.ofdm_symbol_size)* ((phy_vars_eNB->frame_parms.Ncp==1) ? 6 : 7);
  int len;
  //int slot_offset_F = (subframe<<1)*slot_sizeF;

  slot_offset = subframe*phy_vars_eNB->frame_parms.samples_per_tti;

  if ((subframe_select(&phy_vars_eNB->frame_parms,subframe)==SF_DL)||
      ((subframe_select(&phy_vars_eNB->frame_parms,subframe)==SF_S))) {
    //    LOG_D(HW,"Frame %d: Generating slot %d\n",frame,next_slot);

    VCD_SIGNAL_DUMPER_DUMP_FUNCTION_BY_NAME(VCD_SIGNAL_DUMPER_FUNCTIONS_ENB_OFDM_MODULATION,1);

    do_OFDM_mod_symbol(&phy_vars_eNB->common_vars,
           0,
           subframe<<1,
           &phy_vars_eNB->frame_parms,
           phy_vars_eNB->do_precoding);
 
    // if S-subframe generate first slot only 
    if (subframe_select(&phy_vars_eNB->frame_parms,subframe) == SF_DL) {
      do_OFDM_mod_symbol(&phy_vars_eNB->common_vars,
       0,
       1+(subframe<<1),
       &phy_vars_eNB->frame_parms,
       phy_vars_eNB->do_precoding);
    }

    VCD_SIGNAL_DUMPER_DUMP_FUNCTION_BY_NAME(VCD_SIGNAL_DUMPER_FUNCTIONS_ENB_OFDM_MODULATION,0);
 

    for (aa=0; aa<phy_vars_eNB->frame_parms.nb_antennas_tx; aa++) {
      // if S-subframe generate first slot only
      if (subframe_select(&phy_vars_eNB->frame_parms,subframe) == SF_S)
  len = phy_vars_eNB->frame_parms.samples_per_tti>>1;
      else
  len = phy_vars_eNB->frame_parms.samples_per_tti;

      for (i=0; i<len; i++) {
        tx_offset = (int)slot_offset+time_offset[aa]+i;

  
        if (tx_offset<0)
          tx_offset += LTE_NUMBER_OF_SUBFRAMES_PER_FRAME*phy_vars_eNB->frame_parms.samples_per_tti;

        if (tx_offset>=(LTE_NUMBER_OF_SUBFRAMES_PER_FRAME*phy_vars_eNB->frame_parms.samples_per_tti))
          tx_offset -= LTE_NUMBER_OF_SUBFRAMES_PER_FRAME*phy_vars_eNB->frame_parms.samples_per_tti;


  ((short*)&phy_vars_eNB->common_vars.txdata[0][aa][tx_offset])[0] = ((short*)&phy_vars_eNB->common_vars.txdata[0][aa][tx_offset])[0]<<openair0_cfg[CC_id].iq_txshift;
  
  ((short*)&phy_vars_eNB->common_vars.txdata[0][aa][tx_offset])[1] = ((short*)&phy_vars_eNB->common_vars.txdata[0][aa][tx_offset])[1]<<openair0_cfg[CC_id].iq_txshift;
     }
     // if S-subframe switch to RX in second subframe
     if (subframe_select(&phy_vars_eNB->frame_parms,subframe) == SF_S) {
       for (i=0; i<len; i++) {
   phy_vars_eNB->common_vars.txdata[0][aa][tx_offset++] = 0x00010001;
       }
     }

     if ((((phy_vars_eNB->frame_parms.tdd_config==0) ||
    (phy_vars_eNB->frame_parms.tdd_config==1) ||
    (phy_vars_eNB->frame_parms.tdd_config==2) ||
    (phy_vars_eNB->frame_parms.tdd_config==6)) && 
    (subframe==0)) || (subframe==5)) {
       // turn on tx switch N_TA_offset before
       //LOG_D(HW,"subframe %d, time to switch to tx (N_TA_offset %d, slot_offset %d) \n",subframe,phy_vars_eNB->N_TA_offset,slot_offset);
       for (i=0; i<phy_vars_eNB->N_TA_offset; i++) {
         tx_offset = (int)slot_offset+time_offset[aa]+i-phy_vars_eNB->N_TA_offset;
         if (tx_offset<0)
           tx_offset += LTE_NUMBER_OF_SUBFRAMES_PER_FRAME*phy_vars_eNB->frame_parms.samples_per_tti;
   
   if (tx_offset>=(LTE_NUMBER_OF_SUBFRAMES_PER_FRAME*phy_vars_eNB->frame_parms.samples_per_tti))
     tx_offset -= LTE_NUMBER_OF_SUBFRAMES_PER_FRAME*phy_vars_eNB->frame_parms.samples_per_tti;
   
   phy_vars_eNB->common_vars.txdata[0][aa][tx_offset] = 0x00000000;
       }
     }
    }
  }
  VCD_SIGNAL_DUMPER_DUMP_FUNCTION_BY_NAME(VCD_SIGNAL_DUMPER_FUNCTIONS_PHY_ENB_SFGEN , 0 );
}

void tx_fh_if5_NB_IoT(PHY_VARS_eNB_NB_IoT *eNB,L1_rxtx_proc_t *proc) {
  VCD_SIGNAL_DUMPER_DUMP_VARIABLE_BY_NAME( VCD_SIGNAL_DUMPER_VARIABLES_TRX_TST, proc->timestamp_tx&0xffffffff );
  if ((eNB->frame_parms.frame_type==FDD) ||
      ((eNB->frame_parms.frame_type==TDD) &&
       (subframe_select(&eNB->frame_parms,proc->subframe_tx) != SF_UL)))    
    send_IF5(eNB, proc->timestamp_tx, proc->subframe_tx, &seqno, IF5_RRH_GW_DL);
}


void tx_fh_if5_mobipass_NB_IoT(PHY_VARS_eNB_NB_IoT *eNB,L1_rxtx_proc_t *proc) {
  VCD_SIGNAL_DUMPER_DUMP_VARIABLE_BY_NAME( VCD_SIGNAL_DUMPER_VARIABLES_TRX_TST, proc->timestamp_tx&0xffffffff );
  if ((eNB->frame_parms.frame_type==FDD) ||
      ((eNB->frame_parms.frame_type==TDD) &&
       (subframe_select(&eNB->frame_parms,proc->subframe_tx) != SF_UL)))    
    send_IF5(eNB, proc->timestamp_tx, proc->subframe_tx, &seqno, IF5_MOBIPASS); 
}

void tx_fh_if4p5_NB_IoT(PHY_VARS_eNB_NB_IoT *eNB,L1_rxtx_proc_t *proc) {
  if ((eNB->frame_parms.frame_type==FDD) ||
      ((eNB->frame_parms.frame_type==TDD) &&
       (subframe_select(&eNB->frame_parms,proc->subframe_tx) != SF_UL)))    
    send_IF4p5(eNB,proc->frame_tx,proc->subframe_tx, IF4p5_PDLFFT);
}


void proc_tx_high0_NB_IoT(PHY_VARS_eNB_NB_IoT *eNB,
       eNB_rxtx_proc_NB_IoT_t *proc,
       relaying_type_t r_type,
       PHY_VARS_RN_NB_IoT *rn) {

  int offset = proc == &eNB->proc.proc_rxtx[0] ? 0 : 1;

  VCD_SIGNAL_DUMPER_DUMP_VARIABLE_BY_NAME( VCD_SIGNAL_DUMPER_VARIABLES_FRAME_NUMBER_TX0_ENB+offset, proc->frame_tx );
  VCD_SIGNAL_DUMPER_DUMP_VARIABLE_BY_NAME( VCD_SIGNAL_DUMPER_VARIABLES_SUBFRAME_NUMBER_TX0_ENB+offset, proc->subframe_tx );

  // issue here
  phy_procedures_eNB_TX_NB_IoT(eNB,proc,1);

  /* we're done, let the next one proceed */
  if (pthread_mutex_lock(&sync_phy_proc.mutex_phy_proc_tx) != 0) {
    LOG_E(PHY, "[SCHED][eNB] error locking PHY proc mutex for eNB TX proc\n");
    exit_fun("nothing to add");
  } 
  sync_phy_proc.phy_proc_CC_id++;
  sync_phy_proc.phy_proc_CC_id %= MAX_NUM_CCs;
  pthread_cond_broadcast(&sync_phy_proc.cond_phy_proc_tx);
  if (pthread_mutex_unlock(&sync_phy_proc.mutex_phy_proc_tx) != 0) {
    LOG_E(PHY, "[SCHED][eNB] error unlocking PHY proc mutex for eNB TX proc\n");
    exit_fun("nothing to add");
  }

}

void proc_tx_high_NB_IoT(PHY_VARS_eNB_NB_IoT *eNB,
      L1_rxtx_proc_t *proc,
      relaying_type_t r_type,
      PHY_VARS_RN_NB_IoT *rn) {


  // do PHY high
  proc_tx_high0_NB_IoT(eNB,proc,r_type,rn);

  // if TX fronthaul go ahead 
  if (eNB->tx_fh) eNB->tx_fh(eNB,proc);

}


void proc_tx_full_NB_IoT(PHY_VARS_eNB_NB_IoT *eNB,
      eNB_rxtx_proc_NB_IoT_t *proc,
      relaying_type_t r_type,
      PHY_VARS_RN_NB_IoT *rn) {


  // do PHY high
  proc_tx_high0_NB_IoT(eNB,proc,r_type,rn);
  // do OFDM modulation
  do_OFDM_mod_rt_NB_IoT(proc->subframe_tx,eNB);
  // if TX fronthaul go ahead 
  if (eNB->tx_fh) eNB->tx_fh(eNB,proc);

}

#if defined(ENABLE_ITTI) && defined(ENABLE_USE_MME)
/* Wait for eNB application initialization to be complete (eNB registration to MME) */
static void wait_system_ready (char *message, volatile int *start_flag) {
  
  static char *indicator[] = {".    ", "..   ", "...  ", ".... ", ".....",
            " ....", "  ...", "   ..", "    .", "     "};
  int i = 0;
  
  while ((!oai_exit) && (*start_flag == 0)) {
    //LOG_N(EMU, message, indicator[i]);
    fflush(stdout);
    i = (i + 1) % (sizeof(indicator) / sizeof(indicator[0]));
    usleep(200000);
  }
  
    //LOG_D(EMU,"\n");
}
#endif


static inline int rxtx_NB_IoT(PHY_VARS_eNB_NB_IoT *eNB,eNB_rxtx_proc_NB_IoT_t *proc, char *thread_name) {

  ///start_meas(&softmodem_stats_rxtx_sf);

  // ****************************************
  // Common RX procedures subframe n

  
   if ((eNB->do_prach)&&((eNB->node_function != NGFI_RCC_IF4p5_NB_IoT)))
    eNB->do_prach(eNB,proc->frame_rx,proc->subframe_rx);
  phy_procedures_eNB_common_RX(eNB,proc);
  
  // UE-specific RX processing for subframe n
  ///////////////////////////////////// for NB-IoT testing  ////////////////////////
  // for NB-IoT testing  // activating only TX part
  if (eNB->proc_uespec_rx) eNB->proc_uespec_rx(eNB, proc, no_relay );
   ////////////////////////////////////END///////////////////////

  //npusch_procedures(eNB,proc,data_or_control);
  //fill_rx_indication(eNB,i,frame,subframe);
  //////////////////////////////////// for IF Module/scheduler testing
 
  pthread_mutex_lock(&eNB->UL_INFO_mutex);

  eNB->UL_INFO.frame     = proc->frame_rx;
  eNB->UL_INFO.subframe  = proc->subframe_rx;
  eNB->UL_INFO.module_id = eNB->Mod_id;
  eNB->UL_INFO.CC_id     = eNB->CC_id;
  eNB->UL_INFO.hypersfn  = proc->HFN;

  eNB->if_inst_NB_IoT->UL_indication(&eNB->UL_INFO);

  pthread_mutex_unlock(&eNB->UL_INFO_mutex);

  //LOG_I(PHY,"After UL_indication\n");
  // *****************************************
  // TX processing for subframe n+4
  // run PHY TX procedures the one after the other for all CCs to avoid race conditions
  // (may be relaxed in the future for performance reasons)
  // *****************************************
  //if (wait_CCs(proc)<0) return(-1);
  
  if (oai_exit) return(-1);
  
  if (eNB->proc_tx) eNB->proc_tx(eNB, proc, no_relay, NULL );
  
  if (release_thread(&proc->mutex_rxtx,&proc->instance_cnt_rxtx,thread_name)<0) return(-1);

 /// stop_meas( &softmodem_stats_rxtx_sf );
  
  return(0);
}


static void* eNB_thread_single_NB_IoT( void* param ) {

  static int eNB_thread_single_status;

  eNB_proc_NB_IoT_t             *proc = (eNB_proc_NB_IoT_t*)param;
  L1_rxtx_proc_t        *proc_rxtx = &proc->proc_rxtx[0];
  PHY_VARS_eNB_NB_IoT *eNB = PHY_vars_eNB_NB_IoT_g[0][proc->CC_id];
  //PHY_VARS_eNB_NB_IoT *eNB_NB_IoT = PHY_vars_eNB_NB_IoT_g[0][proc->CC_id];
  LTE_DL_FRAME_PARMS *fp = &eNB->frame_parms;
 // NB_IoT_DL_FRAME_PARMS *fp_NB_IoT = &eNB_NB_IoT->frame_parms_NB_IoT;
  eNB->CC_id =  proc->CC_id;

  void *rxp[2],*rxp2[2];

  int subframe=0, frame=0; 

  int32_t dummy_rx[fp->nb_antennas_rx][fp->samples_per_tti] __attribute__((aligned(32)));

  int ic;

  int rxs;

  int i;

  // initialize the synchronization buffer to the common_vars.rxdata
  for (int i=0;i<fp->nb_antennas_rx;i++)
    rxp[i] = &eNB->common_vars.rxdata[0][i][0];

  // set default return value
  eNB_thread_single_status = 0;

  thread_top_init("eNB_thread_single",0,870000,1000000,1000000);

  wait_sync("eNB_thread_single");

#if defined(ENABLE_ITTI) && defined(ENABLE_USE_MME)
  if ((eNB->node_function < NGFI_RRU_IF5_NB_IoT) && (eNB->mac_enabled==1))
    wait_system_ready ("Waiting for eNB application to be ready %s\r", &start_eNB);
#endif 

  // Start IF device if any
  if (eNB->start_if) 
    if (eNB->start_if(eNB) != 0)
      LOG_E(HW,"Could not start the IF device\n");

  // Start RF device if any
  if (eNB->start_rf)
    if (eNB->start_rf(eNB) != 0)
      LOG_E(HW,"Could not start the RF device\n");

  // wakeup asnych_rxtx thread because the devices are ready at this point
  pthread_mutex_lock(&proc->mutex_asynch_rxtx);
  proc->instance_cnt_asynch_rxtx=0;
  pthread_mutex_unlock(&proc->mutex_asynch_rxtx);
  pthread_cond_signal(&proc->cond_asynch_rxtx);



  // if this is a slave eNB, try to synchronize on the DL frequency
  if ((eNB->is_slave) &&
      ((eNB->node_function >= NGFI_RRU_IF5))) {
    // if FDD, switch RX on DL frequency
    
    double temp_freq1 = eNB->rfdevice.openair0_cfg->rx_freq[0];
    double temp_freq2 = eNB->rfdevice.openair0_cfg->tx_freq[0];
    for (i=0;i<4;i++) {
      eNB->rfdevice.openair0_cfg->rx_freq[i] = eNB->rfdevice.openair0_cfg->tx_freq[i];
      eNB->rfdevice.openair0_cfg->tx_freq[i] = temp_freq1;
    }
    eNB->rfdevice.trx_set_freq_func(&eNB->rfdevice,eNB->rfdevice.openair0_cfg,0);

    while ((eNB->in_synch ==0)&&(!oai_exit)) {
      // read in frame
      rxs = eNB->rfdevice.trx_read_func(&eNB->rfdevice,
          &(proc->timestamp_rx),
          rxp,
          fp->samples_per_tti*10,
          fp->nb_antennas_rx);

      if (rxs != (fp->samples_per_tti*10))
  exit_fun("Problem receiving samples\n");

      // wakeup synchronization processing thread
      wakeup_synch(eNB);
      ic=0;
      
      while ((ic>=0)&&(!oai_exit)) {
  // continuously read in frames, 1ms at a time, 
  // until we are done with the synchronization procedure
  
  for (i=0; i<fp->nb_antennas_rx; i++)
    rxp2[i] = (void*)&dummy_rx[i][0];
  for (i=0;i<10;i++)
    rxs = eNB->rfdevice.trx_read_func(&eNB->rfdevice,
              &(proc->timestamp_rx),
              rxp2,
              fp->samples_per_tti,
              fp->nb_antennas_rx);
  if (rxs != fp->samples_per_tti)
    exit_fun( "problem receiving samples" );

  pthread_mutex_lock(&eNB->proc.mutex_synch);
  ic = eNB->proc.instance_cnt_synch;
  pthread_mutex_unlock(&eNB->proc.mutex_synch);
      } // ic>=0
    } // in_synch==0
    // read in rx_offset samples
    LOG_I(PHY,"Resynchronizing by %d samples\n",eNB->rx_offset);
    rxs = eNB->rfdevice.trx_read_func(&eNB->rfdevice,
              &(proc->timestamp_rx),
              rxp,
              eNB->rx_offset,
              fp->nb_antennas_rx);
    if (rxs != eNB->rx_offset)
      exit_fun( "problem receiving samples" );

    for (i=0;i<4;i++) {
      eNB->rfdevice.openair0_cfg->rx_freq[i] = temp_freq1;
      eNB->rfdevice.openair0_cfg->tx_freq[i] = temp_freq2;
    }
    eNB->rfdevice.trx_set_freq_func(&eNB->rfdevice,eNB->rfdevice.openair0_cfg,1);
  } // if RRU and slave


  // This is a forever while loop, it loops over subframes which are scheduled by incoming samples from HW devices
  while (!oai_exit) {

    // these are local subframe/frame counters to check that we are in synch with the fronthaul timing.
    // They are set on the first rx/tx in the underly FH routines.
    if (subframe==9) { 
      subframe=0;
      frame++;
      frame&=1023;
    } else {
      subframe++;
    }      

    if (eNB->CC_id==1) 
  LOG_D(PHY,"eNB thread single (proc %p, CC_id %d), frame %d (%p), subframe %d (%p)\n",
    proc, eNB->CC_id, frame,&frame,subframe,&subframe);
 
    // synchronization on FH interface, acquire signals/data and block
    if (eNB->rx_fh) eNB->rx_fh(eNB,&frame,&subframe);
    else AssertFatal(1==0, "No fronthaul interface : eNB->node_function %d",eNB->node_function);

    T(T_ENB_MASTER_TICK, T_INT(0), T_INT(proc->frame_rx), T_INT(proc->subframe_rx));

    proc_rxtx->subframe_rx = proc->subframe_rx;
    proc_rxtx->frame_rx    = proc->frame_rx;
    proc_rxtx->subframe_tx = (proc->subframe_rx+4)%10;
    proc_rxtx->frame_tx    = (proc->subframe_rx>5) ? (1+proc->frame_rx)&1023 : proc->frame_rx;
    proc->frame_tx         = proc_rxtx->frame_tx;
    proc_rxtx->timestamp_tx = proc->timestamp_tx;
    // adjust for timing offset between RRU
    if (eNB->CC_id!=0) proc_rxtx->frame_tx = (proc_rxtx->frame_tx+proc->frame_offset)&1023;

    // At this point, all information for subframe has been received on FH interface
    // If this proc is to provide synchronization, do so
    wakeup_slaves(proc);

   
    if (rxtx_NB_IoT(eNB,proc_rxtx,"eNB_thread_single") < 0) break;
    //if (rxtx(eNB,proc_rxtx,"eNB_thread_single") < 0) break;
  }
  

  printf( "Exiting eNB_single thread \n");
 
  eNB_thread_single_status = 0;
  return &eNB_thread_single_status;

}

/*!
 * \brief The RX UE-specific and TX thread of eNB.
 * \param param is a \ref eNB_proc_t structure which contains the info what to process.
 * \returns a pointer to an int. The storage is not on the heap and must not be freed.
 */
static void* eNB_thread_rxtx_NB_IoT( void* param ) {

  static int eNB_thread_rxtx_status;

  L1_rxtx_proc_t *proc = (L1_rxtx_proc_t*)param;
  ///eNB_rxtx_proc_NB_IoT_t *proc_NB_IoT = (eNB_rxtx_proc_NB_IoT_t*)param;  // to remove when eNB_thread_rxtx_status is duplicated for NB-IoT

  PHY_VARS_eNB_NB_IoT *eNB = PHY_vars_eNB_NB_IoT_g[0][proc->CC_id];

  ///PHY_VARS_eNB_NB_IoT *eNB_NB_IoT = PHY_vars_eNB_NB_IoT_g[0][proc_NB_IoT->CC_id]; // to remove when eNB_thread_rxtx_status is duplicated for NB-IoT

  char thread_name[100];


  // set default return value
  eNB_thread_rxtx_status = 0;


  sprintf(thread_name,"RXn_TXnp4_%d\n",&eNB->proc.proc_rxtx[0] == proc ? 0 : 1);
  thread_top_init(thread_name,1,850000L,1000000L,2000000L);

  while (!oai_exit) {
    VCD_SIGNAL_DUMPER_DUMP_FUNCTION_BY_NAME( VCD_SIGNAL_DUMPER_FUNCTIONS_eNB_PROC_RXTX0+(proc->subframe_rx&1), 0 );

    if (wait_on_condition(&proc->mutex_rxtx,&proc->cond_rxtx,&proc->instance_cnt_rxtx,thread_name)<0) break;

    VCD_SIGNAL_DUMPER_DUMP_FUNCTION_BY_NAME( VCD_SIGNAL_DUMPER_FUNCTIONS_eNB_PROC_RXTX0+(proc->subframe_rx&1), 1 );

    
  
    if (oai_exit) break;

    if (eNB->CC_id==0)
    {
      if (rxtx_NB_IoT(eNB,proc,thread_name) < 0) break;
    }

  } // while !oai_exit

  VCD_SIGNAL_DUMPER_DUMP_FUNCTION_BY_NAME( VCD_SIGNAL_DUMPER_FUNCTIONS_eNB_PROC_RXTX0+(proc->subframe_rx&1), 0 );

  printf( "Exiting eNB thread RXn_TXnp4\n");

  eNB_thread_rxtx_status = 0;
  return &eNB_thread_rxtx_status;
}


// asynchronous UL with IF5 (RCC,RAU,eNodeB_BBU)
void fh_if5_asynch_UL_NB_IoT(PHY_VARS_eNB_NB_IoT *eNB,int *frame,int *subframe) {

  eNB_proc_NB_IoT_t *proc       = &eNB->proc;
  LTE_DL_FRAME_PARMS *fp = &eNB->frame_parms;

  recv_IF5(eNB, &proc->timestamp_rx, *subframe, IF5_MOBIPASS); 

  int offset_mobipass = 40120;
  pthread_mutex_lock(&proc->mutex_asynch_rxtx);
  proc->subframe_rx = ((proc->timestamp_rx-offset_mobipass)/fp->samples_per_tti)%10;
  proc->frame_rx    = ((proc->timestamp_rx-offset_mobipass)/(fp->samples_per_tti*10))&1023;
  
  if (proc->first_rx == 1) {
    proc->first_rx =2;
    *subframe = proc->subframe_rx;
    *frame    = proc->frame_rx; 
    LOG_E(PHY,"[Mobipass]timestamp_rx:%"PRId64", frame_rx %d, subframe: %d\n",proc->timestamp_rx,proc->frame_rx,proc->subframe_rx);
  }
  else {
    if (proc->subframe_rx != *subframe) {
        proc->first_rx++;
       LOG_E(PHY,"[Mobipass]timestamp:%"PRId64", subframe_rx %d is not what we expect %d, first_rx:%d\n",proc->timestamp_rx, proc->subframe_rx,*subframe, proc->first_rx);
      //exit_fun("Exiting");
    }
    if (proc->frame_rx != *frame) {
        proc->first_rx++;
       LOG_E(PHY,"[Mobipass]timestamp:%"PRId64", frame_rx %d is not what we expect %d, first_rx:%d\n",proc->timestamp_rx,proc->frame_rx,*frame, proc->first_rx);  
     // exit_fun("Exiting");
    }
    // temporary solution
      *subframe = proc->subframe_rx;
      *frame    = proc->frame_rx;
  }

  pthread_mutex_unlock(&proc->mutex_asynch_rxtx);

} // eNodeB_3GPP_BBU 


// asynchronous UL with IF4p5 (RCC,RAU,eNodeB_BBU)
void fh_if4p5_asynch_UL_NB_IoT(PHY_VARS_eNB_NB_IoT *eNB,int *frame,int *subframe) {

  LTE_DL_FRAME_PARMS *fp = &eNB->frame_parms;
  eNB_proc_NB_IoT_t *proc       = &eNB->proc;

  uint16_t packet_type;
  uint32_t symbol_number,symbol_mask,symbol_mask_full,prach_rx;


  symbol_number = 0;
  symbol_mask = 0;
  symbol_mask_full = (1<<fp->symbols_per_tti)-1;
  prach_rx = 0;

  do {   // Blocking, we need a timeout on this !!!!!!!!!!!!!!!!!!!!!!!
    recv_IF4p5(eNB, &proc->frame_rx, &proc->subframe_rx, &packet_type, &symbol_number);
    if (proc->first_rx != 0) {
      *frame = proc->frame_rx;
      *subframe = proc->subframe_rx;
      proc->first_rx--;
    }
    else {
      if (proc->frame_rx != *frame) {
  LOG_E(PHY,"fh_if4p5_asynch_UL: frame_rx %d is not what we expect %d\n",proc->frame_rx,*frame);
  exit_fun("Exiting");
      }
      if (proc->subframe_rx != *subframe) {
  LOG_E(PHY,"fh_if4p5_asynch_UL: subframe_rx %d is not what we expect %d\n",proc->subframe_rx,*subframe);
  exit_fun("Exiting");
      }
    }
    if (packet_type == IF4p5_PULFFT) {
      symbol_mask = symbol_mask | (1<<symbol_number);
      prach_rx = (is_prach_subframe(fp, proc->frame_rx, proc->subframe_rx)>0) ? 1 : 0;                            
    } else if (packet_type == IF4p5_PRACH) {
      prach_rx = 0;
    }
  } while( (symbol_mask != symbol_mask_full) || (prach_rx == 1));    
  

} 


void fh_if5_asynch_DL_NB_IoT(PHY_VARS_eNB_NB_IoT *eNB,int *frame,int *subframe) {

  LTE_DL_FRAME_PARMS *fp = &eNB->frame_parms;
  eNB_proc_NB_IoT_t *proc       = &eNB->proc;
  int subframe_tx,frame_tx;
  openair0_timestamp timestamp_tx;

  recv_IF5(eNB, &timestamp_tx, *subframe, IF5_RRH_GW_DL); 
  clock_gettime( CLOCK_MONOTONIC, &end_fh);
  end_fh_sf = *subframe;
  recv_if_count = recv_if_count + 1;
  LOG_D(HW,"[From SF %d to SF %d] RTT_FH: %"PRId64"\n", start_fh_prev_sf, end_fh_sf, clock_difftime_ns(start_fh_prev, end_fh));

  VCD_SIGNAL_DUMPER_DUMP_FUNCTION_BY_NAME( VCD_SIGNAL_DUMPER_FUNCTIONS_RECV_IF5, 0 );

  subframe_tx = (timestamp_tx/fp->samples_per_tti)%10;
  frame_tx    = (timestamp_tx/(fp->samples_per_tti*10))&1023;

  VCD_SIGNAL_DUMPER_DUMP_VARIABLE_BY_NAME( VCD_SIGNAL_DUMPER_VARIABLES_FRAME_NUMBER_TX0_ENB, frame_tx );
  VCD_SIGNAL_DUMPER_DUMP_VARIABLE_BY_NAME( VCD_SIGNAL_DUMPER_VARIABLES_SUBFRAME_NUMBER_TX0_ENB, subframe_tx ); 

  if (proc->first_tx != 0) {
    *subframe = subframe_tx;
    *frame    = frame_tx;
    proc->first_tx = 0;
  }
  else {
    if (subframe_tx != *subframe) {
      LOG_E(PHY,"fh_if5_asynch_DL: subframe_tx %d is not what we expect %d\n",subframe_tx,*subframe);
      exit_fun("Exiting");
    }
    if (frame_tx != *frame) { 
      LOG_E(PHY,"fh_if5_asynch_DL: frame_tx %d is not what we expect %d\n",frame_tx,*frame);
      exit_fun("Exiting");
    }
  }
}

void fh_if4p5_asynch_DL_NB_IoT(PHY_VARS_eNB_NB_IoT *eNB,int *frame,int *subframe) {

  LTE_DL_FRAME_PARMS *fp = &eNB->frame_parms;
  eNB_proc_NB_IoT_t *proc       = &eNB->proc;

  uint16_t packet_type;
  uint32_t symbol_number,symbol_mask_full;
  int subframe_tx,frame_tx;

  symbol_number = 0;

  LOG_D(PHY,"fh_asynch_DL_IF4p5: in, frame %d, subframe %d\n",*frame,*subframe);

  // correct for TDD
  if (fp->frame_type == TDD) {
    while (subframe_select(fp,*subframe) == SF_UL) {
      *subframe=*subframe+1;
      if (*subframe==10) {
  *subframe=0;
  *frame=*frame+1;
      }
    }
  }

  LOG_D(PHY,"fh_asynch_DL_IF4p5: after TDD correction, frame %d, subframe %d\n",*frame,*subframe);

  symbol_mask_full = ((subframe_select(fp,*subframe) == SF_S) ? (1<<fp->dl_symbols_in_S_subframe) : (1<<fp->symbols_per_tti))-1;
  do {   // Blocking, we need a timeout on this !!!!!!!!!!!!!!!!!!!!!!!
    recv_IF4p5(eNB, &frame_tx, &subframe_tx, &packet_type, &symbol_number);
    if (proc->first_tx != 0) {
      *frame    = frame_tx;
      *subframe = subframe_tx;
      proc->first_tx = 0;
      proc->frame_offset = frame_tx - proc->frame_tx;
      symbol_mask_full = ((subframe_select(fp,*subframe) == SF_S) ? (1<<fp->dl_symbols_in_S_subframe) : (1<<fp->symbols_per_tti))-1;

    }
    else {
      if (frame_tx != *frame) {
  LOG_E(PHY,"fh_if4p5_asynch_DL: frame_tx %d is not what we expect %d\n",frame_tx,*frame);
  exit_fun("Exiting");
      }
      if (subframe_tx != *subframe) {
  LOG_E(PHY,"fh_if4p5_asynch_DL: (frame %d) subframe_tx %d is not what we expect %d\n",frame_tx,subframe_tx,*subframe);
  //*subframe = subframe_tx;
  exit_fun("Exiting");
      }
    }
    if (packet_type == IF4p5_PDLFFT) {
      proc->symbol_mask[subframe_tx] =proc->symbol_mask[subframe_tx] | (1<<symbol_number);
    }
    else {
      LOG_E(PHY,"Illegal IF4p5 packet type (should only be IF4p5_PDLFFT%d\n",packet_type);
      exit_fun("Exiting");
    }
  } while (proc->symbol_mask[*subframe] != symbol_mask_full);    
  
  *frame = frame_tx;


  VCD_SIGNAL_DUMPER_DUMP_VARIABLE_BY_NAME( VCD_SIGNAL_DUMPER_VARIABLES_FRAME_NUMBER_TX0_ENB, frame_tx );
  VCD_SIGNAL_DUMPER_DUMP_VARIABLE_BY_NAME( VCD_SIGNAL_DUMPER_VARIABLES_SUBFRAME_NUMBER_TX0_ENB, subframe_tx );

  // intialize this to zero after we're done with the subframe
  proc->symbol_mask[*subframe] = 0;
  
  do_OFDM_mod_rt_NB_IoT(*subframe, eNB);
} 


/*!
 * \brief The Asynchronous RX/TX FH thread of RAU/RCC/eNB/RRU.
 * This handles the RX FH for an asynchronous RRU/UE
 * \param param is a \ref eNB_proc_t structure which contains the info what to process.
 * \returns a pointer to an int. The storage is not on the heap and must not be freed.
 */
static void* eNB_thread_asynch_rxtx_NB_IoT( void* param ) {

  static int eNB_thread_asynch_rxtx_status;

  eNB_proc_NB_IoT_t *proc = (eNB_proc_NB_IoT_t*)param;
  PHY_VARS_eNB_NB_IoT *eNB = PHY_vars_eNB_NB_IoT_g[0][proc->CC_id];



  int subframe=0, frame=0; 

  thread_top_init("thread_asynch",1,870000L,1000000L,1000000L);

  // wait for top-level synchronization and do one acquisition to get timestamp for setting frame/subframe

  wait_sync("thread_asynch");

  // wait for top-level synchronization and do one acquisition to get timestamp for setting frame/subframe
  printf( "waiting for devices (eNB_thread_asynch_rx)\n");

  wait_on_condition(&proc->mutex_asynch_rxtx,&proc->cond_asynch_rxtx,&proc->instance_cnt_asynch_rxtx,"thread_asynch");

  printf( "devices ok (eNB_thread_asynch_rx)\n");


  while (!oai_exit) { 
   
    if (oai_exit) break;   

    if (subframe==9) { 
      subframe=0;
      frame++;
      frame&=1023;
    } else {
      subframe++;
    }      

    if (eNB->fh_asynch) eNB->fh_asynch(eNB,&frame,&subframe);
    else AssertFatal(1==0, "Unknown eNB->node_function %d",eNB->node_function);
    
  }

  eNB_thread_asynch_rxtx_status=0;
  return(&eNB_thread_asynch_rxtx_status);
}


void rx_rf_NB_IoT(PHY_VARS_eNB_NB_IoT *eNB,int *frame,int *subframe) {

  eNB_proc_NB_IoT_t *proc = &eNB->proc;
  LTE_DL_FRAME_PARMS *fp = &eNB->frame_parms;
  void *rxp[fp->nb_antennas_rx],*txp[fp->nb_antennas_tx]; 
  unsigned int rxs,txs;
  int i;
  int tx_sfoffset = (eNB->single_thread_flag == 1) ? 3 : 2;
  openair0_timestamp ts,old_ts;

  if (proc->first_rx==0) {
    
    // Transmit TX buffer based on timestamp from RX
    //    printf("trx_write -> USRP TS %llu (sf %d)\n", (proc->timestamp_rx+(3*fp->samples_per_tti)),(proc->subframe_rx+2)%10);
    VCD_SIGNAL_DUMPER_DUMP_VARIABLE_BY_NAME( VCD_SIGNAL_DUMPER_VARIABLES_TRX_TST, (proc->timestamp_rx+(tx_sfoffset*fp->samples_per_tti)-openair0_cfg[0].tx_sample_advance)&0xffffffff );
    VCD_SIGNAL_DUMPER_DUMP_FUNCTION_BY_NAME( VCD_SIGNAL_DUMPER_FUNCTIONS_TRX_WRITE, 1 );
    // prepare tx buffer pointers

    lte_subframe_t SF_type     = subframe_select(fp,(proc->subframe_rx+tx_sfoffset)%10);
    lte_subframe_t prevSF_type = subframe_select(fp,(proc->subframe_rx+tx_sfoffset+9)%10);
    lte_subframe_t nextSF_type = subframe_select(fp,(proc->subframe_rx+tx_sfoffset+1)%10);
    if ((SF_type == SF_DL) ||
  (SF_type == SF_S)) {

      for (i=0; i<fp->nb_antennas_tx; i++)
  txp[i] = (void*)&eNB->common_vars.txdata[0][i][((proc->subframe_rx+tx_sfoffset)%10)*fp->samples_per_tti]; 

      int siglen=fp->samples_per_tti,flags=1;

      if (SF_type == SF_S) {
  siglen = fp->dl_symbols_in_S_subframe*(fp->ofdm_symbol_size+fp->nb_prefix_samples0);
  flags=3; // end of burst
      }
      if ((fp->frame_type == TDD) &&
    (SF_type == SF_DL)&&
    (prevSF_type == SF_UL) &&
    (nextSF_type == SF_DL))
  flags = 2; // start of burst

      if ((fp->frame_type == TDD) &&
    (SF_type == SF_DL)&&
    (prevSF_type == SF_UL) &&
    (nextSF_type == SF_UL))
  flags = 4; // start of burst and end of burst (only one DL SF between two UL)
     
      VCD_SIGNAL_DUMPER_DUMP_FUNCTION_BY_NAME( VCD_SIGNAL_DUMPER_FUNCTIONS_TRX_WRITE, 1 );
      VCD_SIGNAL_DUMPER_DUMP_VARIABLE_BY_NAME( VCD_SIGNAL_DUMPER_VARIABLES_TRX_WRITE_FLAGS,flags); 
      txs = eNB->rfdevice.trx_write_func(&eNB->rfdevice,
           proc->timestamp_rx+eNB->ts_offset+(tx_sfoffset*fp->samples_per_tti)-openair0_cfg[0].tx_sample_advance,
           txp,
           siglen,
           fp->nb_antennas_tx,
           flags);
      clock_gettime( CLOCK_MONOTONIC, &end_rf);    
      end_rf_ts = proc->timestamp_rx+eNB->ts_offset+(tx_sfoffset*fp->samples_per_tti)-openair0_cfg[0].tx_sample_advance;
      if (recv_if_count != 0 ) {
        recv_if_count = recv_if_count-1;
        LOG_D(HW,"[From Timestamp %"PRId64" to Timestamp %"PRId64"] RTT_RF: %"PRId64"; RTT_RF\n", start_rf_prev_ts, end_rf_ts, clock_difftime_ns(start_rf_prev, end_rf));
        LOG_D(HW,"[From Timestamp %"PRId64" to Timestamp %"PRId64"] RTT_RF: %"PRId64"; RTT_RF\n",start_rf_prev2_ts, end_rf_ts, clock_difftime_ns(start_rf_prev2, end_rf));
      }
      VCD_SIGNAL_DUMPER_DUMP_FUNCTION_BY_NAME( VCD_SIGNAL_DUMPER_FUNCTIONS_TRX_WRITE, 0 );
      
      
      
      if (txs !=  siglen) {
  LOG_E(PHY,"TX : Timeout (sent %d/%d)\n",txs, fp->samples_per_tti);
  exit_fun( "problem transmitting samples" );
      } 
    }
  }

  for (i=0; i<fp->nb_antennas_rx; i++)
    rxp[i] = (void*)&eNB->common_vars.rxdata[0][i][*subframe*fp->samples_per_tti];
  
  VCD_SIGNAL_DUMPER_DUMP_FUNCTION_BY_NAME( VCD_SIGNAL_DUMPER_FUNCTIONS_TRX_READ, 1 );

  old_ts = proc->timestamp_rx;

  rxs = eNB->rfdevice.trx_read_func(&eNB->rfdevice,
            &ts,
            rxp,
            fp->samples_per_tti,
            fp->nb_antennas_rx);
  start_rf_prev2= start_rf_prev;
  start_rf_prev2_ts= start_rf_prev_ts; 
  start_rf_prev = start_rf_new;
  start_rf_prev_ts = start_rf_new_ts;
  clock_gettime( CLOCK_MONOTONIC, &start_rf_new);
  start_rf_new_ts = ts;
  LOG_D(PHY,"rx_rf: first_rx %d received ts %"PRId64" (sptti %d)\n",proc->first_rx,ts,fp->samples_per_tti);
  VCD_SIGNAL_DUMPER_DUMP_FUNCTION_BY_NAME( VCD_SIGNAL_DUMPER_FUNCTIONS_TRX_READ, 0 );

  proc->timestamp_rx = ts-eNB->ts_offset;

  if (rxs != fp->samples_per_tti)
    LOG_E(PHY,"rx_rf: Asked for %d samples, got %d from USRP\n",fp->samples_per_tti,rxs);

  VCD_SIGNAL_DUMPER_DUMP_FUNCTION_BY_NAME( VCD_SIGNAL_DUMPER_FUNCTIONS_TRX_READ, 0 );
 
  if (proc->first_rx == 1) {
    eNB->ts_offset = proc->timestamp_rx;
    proc->timestamp_rx=0;
  }
  else {

    if (proc->timestamp_rx - old_ts != fp->samples_per_tti) {
      LOG_I(PHY,"rx_rf: rfdevice timing drift of %"PRId64" samples (ts_off %"PRId64")\n",proc->timestamp_rx - old_ts - fp->samples_per_tti,eNB->ts_offset);
      eNB->ts_offset += (proc->timestamp_rx - old_ts - fp->samples_per_tti);
      proc->timestamp_rx = ts-eNB->ts_offset;
    }
  }
  proc->frame_rx    = (proc->timestamp_rx / (fp->samples_per_tti*10))&1023;
  proc->subframe_rx = (proc->timestamp_rx / fp->samples_per_tti)%10;
  proc->frame_rx    = (proc->frame_rx+proc->frame_offset)&1023;
  proc->frame_tx    = proc->frame_rx;
  if (proc->subframe_rx > 5) proc->frame_tx=(proc->frame_tx+1)&1023;
  // synchronize first reception to frame 0 subframe 0

  proc->timestamp_tx = proc->timestamp_rx+(4*fp->samples_per_tti);
  //  printf("trx_read <- USRP TS %lu (offset %d sf %d, f %d, first_rx %d)\n", proc->timestamp_rx,eNB->ts_offset,proc->subframe_rx,proc->frame_rx,proc->first_rx);  
  
  if (proc->first_rx == 0) {
    if (proc->subframe_rx != *subframe){
      LOG_E(PHY,"rx_rf: Received Timestamp (%"PRId64") doesn't correspond to the time we think it is (proc->subframe_rx %d, subframe %d)\n",proc->timestamp_rx,proc->subframe_rx,*subframe);
      exit_fun("Exiting");
    }
    int f2 = (*frame+proc->frame_offset)&1023;    
    if (proc->frame_rx != f2) {
      LOG_E(PHY,"rx_rf: Received Timestamp (%"PRId64") doesn't correspond to the time we think it is (proc->frame_rx %d frame %d, frame_offset %d, f2 %d)\n",proc->timestamp_rx,proc->frame_rx,*frame,proc->frame_offset,f2);
      exit_fun("Exiting");
    }
  } else {
    proc->first_rx--;
    *frame = proc->frame_rx;
    *subframe = proc->subframe_rx;        
  }
  
  //printf("timestamp_rx %lu, frame %d(%d), subframe %d(%d)\n",proc->timestamp_rx,proc->frame_rx,frame,proc->subframe_rx,subframe);
  
  VCD_SIGNAL_DUMPER_DUMP_VARIABLE_BY_NAME( VCD_SIGNAL_DUMPER_VARIABLES_TRX_TS, proc->timestamp_rx&0xffffffff );
  
  if (rxs != fp->samples_per_tti)
    exit_fun( "problem receiving samples" );
  
}


void rx_fh_if5_NB_IoT(PHY_VARS_eNB_NB_IoT *eNB,int *frame, int *subframe) {

  LTE_DL_FRAME_PARMS *fp = &eNB->frame_parms;
  eNB_proc_NB_IoT_t *proc = &eNB->proc;

  recv_IF5(eNB, &proc->timestamp_rx, *subframe, IF5_RRH_GW_UL); 

  proc->frame_rx    = (proc->timestamp_rx / (fp->samples_per_tti*10))&1023;
  proc->subframe_rx = (proc->timestamp_rx / fp->samples_per_tti)%10;
  
  if (proc->first_rx == 0) {
    if (proc->subframe_rx != *subframe){
      LOG_E(PHY,"rx_fh_if5: Received Timestamp doesn't correspond to the time we think it is (proc->subframe_rx %d, subframe %d)\n",proc->subframe_rx,*subframe);
      exit_fun("Exiting");
    }
    
    if (proc->frame_rx != *frame) {
      LOG_E(PHY,"rx_fh_if5: Received Timestamp doesn't correspond to the time we think it is (proc->frame_rx %d frame %d)\n",proc->frame_rx,*frame);
      exit_fun("Exiting");
    }
  } else {
    proc->first_rx--;
    *frame = proc->frame_rx;
    *subframe = proc->subframe_rx;        
  }      



  proc->timestamp_tx = proc->timestamp_rx +  (4*fp->samples_per_tti);
  
  VCD_SIGNAL_DUMPER_DUMP_VARIABLE_BY_NAME( VCD_SIGNAL_DUMPER_VARIABLES_TRX_TS, proc->timestamp_rx&0xffffffff );

}

void rx_fh_if4p5_NB_IoT(PHY_VARS_eNB_NB_IoT *eNB,int *frame,int *subframe) {

  LTE_DL_FRAME_PARMS *fp = &eNB->frame_parms;
  eNB_proc_NB_IoT_t *proc = &eNB->proc;
  int f,sf;

  uint16_t packet_type;
  uint32_t symbol_number=0;
  uint32_t symbol_mask_full;

  if ((fp->frame_type == TDD) && (subframe_select(fp,*subframe)==SF_S))
    symbol_mask_full = (1<<fp->ul_symbols_in_S_subframe)-1;
  else 
    symbol_mask_full = (1<<fp->symbols_per_tti)-1;

  if (eNB->CC_id==1) LOG_I(PHY,"rx_fh_if4p5: frame %d, subframe %d\n",*frame,*subframe);
  do {   // Blocking, we need a timeout on this !!!!!!!!!!!!!!!!!!!!!!!
    recv_IF4p5(eNB, &f, &sf, &packet_type, &symbol_number);

    //proc->frame_rx = (proc->frame_rx + proc->frame_offset)&1023;
    if (packet_type == IF4p5_PULFFT) {
      LOG_D(PHY,"rx_fh_if4p5: frame %d, subframe %d, PULFFT symbol %d\n",f,sf,symbol_number);

      proc->symbol_mask[sf] = proc->symbol_mask[sf] | (1<<symbol_number);
    } else if (packet_type == IF4p5_PULTICK) {
    
      if ((proc->first_rx==0) && (f!=*frame))
  LOG_E(PHY,"rx_fh_if4p5: PULTICK received frame %d != expected %d\n",f,*frame);
      if ((proc->first_rx==0) && (sf!=*subframe))
  LOG_E(PHY,"rx_fh_if4p5: PULTICK received subframe %d != expected %d (first_rx %d)\n",sf,*subframe,proc->first_rx);
      break;
    } else if (packet_type == IF4p5_PRACH) {
      LOG_D(PHY,"rx_fh:if4p5: frame %d, subframe %d, PRACH\n",f,sf);
      // wakeup prach processing
      if (eNB->do_prach) eNB->do_prach(eNB,f,sf);
    }

    if (eNB->CC_id==1) LOG_I(PHY,"rx_fh_if4p5: symbol_mask[%d] %x\n",*subframe,proc->symbol_mask[*subframe]);

  } while(proc->symbol_mask[*subframe] != symbol_mask_full);    

  proc->subframe_rx = sf;
  proc->frame_rx    = f;

  proc->symbol_mask[*subframe] = 0;
  proc->symbol_mask[(9+*subframe)%10]= 0; // to handle a resynchronization event

  if (eNB->CC_id==1) LOG_I(PHY,"Clearing symbol_mask[%d]\n",*subframe);

  //caculate timestamp_rx, timestamp_tx based on frame and subframe
  proc->timestamp_rx = ((proc->frame_rx * 10)  + proc->subframe_rx ) * fp->samples_per_tti ;
  proc->timestamp_tx = proc->timestamp_rx +  (4*fp->samples_per_tti);
  
 
  if (proc->first_rx == 0) {
    if (proc->subframe_rx != *subframe){
      LOG_E(PHY,"rx_fh_if4p5, CC_id %d: Received Timestamp doesn't correspond to the time we think it is (proc->subframe_rx %d, subframe %d,CCid %d)\n",eNB->CC_id,proc->subframe_rx,*subframe,eNB->CC_id);
    }
    if (proc->frame_rx != *frame) {
      if (proc->frame_rx == proc->frame_offset) // This means that the RRU has adjusted its frame timing
  proc->frame_offset = 0;
      else 
  LOG_E(PHY,"rx_fh_if4p5: Received Timestamp doesn't correspond to the time we think it is (proc->frame_rx %d frame %d,CCid %d)\n",proc->frame_rx,*frame,eNB->CC_id);
    }
  } else {
    proc->first_rx = 0;
    if (eNB->CC_id==0)
      proc->frame_offset = 0;
    else
      proc->frame_offset = PHY_vars_eNB_NB_IoT_g[0][0]->proc.frame_rx;

    *frame = proc->frame_rx;//(proc->frame_rx + proc->frame_offset)&1023;
    *subframe = proc->subframe_rx;        
  }
  


  if (eNB->CC_id==0) VCD_SIGNAL_DUMPER_DUMP_VARIABLE_BY_NAME( VCD_SIGNAL_DUMPER_VARIABLES_TRX_TS, proc->timestamp_rx&0xffffffff );
  
}


void rx_fh_slave_NB_IoT(PHY_VARS_eNB_NB_IoT *eNB,int *frame,int *subframe) {
  // This case is for synchronization to another thread
  // it just waits for an external event.  The actual rx_fh is handle by the asynchronous RX thread
  eNB_proc_NB_IoT_t *proc=&eNB->proc;

  if (wait_on_condition(&proc->mutex_FH,&proc->cond_FH,&proc->instance_cnt_FH,"rx_fh_slave") < 0)
    return;

  release_thread(&proc->mutex_FH,&proc->instance_cnt_FH,"rx_fh_slave");

  
}

uint32_t sync_corr[307200] __attribute__((aligned(32)));

// This thread run the initial synchronization like a UE
void *eNB_thread_synch_NB_IoT(void *arg) {

  PHY_VARS_eNB_NB_IoT *eNB = (PHY_VARS_eNB_NB_IoT*)arg;
  LTE_DL_FRAME_PARMS *fp=&eNB->frame_parms;
  int32_t sync_pos,sync_pos2;
  uint32_t peak_val;

  thread_top_init("eNB_thread_synch",0,5000000,10000000,10000000);

  wait_sync("eNB_thread_synch");

  // initialize variables for PSS detection
  lte_sync_time_init(&eNB->frame_parms);

  while (!oai_exit) {

    // wait to be woken up
    pthread_mutex_lock(&eNB->proc.mutex_synch);
    while (eNB->proc.instance_cnt_synch < 0)
      pthread_cond_wait(&eNB->proc.cond_synch,&eNB->proc.mutex_synch);
    pthread_mutex_unlock(&eNB->proc.mutex_synch);

    // if we're not in synch, then run initial synch
    if (eNB->in_synch == 0) { 
      // run intial synch like UE
      LOG_I(PHY,"Running initial synchronization\n");
      
      sync_pos = lte_sync_time_eNB(eNB->common_vars.rxdata[0],
           fp,
           fp->samples_per_tti*5,
           &peak_val,
           sync_corr);
      LOG_I(PHY,"eNB synch: %d, val %d\n",sync_pos,peak_val);

      if (sync_pos >= 0) {
  if (sync_pos >= fp->nb_prefix_samples)
    sync_pos2 = sync_pos - fp->nb_prefix_samples;
  else
    sync_pos2 = sync_pos + (fp->samples_per_tti*10) - fp->nb_prefix_samples;
  
  if (fp->frame_type == FDD) {
    
    // PSS is hypothesized in last symbol of first slot in Frame
    int sync_pos_slot = (fp->samples_per_tti>>1) - fp->ofdm_symbol_size - fp->nb_prefix_samples;
    
    if (sync_pos2 >= sync_pos_slot)
      eNB->rx_offset = sync_pos2 - sync_pos_slot;
    else
      eNB->rx_offset = (fp->samples_per_tti*10) + sync_pos2 - sync_pos_slot;
  }
  else {
    
  }

  LOG_I(PHY,"Estimated sync_pos %d, peak_val %d => timing offset %d\n",sync_pos,peak_val,eNB->rx_offset);
  
  /*
  if ((peak_val > 300000) && (sync_pos > 0)) {
  //      if (sync_pos++ > 3) {
  write_output("eNB_sync.m","sync",(void*)&sync_corr[0],fp->samples_per_tti*5,1,2);
  write_output("eNB_rx.m","rxs",(void*)eNB->common_vars.rxdata[0][0],fp->samples_per_tti*10,1,1);
  exit(-1);
  }
  */
  eNB->in_synch=1;
      }
    }

    // release thread
    pthread_mutex_lock(&eNB->proc.mutex_synch);
    eNB->proc.instance_cnt_synch--;
    pthread_mutex_unlock(&eNB->proc.mutex_synch);
  } // oai_exit

  lte_sync_time_free();

  return NULL;
}



/*!
 * \brief The Fronthaul thread of RRU/RAU/RCC/eNB
 * In the case of RRU/eNB, handles interface with external RF
 * In the case of RAU/RCC, handles fronthaul interface with RRU/RAU
 * \param param is a \ref eNB_proc_t structure which contains the info what to process.
 * \returns a pointer to an int. The storage is not on the heap and must not be freed.
 */

static void* eNB_thread_FH_NB_IoT( void* param ) {
  
  static int eNB_thread_FH_status;

  eNB_proc_NB_IoT_t *proc = (eNB_proc_NB_IoT_t*)param;
  PHY_VARS_eNB_NB_IoT *eNB = PHY_vars_eNB_NB_IoT_g[0][proc->CC_id];
  LTE_DL_FRAME_PARMS *fp = &eNB->frame_parms;

  int subframe=0, frame=0; 

  // set default return value
  eNB_thread_FH_status = 0;

  thread_top_init("eNB_thread_FH",0,870000,1000000,1000000);

  wait_sync("eNB_thread_FH");

#if defined(ENABLE_ITTI) && defined(ENABLE_USE_MME)
  if (eNB->node_function < NGFI_RRU_IF5_NB_IoT)
    wait_system_ready ("Waiting for eNB application to be ready %s\r", &start_eNB);
#endif 

  // Start IF device if any
  if (eNB->start_if) 
    if (eNB->start_if(eNB) != 0)
      LOG_E(HW,"Could not start the IF device\n");

  // Start RF device if any
  if (eNB->start_rf)
    if (eNB->start_rf(eNB) != 0)
      LOG_E(HW,"Could not start the RF device\n");

  // wakeup asnych_rxtx thread because the devices are ready at this point
  pthread_mutex_lock(&proc->mutex_asynch_rxtx);
  proc->instance_cnt_asynch_rxtx=0;
  pthread_mutex_unlock(&proc->mutex_asynch_rxtx);
  pthread_cond_signal(&proc->cond_asynch_rxtx);

  // This is a forever while loop, it loops over subframes which are scheduled by incoming samples from HW devices
  while (!oai_exit) {

    // these are local subframe/frame counters to check that we are in synch with the fronthaul timing.
    // They are set on the first rx/tx in the underly FH routines.
    if (subframe==9) { 
      subframe=0;
      frame++;
      frame&=1023;
    } else {
      subframe++;
    }      

 
    // synchronization on FH interface, acquire signals/data and block
    if (eNB->rx_fh) eNB->rx_fh(eNB,&frame,&subframe);
    else AssertFatal(1==0, "No fronthaul interface : eNB->node_function %d",eNB->node_function);

    T(T_ENB_MASTER_TICK, T_INT(0), T_INT(proc->frame_rx), T_INT(proc->subframe_rx));

    // At this point, all information for subframe has been received on FH interface
    // If this proc is to provide synchronization, do so
    wakeup_slaves(proc);
      
    // wake up RXn_TXnp4 thread for the subframe
    // choose even or odd thread for RXn-TXnp4 processing 
    if (wakeup_rxtx(proc,&proc->proc_rxtx[proc->subframe_rx&1],fp) < 0)
      break;

    // artifical sleep for very slow fronthaul
    if (eNB->frame_parms.N_RB_DL==6)
      rt_sleep_ns(800000LL);
  }
    
  printf( "Exiting FH thread \n");
 
  eNB_thread_FH_status = 0;
  return &eNB_thread_FH_status;
}


/************************************************************************************************************************/
/* this function maps the phy_vars_eNB tx and rx buffers to the available rf chains.
   Each rf chain is is addressed by the card number and the chain on the card. The
   rf_map specifies for each CC, on which rf chain the mapping should start. Multiple
   antennas are mapped to successive RF chains on the same card. */
int setup_eNB_buffers_NB_IoT(PHY_VARS_eNB_NB_IoT **phy_vars_eNB, openair0_config_t *openair0_cfg) {

  int i,j; 
  int CC_id,card,ant;

  //uint16_t N_TA_offset = 0;

  LTE_DL_FRAME_PARMS *frame_parms;

  for (CC_id=0; CC_id<MAX_NUM_CCs; CC_id++) {
    if (phy_vars_eNB[CC_id]) {
      frame_parms = &(phy_vars_eNB[CC_id]->frame_parms);
      printf("setup_eNB_buffers: frame_parms = %p\n",frame_parms);
    } else {
      printf("phy_vars_eNB[%d] not initialized\n", CC_id);
      return(-1);
    }

    /*
    if (frame_parms->frame_type == TDD) {
      if (frame_parms->N_RB_DL == 100)
        N_TA_offset = 624;
      else if (frame_parms->N_RB_DL == 50)
        N_TA_offset = 624/2;
      else if (frame_parms->N_RB_DL == 25)
        N_TA_offset = 624/4;
    }
    */
 

    if (openair0_cfg[CC_id].mmapped_dma == 1) {
    // replace RX signal buffers with mmaped HW versions
      
      for (i=0; i<frame_parms->nb_antennas_rx; i++) {
  card = i/4;
  ant = i%4;
  printf("Mapping eNB CC_id %d, rx_ant %d, on card %d, chain %d\n",CC_id,i,phy_vars_eNB[CC_id]->rf_map.card+card, phy_vars_eNB[CC_id]->rf_map.chain+ant);
  free(phy_vars_eNB[CC_id]->common_vars.rxdata[0][i]);
  phy_vars_eNB[CC_id]->common_vars.rxdata[0][i] = openair0_cfg[phy_vars_eNB[CC_id]->rf_map.card+card].rxbase[phy_vars_eNB[CC_id]->rf_map.chain+ant];
  
  printf("rxdata[%d] @ %p\n",i,phy_vars_eNB[CC_id]->common_vars.rxdata[0][i]);
  for (j=0; j<16; j++) {
    printf("rxbuffer %d: %x\n",j,phy_vars_eNB[CC_id]->common_vars.rxdata[0][i][j]);
    phy_vars_eNB[CC_id]->common_vars.rxdata[0][i][j] = 16-j;
  }
      }
      
      for (i=0; i<frame_parms->nb_antennas_tx; i++) {
  card = i/4;
  ant = i%4;
  printf("Mapping eNB CC_id %d, tx_ant %d, on card %d, chain %d\n",CC_id,i,phy_vars_eNB[CC_id]->rf_map.card+card, phy_vars_eNB[CC_id]->rf_map.chain+ant);
  free(phy_vars_eNB[CC_id]->common_vars.txdata[0][i]);
  phy_vars_eNB[CC_id]->common_vars.txdata[0][i] = openair0_cfg[phy_vars_eNB[CC_id]->rf_map.card+card].txbase[phy_vars_eNB[CC_id]->rf_map.chain+ant];
  
  printf("txdata[%d] @ %p\n",i,phy_vars_eNB[CC_id]->common_vars.txdata[0][i]);
  
  for (j=0; j<16; j++) {
    printf("txbuffer %d: %x\n",j,phy_vars_eNB[CC_id]->common_vars.txdata[0][i][j]);
    phy_vars_eNB[CC_id]->common_vars.txdata[0][i][j] = 16-j;
  }
      }
    }
    else {  // not memory-mapped DMA 
      //nothing to do, everything already allocated in lte_init
      /*
      rxdata = (int32_t**)malloc16(frame_parms->nb_antennas_rx*sizeof(int32_t*));
      txdata = (int32_t**)malloc16(frame_parms->nb_antennas_tx*sizeof(int32_t*));
      
      for (i=0; i<frame_parms->nb_antennas_rx; i++) {
  free(phy_vars_eNB[CC_id]->common_vars.rxdata[0][i]);
  rxdata[i] = (int32_t*)(32 + malloc16(32+frame_parms->samples_per_tti*10*sizeof(int32_t))); // FIXME broken memory allocation
  phy_vars_eNB[CC_id]->common_vars.rxdata[0][i] = rxdata[i]; //-N_TA_offset; // N_TA offset for TDD         FIXME! N_TA_offset > 16 => access of unallocated memory
  memset(rxdata[i], 0, frame_parms->samples_per_tti*10*sizeof(int32_t));
  printf("rxdata[%d] @ %p (%p) (N_TA_OFFSET %d)\n", i, phy_vars_eNB[CC_id]->common_vars.rxdata[0][i],rxdata[i],N_TA_offset);      
      }
      
      for (i=0; i<frame_parms->nb_antennas_tx; i++) {
  free(phy_vars_eNB[CC_id]->common_vars.txdata[0][i]);
  txdata[i] = (int32_t*)(32 + malloc16(32 + frame_parms->samples_per_tti*10*sizeof(int32_t))); // FIXME broken memory allocation
  phy_vars_eNB[CC_id]->common_vars.txdata[0][i] = txdata[i];
  memset(txdata[i],0, frame_parms->samples_per_tti*10*sizeof(int32_t));
  printf("txdata[%d] @ %p\n", i, phy_vars_eNB[CC_id]->common_vars.txdata[0][i]);
      }
      */
    }
  }

  return(0);
}


int start_if_NB_IoT(PHY_VARS_eNB_NB_IoT *eNB) {
  return(eNB->ifdevice.trx_start_func(&eNB->ifdevice));
}

int start_rf_NB_IoT(PHY_VARS_eNB_NB_IoT *eNB) {
  return(eNB->rfdevice.trx_start_func(&eNB->rfdevice));
}






/**************************************************************************************************************************************/





/*!
 * \brief The prach receive thread of eNB.
 * \param param is a \ref eNB_proc_t structure which contains the info what to process.
 * \returns a pointer to an int. The storage is not on the heap and must not be freed.
 */
static void* eNB_thread_prach_NB_IoT( void* param ) {
  static int eNB_thread_prach_status;

  PHY_VARS_eNB_NB_IoT *eNB= (PHY_VARS_eNB_NB_IoT *)param;
  L1_proc_t *proc = &eNB->proc;

  // set default return value
  eNB_thread_prach_status = 0;

  thread_top_init("eNB_thread_prach_NB_IoT",1,500000L,1000000L,20000000L);

  while (!oai_exit) {
    
    if (oai_exit) break;

    if (wait_on_condition(&proc->mutex_prach,&proc->cond_prach,&proc->instance_cnt_prach,"eNB_prach_thread") < 0) break;
    
    //prach_procedures(eNB);
    
    ////// NB_IoT testing ///////
    prach_procedures_NB_IoT(eNB);
    /////////////////////////////
    
    if (release_thread(&proc->mutex_prach,&proc->instance_cnt_prach,"eNB_prach_thread") < 0) break;
  }

  printf( "Exiting eNB thread PRACH\n");

  eNB_thread_prach_status = 0;
  return &eNB_thread_prach_status;
}

extern void eNB_fep_rru_if5_NB_IoT(PHY_VARS_eNB_NB_IoT *eNB,eNB_rxtx_proc_NB_IoT_t *proc_rxtx);
extern void eNB_fep_full_NB_IoT(PHY_VARS_eNB_NB_IoT *eNB,eNB_rxtx_proc_NB_IoT_t *proc_rxtx);
extern void do_prach_NB_IoT(PHY_VARS_eNB_NB_IoT *eNB,int frame,int subframe);


///Modify to NB-IoT merge
void init_eNB_NB_IoT(eNB_func_NB_IoT_t node_function[], eNB_timing_NB_IoT_t node_timing[],int nb_inst,eth_params_t *eth_params,int single_thread_flag,int wait_for_sync) {
  
  int CC_id;
  int inst;
  PHY_VARS_eNB_NB_IoT *eNB;
  int ret;

  for (inst=0;inst<nb_inst;inst++) {
    for (CC_id=0;CC_id<MAX_NUM_CCs;CC_id++) {
      eNB = PHY_vars_eNB_NB_IoT_g[inst][CC_id]; 
      eNB->node_function      = node_function[CC_id];
      eNB->node_timing        = node_timing[CC_id];
      eNB->eth_params         = eth_params+CC_id;
      eNB->abstraction_flag   = 0;
      eNB->single_thread_flag = single_thread_flag;
      eNB->ts_offset          = 0;
      eNB->in_synch           = 0;
      eNB->is_slave           = (wait_for_sync>0) ? 1 : 0;

      /////// IF-Module initialization ///////////////

      LOG_I(PHY,"Registering with MAC interface module start\n");
      AssertFatal((eNB->if_inst_NB_IoT         = IF_Module_init_NB_IoT(inst))!=NULL,"Cannot register interface");
      eNB->if_inst_NB_IoT->schedule_response   = schedule_response_NB_IoT;
      eNB->if_inst_NB_IoT->PHY_config_req      = PHY_config_req_NB_IoT;
      LOG_I(PHY,"Registering with MAC interface module sucessfully\n");


#ifndef OCP_FRAMEWORK
      LOG_I(PHY,"Initializing eNB %d CC_id %d : (%s,%s)\n",inst,CC_id,eNB_functions[node_function[CC_id]],eNB_timing[node_timing[CC_id]]);
#endif

      switch (node_function[CC_id]) {
      case NGFI_RRU_IF5_NB_IoT:
  eNB->do_prach             = NULL;
  eNB->do_precoding         = 0;
  eNB->fep                  = eNB_fep_rru_if5_NB_IoT;
  eNB->td                   = NULL;
  eNB->te                   = NULL;
  eNB->proc_uespec_rx       = NULL;
  eNB->proc_tx              = NULL;
  eNB->tx_fh                = NULL;
  eNB->rx_fh                = rx_rf_NB_IoT;
  eNB->start_rf             = start_rf_NB_IoT;
  eNB->start_if             = start_if_NB_IoT;
  eNB->fh_asynch            = fh_if5_asynch_DL_NB_IoT;
  if (oaisim_flag == 0) {
    ret = openair0_device_load(&eNB->rfdevice, &openair0_cfg[CC_id]);
    if (ret<0) {
      printf("Exiting, cannot initialize rf device\n");
      exit(-1);
    }
  }
  eNB->rfdevice.host_type   = RRU_HOST;
  eNB->ifdevice.host_type   = RRU_HOST;
        ret = openair0_transport_load(&eNB->ifdevice, &openair0_cfg[CC_id], eNB->eth_params);
  printf("openair0_transport_init returns %d for CC_id %d\n",ret,CC_id);
        if (ret<0) {
          printf("Exiting, cannot initialize transport protocol\n");
          exit(-1);
        }
  malloc_IF5_buffer(eNB);
  break;
      case NGFI_RRU_IF4p5_NB_IoT:
  eNB->do_precoding         = 0;
  eNB->do_prach             = do_prach_NB_IoT;
  eNB->fep                  = eNB_fep_full_NB_IoT;//(single_thread_flag==1) ? eNB_fep_full_2thread : eNB_fep_full;
  eNB->td                   = NULL;
  eNB->te                   = NULL;
  eNB->proc_uespec_rx       = NULL;
  eNB->proc_tx              = NULL;//proc_tx_rru_if4p5;
  eNB->tx_fh                = NULL;
  eNB->rx_fh                = rx_rf_NB_IoT;
  eNB->fh_asynch            = fh_if4p5_asynch_DL_NB_IoT;
  eNB->start_rf             = start_rf_NB_IoT;
  eNB->start_if             = start_if_NB_IoT;
  if (oaisim_flag == 0) {
    ret = openair0_device_load(&eNB->rfdevice, &openair0_cfg[CC_id]);
    if (ret<0) {
      printf("Exiting, cannot initialize rf device\n");
      exit(-1);
    }
  }
  eNB->rfdevice.host_type   = RRU_HOST;
  eNB->ifdevice.host_type   = RRU_HOST;
  printf("loading transport interface ...\n");
        ret = openair0_transport_load(&eNB->ifdevice, &openair0_cfg[CC_id], eNB->eth_params);
  printf("openair0_transport_init returns %d for CC_id %d\n",ret,CC_id);
        if (ret<0) {
          printf("Exiting, cannot initialize transport protocol\n");
          exit(-1);
        }

  malloc_IF4p5_buffer(eNB);

  break;
      case eNodeB_3GPP_NB_IoT:
  eNB->do_precoding         = eNB->frame_parms.nb_antennas_tx!=eNB->frame_parms.nb_antenna_ports_eNB;
  eNB->do_prach             = do_prach_NB_IoT;
  eNB->fep                  = eNB_fep_full_NB_IoT;//(single_thread_flag==1) ? eNB_fep_full_2thread : eNB_fep_full;
  eNB->td                   = ulsch_decoding_data_NB_IoT;//(single_thread_flag==1) ? ulsch_decoding_data_2thread : ulsch_decoding_data;
  eNB->te                   = dlsch_encoding_NB_IoT;//(single_thread_flag==1) ? dlsch_encoding_2threads : dlsch_encoding;
  ////////////////////// NB-IoT testing ////////////////////
  //eNB->proc_uespec_rx       = phy_procedures_eNB_uespec_RX;
  eNB->proc_uespec_rx       = phy_procedures_eNB_uespec_RX_NB_IoT;

  eNB->proc_tx              = proc_tx_full_NB_IoT;
  eNB->tx_fh                = NULL;
  eNB->rx_fh                = rx_rf_NB_IoT;
  eNB->start_rf             = start_rf_NB_IoT;
  eNB->start_if             = NULL;
        eNB->fh_asynch            = NULL;
        if (oaisim_flag == 0) {
      ret = openair0_device_load(&eNB->rfdevice, &openair0_cfg[CC_id]);
          if (ret<0) {
            printf("Exiting, cannot initialize rf device\n");
            exit(-1);
          }
        }
  eNB->rfdevice.host_type   = RRU_HOST;
  eNB->ifdevice.host_type   = RRU_HOST;
  break;
      case eNodeB_3GPP_BBU_NB_IoT:
  eNB->do_precoding         = eNB->frame_parms.nb_antennas_tx!=eNB->frame_parms.nb_antenna_ports_eNB;
  eNB->do_prach             = do_prach_NB_IoT;
  eNB->fep                  = eNB_fep_full_NB_IoT;//(single_thread_flag==1) ? eNB_fep_full_2thread : eNB_fep_full;
  eNB->td                   = ulsch_decoding_data_NB_IoT;//(single_thread_flag==1) ? ulsch_decoding_data_2thread : ulsch_decoding_data;
  eNB->te                   = dlsch_encoding_NB_IoT;//(single_thread_flag==1) ? dlsch_encoding_2threads : dlsch_encoding;
  eNB->proc_uespec_rx       = phy_procedures_eNB_uespec_RX;
  eNB->proc_tx              = proc_tx_full_NB_IoT;
        if (eNB->node_timing == synch_to_other) {
           eNB->tx_fh             = tx_fh_if5_mobipass_NB_IoT;
           eNB->rx_fh             = rx_fh_slave_NB_IoT;
           eNB->fh_asynch         = fh_if5_asynch_UL_NB_IoT;

        }
        else {
           eNB->tx_fh             = tx_fh_if5_NB_IoT;
           eNB->rx_fh             = rx_fh_if5_NB_IoT;
           eNB->fh_asynch         = NULL;
        }

  eNB->start_rf             = NULL;
  eNB->start_if             = start_if_NB_IoT;
  eNB->rfdevice.host_type   = RRU_HOST;

  eNB->ifdevice.host_type   = RRU_HOST;

        ret = openair0_transport_load(&eNB->ifdevice, &openair0_cfg[CC_id], eNB->eth_params);
        printf("openair0_transport_init returns %d for CC_id %d\n",ret,CC_id);
        if (ret<0) {
          printf("Exiting, cannot initialize transport protocol\n");
          exit(-1);
        }
  malloc_IF5_buffer(eNB);
  break;
      case NGFI_RCC_IF4p5_NB_IoT:
  eNB->do_precoding         = 0;
  eNB->do_prach             = do_prach_NB_IoT;
  eNB->fep                  = NULL;
  eNB->td                   = ulsch_decoding_data_NB_IoT;//(single_thread_flag==1) ? ulsch_decoding_data_2thread : ulsch_decoding_data;
  eNB->te                   = dlsch_encoding_NB_IoT;//(single_thread_flag==1) ? dlsch_encoding_2threads : dlsch_encoding;
  eNB->proc_uespec_rx       = phy_procedures_eNB_uespec_RX_NB_IoT;
  eNB->proc_tx              = proc_tx_high_NB_IoT;
  eNB->tx_fh                = tx_fh_if4p5_NB_IoT;
  eNB->rx_fh                = rx_fh_if4p5_NB_IoT;
  eNB->start_rf             = NULL;
  eNB->start_if             = start_if_NB_IoT;
        eNB->fh_asynch            = (eNB->node_timing == synch_to_other) ? fh_if4p5_asynch_UL_NB_IoT : NULL;
  eNB->rfdevice.host_type   = RRU_HOST;
  eNB->ifdevice.host_type   = RRU_HOST;
        ret = openair0_transport_load(&eNB->ifdevice, &openair0_cfg[CC_id], eNB->eth_params);
        printf("openair0_transport_init returns %d for CC_id %d\n",ret,CC_id);
        if (ret<0) {
          printf("Exiting, cannot initialize transport protocol\n");
          exit(-1);
        }
  malloc_IF4p5_buffer(eNB);

  break;
      case NGFI_RAU_IF4p5_NB_IoT:
  eNB->do_precoding   = 0;
  eNB->do_prach       = do_prach_NB_IoT;
  eNB->fep            = NULL;

  eNB->td             = ulsch_decoding_data_NB_IoT;//(single_thread_flag==1) ? ulsch_decoding_data_2thread : ulsch_decoding_data;
  eNB->te             = dlsch_encoding_NB_IoT;//(single_thread_flag==1) ? dlsch_encoding_2threads : dlsch_encoding;
  eNB->proc_uespec_rx = phy_procedures_eNB_uespec_RX_NB_IoT;
  eNB->proc_tx        = proc_tx_high_NB_IoT;
  eNB->tx_fh          = tx_fh_if4p5_NB_IoT; 
  eNB->rx_fh          = rx_fh_if4p5_NB_IoT; 
        eNB->fh_asynch      = (eNB->node_timing == synch_to_other) ? fh_if4p5_asynch_UL_NB_IoT : NULL;
  eNB->start_rf       = NULL;
  eNB->start_if       = start_if_NB_IoT;

  eNB->rfdevice.host_type   = RRU_HOST;
  eNB->ifdevice.host_type   = RRU_HOST;
        ret = openair0_transport_load(&eNB->ifdevice, &openair0_cfg[CC_id], eNB->eth_params);
        printf("openair0_transport_init returns %d for CC_id %d\n",ret,CC_id);
        if (ret<0) {
          printf("Exiting, cannot initialize transport protocol\n");
          exit(-1);
        }
  break;  
  malloc_IF4p5_buffer(eNB);

      }

      if (setup_eNB_buffers(PHY_vars_eNB_NB_IoT_g[inst],&openair0_cfg[CC_id])!=0) {
  printf("Exiting, cannot initialize eNodeB Buffers\n");
  exit(-1);
      }
    }

    init_eNB_proc_NB_IoT(inst);
  }

  sleep(1);
  LOG_D(HW,"[lte-softmodem.c] eNB threads created\n");
  

}


void init_eNB_proc_NB_IoT(int inst) {
  
  int i=0;
  int CC_id;
  PHY_VARS_eNB_NB_IoT *eNB;
  eNB_proc_NB_IoT_t *proc;
  L1_rxtx_proc_t *proc_rxtx;
  pthread_attr_t *attr0=NULL,*attr1=NULL,*attr_FH=NULL,*attr_prach=NULL,*attr_asynch=NULL,*attr_single=NULL,*attr_fep=NULL,*attr_td=NULL,*attr_te=NULL,*attr_synch=NULL;

  for (CC_id=0; CC_id<MAX_NUM_CCs; CC_id++) {
    eNB = PHY_vars_eNB_NB_IoT_g[inst][CC_id];
#ifndef OCP_FRAMEWORK
    LOG_I(PHY,"Initializing eNB %d CC_id %d (%s,%s),\n",inst,CC_id,eNB_functions[eNB->node_function],eNB_timing[eNB->node_timing]);
#endif
    proc = &eNB->proc;

    proc_rxtx = proc->proc_rxtx;
    proc_rxtx[0].instance_cnt_rxtx = -1;
    proc_rxtx[1].instance_cnt_rxtx = -1;
    proc->instance_cnt_prach       = -1;
    proc->instance_cnt_FH          = -1;
    proc->instance_cnt_asynch_rxtx = -1;
    proc->CC_id = CC_id;    
    proc->instance_cnt_synch        =  -1;

    proc->first_rx=1;
    proc->first_tx=1;
    proc->frame_offset = 0;

    for (i=0;i<10;i++) proc->symbol_mask[i]=0;

    pthread_mutex_init( &proc_rxtx[0].mutex_rxtx, NULL);
    pthread_mutex_init( &proc_rxtx[1].mutex_rxtx, NULL);
    pthread_cond_init( &proc_rxtx[0].cond_rxtx, NULL);
    pthread_cond_init( &proc_rxtx[1].cond_rxtx, NULL);

    pthread_mutex_init( &proc->mutex_prach, NULL);
    pthread_mutex_init( &proc->mutex_asynch_rxtx, NULL);
    pthread_mutex_init( &proc->mutex_synch,NULL);

    pthread_cond_init( &proc->cond_prach, NULL);
    pthread_cond_init( &proc->cond_FH, NULL);
    pthread_cond_init( &proc->cond_asynch_rxtx, NULL);
    pthread_cond_init( &proc->cond_synch,NULL);

    pthread_attr_init( &proc->attr_FH);
    pthread_attr_init( &proc->attr_prach);
    pthread_attr_init( &proc->attr_synch);
    pthread_attr_init( &proc->attr_asynch_rxtx);
    pthread_attr_init( &proc->attr_single);
    pthread_attr_init( &proc->attr_fep);
    pthread_attr_init( &proc->attr_td);
    pthread_attr_init( &proc->attr_te);
    pthread_attr_init( &proc_rxtx[0].attr_rxtx);
    pthread_attr_init( &proc_rxtx[1].attr_rxtx);
#ifndef DEADLINE_SCHEDULER
    attr0       = &proc_rxtx[0].attr_rxtx;
    attr1       = &proc_rxtx[1].attr_rxtx;
    attr_FH     = &proc->attr_FH;
    attr_prach  = &proc->attr_prach;
    attr_synch  = &proc->attr_synch;
    attr_asynch = &proc->attr_asynch_rxtx;
    attr_single = &proc->attr_single;
    attr_fep    = &proc->attr_fep;
    attr_td     = &proc->attr_td;
    attr_te     = &proc->attr_te; 
#endif

    if (eNB->single_thread_flag==0) {
      //the two threads that manage tw consecutive subframes
      pthread_create( &proc_rxtx[0].pthread_rxtx, attr0, eNB_thread_rxtx_NB_IoT, &proc_rxtx[0] );
      pthread_create( &proc_rxtx[1].pthread_rxtx, attr1, eNB_thread_rxtx_NB_IoT, &proc_rxtx[1] );
      pthread_create( &proc->pthread_FH, attr_FH, eNB_thread_FH_NB_IoT, &eNB->proc );
    }
    else {
      pthread_create(&proc->pthread_single, attr_single, eNB_thread_single_NB_IoT, &eNB->proc);
      init_fep_thread(eNB,attr_fep);
      /*
      init_td_thread(eNB,attr_td);
      init_te_thread(eNB,attr_te);
      */
    }
    pthread_create( &proc->pthread_prach, attr_prach, eNB_thread_prach_NB_IoT, &eNB->proc );
    pthread_create( &proc->pthread_synch, attr_synch, eNB_thread_synch_NB_IoT, eNB);
    if ((eNB->node_timing == synch_to_other) ||
  (eNB->node_function == NGFI_RRU_IF5_NB_IoT) ||
  (eNB->node_function == NGFI_RRU_IF4p5_NB_IoT))


      pthread_create( &proc->pthread_asynch_rxtx, attr_asynch, eNB_thread_asynch_rxtx_NB_IoT, &eNB->proc );

    char name[16];
    if (eNB->single_thread_flag == 0) {
      snprintf( name, sizeof(name), "RXTX0 %d", i );
      pthread_setname_np( proc_rxtx[0].pthread_rxtx, name );
      snprintf( name, sizeof(name), "RXTX1 %d", i );
      pthread_setname_np( proc_rxtx[1].pthread_rxtx, name );
      snprintf( name, sizeof(name), "FH %d", i );
      pthread_setname_np( proc->pthread_FH, name );
    }
    else {
      snprintf( name, sizeof(name), " %d", i );
      pthread_setname_np( proc->pthread_single, name );
    }
  }

  //for multiple CCs: setup master and slaves
 /* 
  for (CC_id=0; CC_id<MAX_NUM_CCs; CC_id++) {
    eNB = PHY_vars_eNB_g[inst][CC_id];

    if (eNB->node_timing == synch_to_ext_device) { //master
      eNB->proc.num_slaves = MAX_NUM_CCs-1;
      eNB->proc.slave_proc = (eNB_proc_t**)malloc(eNB->proc.num_slaves*sizeof(eNB_proc_t*));

      for (i=0; i< eNB->proc.num_slaves; i++) {
        if (i < CC_id)  eNB->proc.slave_proc[i] = &(PHY_vars_eNB_g[inst][i]->proc);
        if (i >= CC_id)  eNB->proc.slave_proc[i] = &(PHY_vars_eNB_g[inst][i+1]->proc);
      }
    }
    }
*/

  /* setup PHY proc TX sync mechanism */
  pthread_mutex_init(&sync_phy_proc.mutex_phy_proc_tx, NULL);
  pthread_cond_init(&sync_phy_proc.cond_phy_proc_tx, NULL);
  sync_phy_proc.phy_proc_CC_id = 0;
}


