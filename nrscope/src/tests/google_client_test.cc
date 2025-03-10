#define PY_SSIZE_T_CLEAN
#include <Python.h>
#include <vector>

int main(int argc, char *argv[])
{
	PyObject *pName, *pModule, *pCreate, *pPush;
	PyObject *pClient, *pDict, *pList, *pDataId;
	PyObject *pInt, *pDouble, *pStr, *pInput;
	std::vector<PyObject *> pLists;

	setenv("PYTHONPATH", ".", 0);
	pLists.resize(3);

	Py_Initialize();
	pName = PyUnicode_FromString((char*)"to_google");

	pModule = PyImport_Import(pName);
	Py_DECREF(pName);

	if (pModule != NULL) {
		pCreate = PyObject_GetAttrString(pModule, "create_table_with_position_and_time");
		pPush = PyObject_GetAttrString(pModule, "push_data_to_table");
		/* pFunc is a new reference */

		if (pCreate && PyCallable_Check(pCreate)) {
			printf("Creating table...\n");
			pStr = PyUnicode_FromString("/home/wanhr/Downloads/nsf-2223556-222187-02b924918c95.json");
			pDataId = PyUnicode_FromString("ngscope5g_dci_log_wanhr");
			pInt = PyLong_FromLong(3);
			pInput = PyTuple_Pack(3, pStr, pDataId, pInt);
			pClient = PyObject_CallObject(pCreate, pInput);

			if (pClient != NULL) {
				if (pPush && PyCallable_Check(pPush)){
					// Initialize and set values of dictionary.

					pDict = PyDict_New();
					pDouble = PyFloat_FromDouble(100.1);
					PyDict_SetItemString(pDict, "timestamp", pDouble);
					pInt = PyLong_FromLong(100);
					PyDict_SetItemString(pDict, "system_frame_index", pInt);
					pInt = PyLong_FromLong(12);
					PyDict_SetItemString(pDict, "slot_index", pInt);
					pInt = PyLong_FromLong(17042);
					PyDict_SetItemString(pDict, "rnti", pInt);
					pStr = PyUnicode_FromString("c-rnti");
					PyDict_SetItemString(pDict, "rnti_type", pStr);
					pStr = PyUnicode_FromString("1_1");
					PyDict_SetItemString(pDict, "dci_format", pStr);
					pInt = PyLong_FromLong(0);
					PyDict_SetItemString(pDict, "k", pInt);
					pStr = PyUnicode_FromString("A");
					PyDict_SetItemString(pDict, "mapping", pStr);
					pInt = PyLong_FromLong(0);
					PyDict_SetItemString(pDict, "time_start", pInt);
					pInt = PyLong_FromLong(12);
					PyDict_SetItemString(pDict, "time_length", pInt);
					pInt = PyLong_FromLong(0);
					PyDict_SetItemString(pDict, "frequency_start", pInt);
					pInt = PyLong_FromLong(1);
					PyDict_SetItemString(pDict, "frequency_length", pInt);
					pInt = PyLong_FromLong(1);
					PyDict_SetItemString(pDict, "nof_dmrs_cdm_groups", pInt);
					pDouble = PyFloat_FromDouble(1.14);
					PyDict_SetItemString(pDict, "beta_dmrs", pDouble);
					pInt = PyLong_FromLong(2);
					PyDict_SetItemString(pDict, "nof_layers", pInt);
					pInt = PyLong_FromLong(1);
					PyDict_SetItemString(pDict, "n_scid", pInt);
					pInt = PyLong_FromLong(1);
					PyDict_SetItemString(pDict, "tb_scaling_field", pInt);
					pStr = PyUnicode_FromString("16QAM");
					PyDict_SetItemString(pDict, "modulation", pStr);
					pInt = PyLong_FromLong(8);
					PyDict_SetItemString(pDict, "mcs_index", pInt);
					pInt = PyLong_FromLong(512);
					PyDict_SetItemString(pDict, "transport_block_size", pInt);
					pDouble = PyFloat_FromDouble(0.516);
					PyDict_SetItemString(pDict, "code_rate", pDouble);
					pInt = PyLong_FromLong(1);
					PyDict_SetItemString(pDict, "redundancy_version", pInt);
					pInt = PyLong_FromLong(1);
					PyDict_SetItemString(pDict, "new_data_indicator", pInt);
					pInt = PyLong_FromLong(12);
					PyDict_SetItemString(pDict, "nof_re", pInt);
					pInt = PyLong_FromLong(132);
					PyDict_SetItemString(pDict, "nof_bits", pInt);
					pStr = PyUnicode_FromString("256QAM");
					PyDict_SetItemString(pDict, "mcs_table", pStr);
					pInt = PyLong_FromLong(0);
					PyDict_SetItemString(pDict, "xoverhead", pInt);

					pList = PyList_New(8000);
					pLists[0] = PyList_New(8000);
					pLists[2] = PyList_New(8000);
					for (int i = 0; i < 8000; i++){
						PyList_SetItem(pLists[0], i, pDict);
						PyList_SetItem(pLists[2], i, pDict);
					}
					if(pDict != NULL && pList != NULL){
						PyTuple_SetItem(pClient, 3, pLists[0]);
						pInt = PyLong_FromLong(0);
						PyTuple_SetItem(pClient, 4, pInt);
						PyObject_CallObject(pPush, pClient);
						pInt = PyLong_FromLong(2);
						PyTuple_SetItem(pClient, 4, pInt);
						PyObject_CallObject(pPush, pClient);
						pInt = PyLong_FromLong(0);
						PyTuple_SetItem(pClient, 4, pInt);
						PyObject_CallObject(pPush, pClient);
						printf("Pushing data...\n");
						Py_DECREF(pClient);
					}else{
						fprintf(stderr,"Creating new dict failed\n");
					}
				}else{
					Py_DECREF(pPush);
					fprintf(stderr,"Call push failed\n");
				}
			}
			else {
				Py_DECREF(pCreate);
				Py_DECREF(pModule);
				PyErr_Print();
				fprintf(stderr,"Call failed\n");
				return 1;
			}
		}
		else {
			if (PyErr_Occurred())
					PyErr_Print();
			fprintf(stderr, "Cannot find function \"%s\"\n", argv[2]);
		}
		Py_XDECREF(pCreate);
		Py_XDECREF(pPush);
		Py_DECREF(pModule);
	}
	else {
		PyErr_Print();
		fprintf(stderr, "Failed to load \"%s\"\n", argv[1]);
		return 1;
	}
	if (Py_FinalizeEx() < 0) {
		return 120;
	}
	return 0;
}
