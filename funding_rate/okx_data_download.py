import requests
import sys
import os
import pandas as pd
import time
import matplotlib.pyplot as plt
import matplotlib.dates as mdates

class OKXRestDataDownload:

    def __init__(self):
        self.BASE = "https://www.okx.com"
        self.RATE_LIMIT_SLEEP = 0.25     # OKX = 10 req / 2 sec

    # ===========================================================================
    # MAIN ENTRY
    # ===========================================================================

    def download_full_asset(self, asset, number_of_rows, frequency, data_path, merged_data_path):
        """
        asset = base asset name, e.g. "BTC"
        Automatically downloads:
            - PERP:    BTC-USD-SWAP
            - SPOT:    BTC-USDT
            - Funding: BTC-USD-SWAP
        And merges them into one DataFrame with aligned timestamps.
        """

        perp = f"{asset}-USD-SWAP"
        spot = f"{asset}-USDT"

        # -------------------------
        # Download PERP OHLCV
        # -------------------------
        df_perp = self._get_data(
            instrument=perp,
            frequency=frequency,
            number_of_rows=number_of_rows
        )

        print("len df_perp: ", len(df_perp))

        # -------------------------
        # Download SPOT OHLCV
        # -------------------------
        df_spot = self._get_data(
            instrument=spot,
            frequency=frequency,
            number_of_rows=number_of_rows
        )

        print("len df_spot: ", len(df_spot))


        # -------------------------
        # Download Funding (PERP only)
        # -------------------------
        df_funding = self._get_funding_rates(
            asset=perp,
            ohlc_start=df_perp.index.min(),
            ohlc_end=df_perp.index.max()
        )

        print("len df_funding: ", len(df_funding))


        # -------------------------
        # Merge PERP + SPOT + FUNDING
        # -------------------------
        merged = (
            df_perp.add_prefix("perp_")
                .merge(df_spot.add_prefix("spot_"), left_index=True, right_index=True, how="outer")
                .merge(df_funding, left_index=True, right_index=True, how="left")
        )

        # ---------------------------------------
        # Create raw funding event column
        # ---------------------------------------
        # fundingEvent has the *actual* funding at event timestamps
        # all other timestamps are 0
        merged["fundingEvent"] = merged["fundingRate"].fillna(0)

        # ---------------------------------------
        # Forward-fill the original fundingRate
        # ---------------------------------------
        merged["fundingRate"] = merged["fundingRate"].ffill()

        # Optional: ensure no missing funding values remain
        merged = merged.dropna(subset=["fundingRate"])

        # store
        os.makedirs(merged_data_path, exist_ok=True)
        save_path = os.path.join(merged_data_path, f"{asset}_full.pkl")
        merged.to_pickle(save_path)

        print(merged)

        print(f"[SAVED] FULL → {save_path}")
        return merged

    # ===========================================================================
    # OHLCV (OKX)
    # ===========================================================================
    def _get_data(self, instrument, frequency, number_of_rows):

        if frequency == 1:
            bar = "1m"
        elif frequency == 5:
            bar = "5m"
        elif frequency == 60:
            bar = "1H"
        else:
            raise ValueError("frequency must be 1, 5, or 60")

        url = f"{self.BASE}/api/v5/market/history-mark-price-candles"

        rows = []
        before = None
        while len(rows) < number_of_rows:
            params = {"instId": instrument, "bar": bar, "limit": 100}
            if before is not None:
                params["after"] = before

            r = requests.get(url, params=params).json()
            batch = r.get("data", [])
            if not batch:
                break

            rows.extend(batch)

            if len(batch) < 100:
                break

            before = int(batch[-1][0])  # oldest candle

        if len(rows) == 0:
            raise RuntimeError("No OHLCV returned from OKX")

        df = pd.DataFrame(rows, columns=[
            'ts', 'open', 'high', 'low', 'close', 'confirm'
        ])

        df["ts"] = pd.to_datetime(df["ts"].astype(int), unit="ms")
        df = df.sort_values("ts")

        df = df.rename(columns={"ts": "open_time"})
        df[["open", "high", "low", "close"]] = df[["open", "high", "low", "close"]].astype(float)

        # set index
        df.index = df["open_time"]

        return df[["open", "high", "low", "close"]]

    # ===========================================================================
    # FUNDING RATES (OKX)
    # ===========================================================================
    def _get_funding_rates(self, asset, ohlc_start, ohlc_end):

        url = f"{self.BASE}/api/v5/public/funding-rate-history"

        all_rows = []
        before = None

        while True:
            params = {"instId": asset, "limit": 400}
            if before:
                params["before"] = before

            r = requests.get(url, params=params).json()
            batch = r.get("data", [])
            if not batch:
                break

            all_rows.extend(batch)

            # pagination cursor: oldest entry
            oldest = int(batch[-1]["fundingTime"])

            if len(batch) < 400:
                break

            before = oldest

            # stop once we have paged *past* the earliest OHLC timestamp
            if oldest < int(ohlc_start.timestamp() * 1000):
                break

        if len(all_rows) == 0:
            print(f"[INFO] No funding data for {asset}")
            return pd.DataFrame(columns=["fundingRate"])

        df = pd.DataFrame(all_rows)
        df["fundingTime"] = pd.to_datetime(df["fundingTime"], unit="ms")
        df["fundingRate"] = df["fundingRate"].astype(float)

        df = df[(df["fundingTime"] >= ohlc_start) & (df["fundingTime"] <= ohlc_end)]

        df = df.set_index("fundingTime").sort_index()

        return df[["fundingRate"]]



    # ===========================================================================
    # PLOT: PRICE + FUNDING
    # ===========================================================================

def plot_all_assets_basis_and_funding(all_data):
    """
    all_data = { "BTC": df, "ETH": df, ... }

    df must contain:
        perp_close, spot_close, fundingRate
    """

    import matplotlib.pyplot as plt
    import matplotlib.dates as mdates

    fig, (ax1, ax2) = plt.subplots(
        2, 1, figsize=(16, 10),
        sharex=True,
        gridspec_kw={"height_ratios": [3, 2]}
    )

    # ----------------------------------------------------
    # TOP PANEL: % DIFFERENCE (BASIS)
    # ----------------------------------------------------
    for asset, df in all_data.items():
        basis = 100 * (df["perp_close"] - df["spot_close"]) / df["spot_close"]
        ax1.plot(df.index, basis, label=asset, linewidth=1.6)

    ax1.set_title("Spot–Perp Percentage Difference (Basis) — All Assets", fontsize=16, fontweight="bold")
    ax1.set_ylabel("Basis (%)")
    ax1.grid(alpha=0.3)
    ax1.legend(loc="upper left")

    # ----------------------------------------------------
    # BOTTOM PANEL: FUNDING RATE (%)
    # ----------------------------------------------------
    for asset, df in all_data.items():
        ax2.plot(df.index, df["fundingRate"] * 100, label=asset, linewidth=1.4)

    ax2.axhline(0, color="gray", linestyle="--", linewidth=1)
    ax2.set_title("Funding Rate — All Assets", fontsize=15, fontweight="bold")
    ax2.set_ylabel("Funding (%)")
    ax2.set_xlabel("Date")
    ax2.grid(alpha=0.3)
    ax2.legend(loc="upper left")

    # Date formatting
    ax2.xaxis.set_major_formatter(mdates.DateFormatter("%Y-%m-%d"))
    plt.xticks(rotation=45)

    plt.tight_layout()
    plt.show()



