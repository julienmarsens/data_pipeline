import os
import pandas as pd
import matplotlib.pyplot as plt
import mysql.connector
import yaml
from datetime import datetime

_PLOT = False

def get_live_pnl(trader_version, start_date_str):

    file = open(f'./common/config/server_config.yml', 'r')
    config = yaml.load(file, Loader=yaml.FullLoader)

    # --- Database connection settings ---
    DB_HOST = config["live_monitoring"]["host"]
    DB_PORT = config.get("live_monitoring", {}).get("port", 3306)
    DB_USER = config["live_monitoring"]["user"]
    DB_PASSWORD = config["live_monitoring"]["password"]
    DB_NAME = config["live_monitoring"]["data_base_name"]

    # --- Ensure output dir exists ---
    out_dir = "prod_report_module/local_data/pnl"
    os.makedirs(out_dir, exist_ok=True)

    # --- Connect to MySQL ---
    conn = mysql.connector.connect(
        host=DB_HOST,
        port=DB_PORT,
        user=DB_USER,
        password=DB_PASSWORD,
        database=DB_NAME
    )

    # --- Parse start_date_str ---
    date_obj = datetime.strptime(start_date_str, "%Y_%m_%d")
    start_time_sql = date_obj.strftime("%Y-%m-%d %H:%M:%S")  # MySQL format

    # --- Query 1: PnL ---
    query_pnl = f"""
    SELECT
        TS as time,
        TotalBalance as PnL_usd
    FROM overview
    WHERE TraderId = "{trader_version}"
      AND TS BETWEEN '{start_time_sql}' AND NOW()
    """
    df_pnl = pd.read_sql(query_pnl, conn)

    # --- Query 2: Quote balances (inventory) ---
    query_inv = f"""
    SELECT
        TS as time,
        QuoteBalanceA as QuoteBalanceA_usd,
        QuoteBalanceB as QuoteBalanceB_usd
    FROM overview
    WHERE TraderId = "{trader_version}"
      AND TS BETWEEN '{start_time_sql}' AND NOW()
    """
    df_inv = pd.read_sql(query_inv, conn)

    conn.close()

    # --- Convert time columns to datetime ---
    if not df_pnl.empty:
        df_pnl["time"] = pd.to_datetime(df_pnl["time"])
        df_pnl = df_pnl[["time", "PnL_usd"]]

    if not df_inv.empty:
        df_inv["time"] = pd.to_datetime(df_inv["time"])
        df_inv = df_inv[["time", "QuoteBalanceA_usd", "QuoteBalanceB_usd"]]

    # --- Save pickles ---
    if not df_pnl.empty:
        df_pnl.to_pickle(os.path.join(out_dir, f"{trader_version}_pnl_data.pkl"))
    if not df_inv.empty:
        df_inv.to_pickle(os.path.join(out_dir, f"{trader_version}_inventory_data.pkl"))

    print(f'Data downloaded for: {trader_version}')
    if not df_pnl.empty:
        print("PnL head:\n", df_pnl.head())
    if not df_inv.empty:
        print("Inventory head:\n", df_inv.head())

    # --- Plot: two graphs (PnL and inventories) ---
    if _PLOT:
        fig, (ax1, ax2) = plt.subplots(2, 1, figsize=(11, 8), sharex=True, constrained_layout=True)

        # Graph 1: PnL
        if not df_pnl.empty:
            ax1.plot(df_pnl["time"], df_pnl["PnL_usd"], linestyle="-", label="PnL")
        ax1.set_ylabel("PnL (USD)")
        ax1.set_title(f"PnL over Time — {trader_version}")
        ax1.grid(True)
        ax1.legend(frameon=False)

        # Graph 2: Quote balances
        if not df_inv.empty:
            ax2.plot(df_inv["time"], df_inv["QuoteBalanceA_usd"], linestyle="-", label="QuoteBalanceA (USD)")
            ax2.plot(df_inv["time"], df_inv["QuoteBalanceB_usd"], linestyle="-", label="QuoteBalanceB (USD)")
        ax2.set_xlabel("Time")
        ax2.set_ylabel("Inventory (USD)")
        ax2.set_title(f"Quote Balances — {trader_version}")
        ax2.grid(True)
        ax2.legend(frameon=False)

        plt.show()

    return

# get_live_pnl(trader_version="trader_621", start_date_str="2025_08_07")

# python3 prod_report_module/helpers/live_trading_data_download.py