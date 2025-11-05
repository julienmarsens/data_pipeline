import gspread
from oauth2client.service_account import ServiceAccountCredentials
import datetime
import pprint
from ruamel.yaml import YAML

# ---- SETUP ----
yaml = YAML()

with open(f'./common/config/local_path.yml', 'r') as f:
    config = yaml.load(f)

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
    pair_start_times = []   # <--- NEW LIST

    def parse_dt(s):
        return datetime.datetime.strptime(s, "%H:%M %d.%m.%Y")

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

            # parse and store as [day, time] directly from sheet
            dt = parse_dt(row['launch_timestamp'])
            pair_start_times.append([dt.strftime("%Y_%m_%d"), dt.strftime("%H:%M:%S")])

    # ---- RESULT ----
    result = {
        "kernel": kernel,
        "pairs": pairs,
        "configs": configs,
        "trader_ids": trader_ids,
        "model_fitting_dates": model_fitting_dates,
        "pair_start_times": pair_start_times   # per-pair timestamps only
    }

    # ---- PRETTY PRINT ----
    pp = pprint.PrettyPrinter(indent=2, width=100)
    pp.pprint(result)

    return result


# pull_prod_config("Kernel_4")
