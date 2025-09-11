import os
import pandas as pd
import matplotlib.pyplot as plt
import mysql.connector
import yaml
from datetime import datetime

def get_latest_assets(client):

    file = open(f'./common/config/server_config.yml', 'r')
    config = yaml.load(file, Loader=yaml.FullLoader)

    # --- Database connection settings ---
    DB_HOST = config["live_monitoring"]["host"]
    DB_PORT = config.get("live_monitoring", {}).get("port", 3306)
    DB_USER = config["live_monitoring"]["user"]
    DB_PASSWORD = config["live_monitoring"]["password"]
    DB_NAME = config["live_monitoring"]["data_base_name"]

    # --- Connect to MySQL ---
    CONN = mysql.connector.connect(
        host=DB_HOST,
        port=DB_PORT,
        user=DB_USER,
        password=DB_PASSWORD,
        database=DB_NAME
    )

    # --- Query only the last row ---
    query_equity = """
        SELECT accountEquity
        FROM allocatorExec 
        WHERE allocator = %s
        ORDER BY TS DESC
        LIMIT 1
    """
    cursor = CONN.cursor()
    cursor.execute(query_equity, (client,))
    row = cursor.fetchone()
    cursor.close()
    try:
        print("USD: ",int(row[0]))
    except:
        print(f"ERROR in getting {client} value")

    return int(row[0]) if row else None

# get_latest_assets(client="arcs")

# python3 deployment/tools/live_assets_data_download.py