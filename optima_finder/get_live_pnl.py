# pip install mysql-connector-python pandas

import os
import pandas as pd
import mysql.connector
from mysql.connector import errorcode

HOST = "13.115.65.27"
PORT = 3306
DB   = "swl_perf"
USER = "root"
PWD  = os.getenv("DB_PASS", "XXX")  # <-- or set as env var DB_PASS

QUERY = """
SELECT
  TS AS `time`,
  TotalBalance AS PnL_usd
FROM overview
WHERE TraderId = %s
ORDER BY TS
"""

def fetch_pnl(trader_id="trader_434") -> pd.DataFrame:
    conn = None
    try:
        conn = mysql.connector.connect(
            host=HOST, port=PORT, database=DB, user=USER, password=PWD
            # ssl_ca="path/to/ca.pem"  # uncomment if the server requires TLS
        )
        df = pd.read_sql(QUERY, conn, params=(trader_id,))
        # Optional: parse TS to datetime
        if "time" in df.columns:
            df["time"] = pd.to_datetime(df["time"], errors="coerce", utc=True)
        return df
    except mysql.connector.Error as err:
        if err.errno == errorcode.ER_ACCESS_DENIED_ERROR:
            raise RuntimeError("Access denied: check user/password") from err
        raise
    finally:
        if conn is not None and conn.is_connected():
            conn.close()

if __name__ == "__main__":
    df = fetch_pnl()
    print(df.tail())
