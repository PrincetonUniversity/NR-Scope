#include "nrscope/hdr/nrscope_def.h"

double get_now_timestamp_in_double(){
  std::chrono::microseconds us = std::chrono::duration_cast< 
    std::chrono::microseconds >(std::chrono::system_clock::now().
    time_since_epoch());

  return ((double) us.count()) / 1000000.0; 
  // + ((double) (us.count() % 1000000)/1000000.0);
}

const char* sch_mapping_to_str(srsran_sch_mapping_type_t mapping)
{
  switch (mapping) {
    case srsran_sch_mapping_type_A:
      return "A";
    case srsran_sch_mapping_type_B:
      return "B";
    default:; // Do nothing
  }
  return "invalid";
}

const char* sch_xoverhead_to_str(srsran_xoverhead_t xoverhead)
{
  switch (xoverhead) {
    case srsran_xoverhead_0:
      return "0";
    case srsran_xoverhead_6:
      return "6";
    case srsran_xoverhead_12:
      return "12";
    case srsran_xoverhead_18:
      return "18";
    default:; // Do nothing
  }
  return "invalid";
}

void my_sig_handler(int s){
  printf("Caught signal %d\n",s);
  exit(0); 
}

uint32_t get_P(uint32_t bwp_nof_prb, bool config_1_or_2)
{
  srsran_assert(bwp_nof_prb > 0 and bwp_nof_prb <= 275, "Invalid BWP size");
  if (bwp_nof_prb <= 36) {
    return config_1_or_2 ? 2 : 4;
  }
  if (bwp_nof_prb <= 72) {
    return config_1_or_2 ? 4 : 8;
  }
  if (bwp_nof_prb <= 144) {
    return config_1_or_2 ? 8 : 16;
  }
  return 16;
}

/* TS 38.214 - total number of RBGs for a uplink bandwidth part of size 
"bwp_nof_prb" PRBs */
uint32_t get_nof_rbgs(uint32_t bwp_nof_prb, 
                      uint32_t bwp_start, 
                      bool config1_or_2)
{
  uint32_t P = get_P(bwp_nof_prb, config1_or_2);
  return srsran::ceil_div(bwp_nof_prb + (bwp_start % P), P);
}

bool CompareSlotResult (SlotResult a, SlotResult b) {
  /* Return true if a < b */
  /* If the sf_round is different */
  if (a.sf_round < b.sf_round) return true;
  if (a.sf_round > b.sf_round) return false;

  /* If the sfn is different */
  if (a.outcome.sfn < b.outcome.sfn) return true;
  if (a.outcome.sfn > b.outcome.sfn) return false;
  
  /* If the sfn is the same */
  return a.slot.idx < b.slot.idx;
}