#include "nrscope/hdr/harq_tracking.h"

HarqTracker::HarqTracker(int nof_known_rntis){
  assert(nof_known_rntis >= 0);
  nof_known_rntis = nof_known_rntis;

  dl_ue_ndi.resize(nof_known_rntis);
  for(int i = 0; i < nof_known_rntis; i++){
    dl_ue_ndi[i].resize(16);
    for(int j = 0; j < 16; j++){
      dl_ue_ndi[i][j] = -1;
    }
  }

  ul_ue_ndi.resize(nof_known_rntis);
  for(int i = 0; i < nof_known_rntis; i++){
    ul_ue_ndi[i].resize(16);
    for(int j = 0; j < 16; j++){
      ul_ue_ndi[i][j] = -1;
    }
  }
}

HarqTracker::~HarqTracker(){
  dl_ue_ndi.clear();
  ul_ue_ndi.clear();
}

bool HarqTracker::is_new_data(int ue_id, int ndi, int harq_id, bool is_dl){
  assert(ue_id < nof_known_rntis && harq_id < 16 && (ndi == 1 || ndi == 0));

  if(is_dl){
    assert(dl_ue_ndi[ue_id][harq_id] == -1 || dl_ue_ndi[ue_id][harq_id] == 1 || 
      dl_ue_ndi[ue_id][harq_id] == 0);
    if(dl_ue_ndi[ue_id][harq_id] == -1){ 
      /* no previous ndi value */
      dl_ue_ndi[ue_id][harq_id] = ndi;
      return true;
    }else if(dl_ue_ndi[ue_id][harq_id] == ndi){ 
      /* ndi is not toggled, it's a retransmission */
      dl_ue_ndi[ue_id][harq_id] = ndi;
      return false;
    }else{ 
      /* ndi is toggled, it's a new transmission */
      dl_ue_ndi[ue_id][harq_id] = ndi;
      return true;
    }
  }else{
    assert(ul_ue_ndi[ue_id][harq_id] == -1 || ul_ue_ndi[ue_id][harq_id] == 1 || 
      ul_ue_ndi[ue_id][harq_id] == 0);
    if(ul_ue_ndi[ue_id][harq_id] == -1){ 
      /* no previous ndi value */
      ul_ue_ndi[ue_id][harq_id] = ndi;
      return true;
    }else if(ul_ue_ndi[ue_id][harq_id] == ndi){ 
      /* ndi is not toggled, it's a retransmission */
      ul_ue_ndi[ue_id][harq_id] = ndi;
      return false;
    }else{ 
      /* ndi is toggled, it's a new transmission */
      ul_ue_ndi[ue_id][harq_id] = ndi;
      return true;
    }
  }
}