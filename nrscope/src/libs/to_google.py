from google.cloud import bigquery
from google.cloud.exceptions import NotFound
from datetime import datetime
import time
import geocoder
import os

def create_table_with_position_and_time(credential, dataset_id_input, nof_usrp):
  try:
    # Get geolocation of the user
    geo_loc = geocoder.ip('me').latlng
    print(geo_loc)
    geo_str = ""
    if(float(geo_loc[0]) >= 0):
      geo_str += "P_" + str(int(geo_loc[0])) + "_" + "{:.4f}".format(
        geo_loc[0] - int(geo_loc[0]))[2:]
    else:
      geo_str += "N_" + str(int(abs(geo_loc[0]))) + "_" + "{:.4f}".format(
        abs(geo_loc[0] - int(geo_loc[0])))[2:]

    if(geo_loc[1] >= 0):
      geo_str += "_P_" + str(int(geo_loc[1])) + "_" + "{:.4f}".format(
        geo_loc[1] - int(geo_loc[1]))[2:]
    else:
      geo_str += "_N_" + str(int(abs(geo_loc[1]))) + "_" + "{:.4f}".format(
        abs(geo_loc[1] - int(geo_loc[1])))[2:]
    print("Current location is: ", geo_str)

    # Get current datetime
    current_date_and_time = datetime.now()
    current_time = current_date_and_time.strftime('%Y_%m_%d_%H_%M_%S_%f')
    print("The current time is", current_time) 

    # Construct a BigQuery client object, check the existence of a bigquery dataset.
    os.environ['GOOGLE_APPLICATION_CREDENTIALS'] = credential

    tables = []
    clients = []
    for usrp_id in range(nof_usrp):
      client = bigquery.Client()
      dataset_id = "{}.{}".format(client.project, dataset_id_input)
      try:
        client.get_dataset(dataset_id)  # Make an API request.
        print("Dataset {} already exists".format(dataset_id))
      except NotFound:
        dataset = bigquery.Dataset(dataset_id)
        dataset.location = "US"
        dataset = client.create_dataset(dataset, timeout=30)  # Make an API request.
        print("Dataset {} is not found, creating".format(dataset_id))

      # TODO(developer): Set table_id to the ID of the table to create.
      table_id = "{}.{}".format(dataset_id, geo_str+"_"+current_time+"_"+str(usrp_id))

      schema = [
          bigquery.SchemaField("timestamp", "FLOAT", mode="REQUIRED"),
          bigquery.SchemaField("system_frame_index", "INTEGER", mode="REQUIRED"),
          bigquery.SchemaField("slot_index", "INTEGER", mode="REQUIRED"),
          bigquery.SchemaField("rnti", "INTEGER", mode="REQUIRED"),
          bigquery.SchemaField("rnti_type", "STRING", mode="REQUIRED"),
          bigquery.SchemaField("dci_format", "STRING", mode="REQUIRED"),
          bigquery.SchemaField("k", "INTEGER", mode="REQUIRED"),
          bigquery.SchemaField("mapping", "STRING", mode="REQUIRED"),
          bigquery.SchemaField("time_start", "INTEGER", mode="REQUIRED"),
          bigquery.SchemaField("time_length", "INTEGER", mode="REQUIRED"),
          bigquery.SchemaField("frequency_start", "INTEGER", mode="REQUIRED"),
          bigquery.SchemaField("frequency_length", "INTEGER", mode="REQUIRED"),
          bigquery.SchemaField("nof_dmrs_cdm_groups", "INTEGER", mode="REQUIRED"),
          bigquery.SchemaField("beta_dmrs", "FLOAT", mode="REQUIRED"),
          bigquery.SchemaField("nof_layers", "INTEGER", mode="REQUIRED"),
          bigquery.SchemaField("n_scid", "INTEGER", mode="REQUIRED"),
          bigquery.SchemaField("tb_scaling_field", "INTEGER", mode="REQUIRED"),
          bigquery.SchemaField("modulation", "STRING", mode="REQUIRED"),
          bigquery.SchemaField("mcs_index", "INTEGER", mode="REQUIRED"),
          bigquery.SchemaField("transport_block_size", "INTEGER", mode="REQUIRED"),
          bigquery.SchemaField("code_rate", "FLOAT", mode="REQUIRED"),
          bigquery.SchemaField("redundancy_version", "INTEGER", mode="REQUIRED"),
          bigquery.SchemaField("new_data_indicator", "INTEGER", mode="REQUIRED"),
          bigquery.SchemaField("nof_re", "INTEGER", mode="REQUIRED"),
          bigquery.SchemaField("nof_bits", "INTEGER", mode="REQUIRED"),
          bigquery.SchemaField("mcs_table", "STRING", mode="REQUIRED"),
          bigquery.SchemaField("xoverhead", "STRING", mode="REQUIRED"),
          bigquery.SchemaField("harq_id", "INTEGER", mode="REQUIRED"),
          bigquery.SchemaField("downlink_assignment_index", "INTEGER", mode="REQUIRED"),
          bigquery.SchemaField("tpc", "INTEGER", mode="REQUIRED"),
          bigquery.SchemaField("pucch_resource", "INTEGER", mode="REQUIRED"),
          bigquery.SchemaField("harq_feedback", "INTEGER", mode="REQUIRED"),
      ]

      table = bigquery.Table(table_id, schema=schema)
      table = client.create_table(table)  # Make an API request.
      tables.append(table)
      clients.append(client)

      print(
        "Create table {}.{}.{}".format(table.project, table.dataset_id, 
                                       table.table_id)
      )
      rows = []
    return clients, tables, schema, rows, nof_usrp

  except:
    print('Caught exceptions.')
    return

def push_data_to_table(clients, tables, schema, rows_input, rf_index):
  try:
    tic = time.perf_counter()
    batch_size = len(rows_input)
    clients[rf_index].insert_rows(tables[rf_index], rows_input, schema)
      
    toc = time.perf_counter()
    print("Pushed {} entries to the google storage, taking {} s.".format(
      len(rows_input), toc-tic))
 
  except:
    print('Caught exceptions.')
    return

if __name__ == '__main__':
  client, table, schema, rows = create_table_with_position_and_time()
  push_data_to_table(client, table, schema, rows)