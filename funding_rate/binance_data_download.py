import requests
import sys
import os
import pandas as pd
import time


class BinanceRestDataDownload:

    def __init__(self):
        pass

    # ===========================================================================
    # MAIN ENTRY (STRUCTURE MATCHES OKX VERSION)
    # ===========================================================================
    def download_full_asset(self, asset, number_of_rows, frequency, data_path, merged_data_path):
        """
        asset = base asset name, e.g. "BTC"
        Automatically downloads:
            - PERP:    BTCUSDT (USDT-M Futures)
            - SPOT:    BTCUSDT
            - Funding: BTCUSDT
        """

        os.makedirs(data_path, exist_ok=True)
        os.makedirs(merged_data_path, exist_ok=True)

        symbol = f"{asset}USDT"

        # -------------------------
        # Download PERP OHLCV
        # -------------------------
        df_perp = self._get_data(
            instrument=symbol,
            frequency=frequency,
            number_of_rows=number_of_rows,
            is_futures=True
        )

        # -------------------------
        # Download SPOT OHLCV
        # -------------------------
        df_spot = self._get_data(
            instrument=symbol,
            frequency=frequency,
            number_of_rows=number_of_rows,
            is_futures=False
        )

        # -------------------------
        # Download Funding
        # -------------------------
        df_funding = self._get_funding_rates(
            asset=symbol,
            max_rows=4000,
            ohlc_start=df_perp.index.min(),
            ohlc_end=df_perp.index.max()
        )

        # -------------------------
        # MERGE (same semantics as OKX)
        # -------------------------
        merged = (
            df_perp.add_prefix("perp_")
            .merge(df_spot.add_prefix("spot_"), left_index=True, right_index=True, how="outer")
            .merge(df_funding, left_index=True, right_index=True, how="left")
        )

        merged["fundingEvent"] = merged["fundingRate"].fillna(0)
        merged["fundingRate"] = merged["fundingRate"].ffill()
        merged = merged.dropna(subset=["fundingRate"])

        save_path = os.path.join(merged_data_path, f"{asset}_full.pkl")
        merged.to_pickle(save_path)

        print(f"[SAVED] FULL → {save_path}")
        return merged

    # ===========================================================================
    # OHLCV (SPOT / PERP)
    # ===========================================================================
    def _get_data(self, instrument, frequency, number_of_rows, is_futures):

        if frequency == 1:
            interval_enum = "1m"
            step_ms = 60_000
        elif frequency == 5:
            interval_enum = "5m"
            step_ms = 5 * 60_000
        elif frequency == 60:
            interval_enum = "1h"
            step_ms = 60 * 60_000
        else:
            raise ValueError("frequency must be 1, 5, or 60")

        if number_of_rows % 1000 != 0:
            raise ValueError("number_of_rows must be divisible by 1000")

        if is_futures:
            url = "https://fapi.binance.com/fapi/v1/klines"
        else:
            url = "https://api.binance.com/api/v3/uiKlines"

        end_time = int(time.time() * 1000)

        df = pd.DataFrame(columns=[
            'open_time', 'open', 'high', 'low', 'close', 'volume',
            'close_time', 'quote_asset_volume', 'number_of_trades',
            'taker_buy_base_asset_volume', 'taker_buy_quote_asset_volume',
            'unused'
        ])

        while len(df) < number_of_rows:
            params = {
                "symbol": instrument,
                "interval": interval_enum,
                "endTime": end_time,
                "limit": 1000
            }

            r = requests.get(url, params=params).json()
            if not r:
                raise RuntimeError(f"No OHLC returned for {instrument}")

            df = pd.concat(
                [df, pd.DataFrame(r, columns=df.columns)],
                ignore_index=True
            ).drop_duplicates(subset="open_time")

            print(f"{instrument} {'PERP' if is_futures else 'SPOT'} rows: {len(df)}")

            end_time -= 1000 * step_ms

        df = df.astype({
            "open_time": float,
            "open": float,
            "high": float,
            "low": float,
            "close": float,
        })

        df.index = pd.to_datetime(df["open_time"], unit="ms")
        df = df.sort_index()

        return df[["open", "high", "low", "close"]]

    # ===========================================================================
    # FUNDING RATES (USDT-M)
    # ===========================================================================
    def _get_funding_rates(self, asset, max_rows, ohlc_start, ohlc_end):

        url = "https://fapi.binance.com/fapi/v1/fundingRate"
        all_rows = []

        start_ms = int(ohlc_start.timestamp() * 1000)
        end_ms = int(ohlc_end.timestamp() * 1000)

        while len(all_rows) < max_rows and start_ms <= end_ms:
            params = {
                "symbol": asset,
                "startTime": start_ms,
                "endTime": end_ms,
                "limit": 1000
            }

            r = requests.get(url, params=params).json()
            if not isinstance(r, list) or not r:
                break

            all_rows.extend(r)

            if len(r) < 1000:
                break

            start_ms = r[-1]["fundingTime"] + 1

        if not all_rows:
            return pd.DataFrame(columns=["fundingRate"])

        df = pd.DataFrame(all_rows)
        df["fundingTime"] = pd.to_datetime(df["fundingTime"], unit="ms")
        df["fundingRate"] = df["fundingRate"].astype(float)

        df = df.drop_duplicates(subset="fundingTime")
        df = df[(df["fundingTime"] >= ohlc_start) & (df["fundingTime"] <= ohlc_end)]
        df = df.set_index("fundingTime").sort_index()

        return df[["fundingRate"]]


def plot_all_assets_basis_and_funding(all_data):
    """
    all_data = { "BTC": df, "ETH": df, ... }

    Each df must contain:
        perp_close
        spot_close
        fundingRate
    """

    import matplotlib.pyplot as plt
    import matplotlib.dates as mdates

    fig, (ax1, ax2) = plt.subplots(
        2, 1,
        figsize=(16, 10),
        sharex=True,
        gridspec_kw={"height_ratios": [3, 2]}
    )

    # ----------------------------------------------------
    # TOP PANEL: % BASIS
    # ----------------------------------------------------
    for asset, df in all_data.items():
        basis = 100 * (df["perp_close"] - df["spot_close"]) / df["spot_close"]
        ax1.plot(df.index, basis, label=asset, linewidth=1.6)

    ax1.set_title("Spot–Perp Percentage Difference (Basis)", fontsize=16, fontweight="bold")
    ax1.set_ylabel("Basis (%)")
    ax1.grid(alpha=0.3)
    ax1.legend(loc="upper left")

    # ----------------------------------------------------
    # BOTTOM PANEL: FUNDING RATE
    # ----------------------------------------------------
    for asset, df in all_data.items():
        ax2.plot(df.index, df["fundingRate"] * 100, label=asset, linewidth=1.4)

    ax2.axhline(0, color="gray", linestyle="--", linewidth=1)
    ax2.set_title("Funding Rate", fontsize=15, fontweight="bold")
    ax2.set_ylabel("Funding (%)")
    ax2.set_xlabel("Date")
    ax2.grid(alpha=0.3)
    ax2.legend(loc="upper left")

    ax2.xaxis.set_major_formatter(mdates.DateFormatter("%Y-%m-%d"))
    plt.xticks(rotation=45)

    plt.tight_layout()
    plt.show()
