import gspread
from oauth2client.service_account import ServiceAccountCredentials
import datetime
import pprint
from ruamel.yaml import YAML

# ---- SETUP ----

with open(f'./common/config/local_path.yml', 'r') as f:
    config = YAML.load(f)

local_google_sheet_credentials_file_path = config["paths"]["google_api_credentials"]

scope = [
    "https://www.googleapis.com/auth/spreadsheets",
    "https://www.googleapis.com/auth/drive"
]

creds = ServiceAccountCredentials.from_json_keyfile_name(local_google_sheet_credentials_file_path, scope)
client = gspread.authorize(creds)

sheet_url = "https://docs.google.com/spreadsheets/d/1ztZ5le4ottuTeRqm2AyeaZ4iCPcZFDCZzvLAMzVzMbY/edit?usp=sharing"
sheet = client.open_by_url(sheet_url).sheet1

def pull_prod_config(kernel):

    # ---- READ ALL ROWS ----
    rows = sheet.get_all_records()

    pairs = []
    configs = []
    trader_ids = []
    model_fitting_dates = []
    timestamps = []

    for row in rows:
        if row['Kernel'] == kernel:
            trader_ids.append(row['TraderID'])

            # Convert "DOGE-AVAX" -> ['DOGEUSDT', 'AVAXUSDT']
            raw_pair = row['Pair']
            t1, t2 = raw_pair.split("-")
            pairs.append([t1 + "USDT", t2 + "USDT"])

            # ---- adjust relative parameters using absolute ----
            rel_parts = str(row['relative parameters']).split("#")
            abs_parts = str(row['absolute parameters']).split("#")

            # replace 2nd before last value in relative with absolute’s
            rel_parts[-2] = abs_parts[-2]

            # join back to string
            new_rel = "#".join(rel_parts)
            configs.append(new_rel)

            model_fitting_dates.append([row['fitting_date_start'], row['fitting_date_end']])
            timestamps.append(row['launch_timestamp'])

    # ---- Compute earliest timestamp ----
    def parse_dt(s):
        return datetime.datetime.strptime(s, "%H:%M %d.%m.%Y")

    earliest_timestamp = min(parse_dt(ts) for ts in timestamps)

    live_pnl_report_span = {
        "start_day": earliest_timestamp.strftime("%Y_%m_%d"),
        "start_time": earliest_timestamp.strftime("%H:%M:%S")
    }

    # ---- RESULT ----
    result = {
        "kernel": kernel,
        "live_pnl_report_start": live_pnl_report_span,
        "pairs": pairs,
        "configs": configs,
        "trader_ids": trader_ids,
        "model_fitting_dates": model_fitting_dates
    }

    # ---- PRETTY PRINT ----
    pp = pprint.PrettyPrinter(indent=2, width=100)
    pp.pprint(result)

    return result

# pull_prod_config("Kernel_4")
