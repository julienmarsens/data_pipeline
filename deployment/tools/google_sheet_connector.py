import gspread
from oauth2client.service_account import ServiceAccountCredentials
from ruamel.yaml import YAML
import re
import datetime

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

sheet_url = "https://docs.google.com/spreadsheets/d/1_B5vIrRbgRc2h6XBDCXIP3WVw1KL9EF-miGoKs4mv_w/edit?usp=sharing"
spreadsheet = client.open_by_url(sheet_url)


def extract_trader_number(trader_str):
    match = re.search(r"trader_(\d+)", str(trader_str))
    return int(match.group(1)) if match else None

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

def pull_highest_trader_id(kernel_name):
    """
    Look at 'Production' tab rows where Column C == kernel_name,
    return trader string with highest numeric suffix from Column B.
    """
    production_ws = spreadsheet.worksheet("Production")  # <---- connect to Production tab

    all_trader_ids = production_ws.col_values(2)  # Column B = TraderID
    all_kernels = production_ws.col_values(3)     # Column C = Kernel

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

def format_pair(pair):
    """Convert [DOGEUSDT, AVAXUSDT] -> DOGE-AVAX"""
    return f"{pair[0].replace('USDT','')}-{pair[1].replace('USDT','')}"


def update_relative_or_absolute(template_str, trade_size):
    """
    Replace before-last parameter with trade_size.
    Example: "a#b#c#d#1000#5" -> "a#b#c#d#<trade_size>#5"
    """
    parts = template_str.split("#")
    parts[-2] = str(int(trade_size))  # replace before last with int trade_size
    return "#".join(parts)


def create_new_deployment_tab():
    # 1. copy template tab
    now = datetime.datetime.now()
    tab_name = f"new_deployment {now.month}_{now.day}_{now.hour}_{now.minute}"

    try:
        spreadsheet.worksheet(tab_name)
        spreadsheet.del_worksheet(spreadsheet.worksheet(tab_name))
    except gspread.WorksheetNotFound:
        pass

    template_ws = spreadsheet.worksheet("deployment_template")
    new_ws = template_ws.duplicate(new_sheet_name=tab_name)
    new_ws.clear()  # keep headers clean if template had data
    new_ws.append_row([
        "deployment date", "TraderID", "Kernel", "public_key", "private_key",
        "pass phrase", "Exchange", "Pair", "relative parameters",
        "absolute parameters", "stop loss", "scaling optimization",
        "overall scaling", "fitting_date_start", "fitting_date_end",
        "launch_timestamp", "Action"
    ])

    today_str = datetime.datetime.today().strftime("%d.%m.%Y")

    pairs = config_to_deploy_config["pairs"]
    params = config_to_deploy_config["parameters"]
    signatures = config_to_deploy_config["signature"]
    model_fitting_dates = config_to_deploy_config["model_fitting_dates"]

    for kernel in config_to_deploy_config["target_kernel"]:
        kernel_data = config_mapping[kernel]
        last_trader_str = kernel_data["last_naming"]
        last_num = extract_trader_number(last_trader_str) or 0
        trader_counter = last_num + 1

        for acc_key in ["account_1", "account_2", "account_3"]:
            acc = kernel_data.get(acc_key, {})
            for pair, trade_size_list in zip(acc.get("pairs", []), acc.get("trade_size", [])):
                # each pair line
                pair_index = pairs.index(pair)  # find index in config_to_deploy
                rel_param = update_relative_or_absolute(params[pair_index], trade_size_list[0])
                abs_param = update_relative_or_absolute(signatures[pair_index], trade_size_list[0])

                fit_start, fit_end = model_fitting_dates[pair_index]

                row = [
                    today_str,
                    f"trader_{trader_counter}",
                    kernel,
                    acc.get("public_key", ""),
                    acc.get("private_key", ""),
                    "",  # pass phrase
                    kernel_data.get("exchange", ""),
                    format_pair(pair),
                    rel_param,
                    abs_param,
                    "", "", "",  # stop loss, scaling opt, overall scaling
                    fit_start,
                    fit_end,
                    "", ""  # launch_timestamp, Action
                ]
                new_ws.append_row(row)
                trader_counter += 1

    print("✅ new_deployment tab created and filled")


if __name__ == "__main__":
    create_new_deployment_tab()
