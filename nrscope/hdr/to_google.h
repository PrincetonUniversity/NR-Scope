#ifndef TO_GOOGLE_H
#define TO_GOOGLE_H

#include "nrscope/hdr/nrscope_def.h"
#include "Python.h"

namespace ToGoogle{

  void init_to_google(std::string google_credential_input, std::string google_project_id_input, std::string google_dataset_id_input);

  void push_google_node(LogNode input_log);

  void to_google_thread();

  void exit_to_google();

};

#endif