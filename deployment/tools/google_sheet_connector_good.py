import gspread
from oauth2client.service_account import ServiceAccountCredentials
from ruamel.yaml import YAML
import re

# ---- SETUP ----
yaml = YAML(typ="rt")  # round-trip
yaml.preserve_quotes = True
yaml.width = 10 ** 6

with open(f'./common/config/local_path.yml', 'r') as f:
    local_path_config = yaml.load(f)

local_google_sheet_credentials_file_path = local_path_config["paths"]["google_api_credentials"]

with open('./deployment/config/config_to_deploy.yml', 'r') as f:
    config_to_deploy_config = yaml.load(f)

with open('./deployment/config/mapping.yml', 'r') as f:
    config_mapping = yaml.load(f)

scope = [
    "https://www.googleapis.com/auth/spreadsheets",
    "https://www.googleapis.com/auth/drive"
]

creds = ServiceAccountCredentials.from_json_keyfile_name(local_google_sheet_credentials_file_path, scope)
client = gspread.authorize(creds)

# sheet_url = "https://docs.google.com/spreadsheets/d/1ztZ5le4ottuTeRqm2AyeaZ4iCPcZFDCZzvLAMzVzMbY/edit?usp=sharing" # PROD
sheet_url = "https://docs.google.com/spreadsheets/d/1_B5vIrRbgRc2h6XBDCXIP3WVw1KL9EF-miGoKs4mv_w/edit?usp=sharing"
sheet = client.open_by_url(sheet_url).sheet1


def extract_trader_number(trader_str):
    """Extract numeric suffix from trader string like 'trader_523'."""
    match = re.search(r"trader_(\d+)", str(trader_str))
    return int(match.group(1)) if match else None


def pull_highest_trader_id(kernel_name):
    """
    Look at sheet rows where Column C == kernel_name,
    return trader string with highest numeric suffix from Column B.
    """
    all_trader_ids = sheet.col_values(2)  # Column B = TraderID
    all_kernels = sheet.col_values(3)     # Column C = Kernel

    trader_ids = []
    for trader, kernel in zip(all_trader_ids[1:], all_kernels[1:]):  # skip headers
        if kernel == kernel_name:
            num = extract_trader_number(trader)
            if num is not None:
                trader_ids.append((trader, num))

    if not trader_ids:
        return None

    max_trader, _ = max(trader_ids, key=lambda x: x[1])
    return max_trader

def update_last_naming():
    """
    Iterate target kernels, pull highest TraderID from sheet,
    update mapping.yml accordingly.
    """
    for kernel in config_to_deploy_config["target_kernel"]:
        trader_str = pull_highest_trader_id(kernel)
        if trader_str:
            print(f"Updating {kernel} last_naming → {trader_str}")
            config_mapping[kernel]["last_naming"] = trader_str

    with open('./deployment/config/mapping.yml', 'w') as f:
        yaml.dump(config_mapping, f)


if __name__ == "__main__":
    update_last_naming()
    print("✅ mapping.yml updated with last_naming per kernel from Google Sheet")

    # python3 deployment/tools/google_sheet_connector.py