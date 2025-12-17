from .binance_data_download import BinanceRestDataDownload
from .binance_data_download import plot_all_assets_basis_and_funding
from .backtesting_engine import backtest_spot_perp_basis

import pandas as pd
import matplotlib.pyplot as plt
import matplotlib.dates as mdates

# ------------------------------------------- #
# python3 -m funding_rate
# ------------------------------------------- #

_PATH = "./funding_rate/local_data/merged"

# ------ Action Flags ------ #
download_data = False
plot_data = True
_backtest = True
# -------------------------- #

assets = ["BTC", "ETH", "SOL", "DOGE", "XRP"]
earn_yield = [0.01, 0.015, 0.01, 0.01, 0.01]

# ------ Backtest Parameters ------ #
investment = 100_000  # USD
leverage = 3
annual_borrow_rate = 0.035
_asset_index = 0
trading_fees_bps = 10
number_of_days = 800

AGGREGATED = False
COMPOUNDING_MONTHLY = False

# Backtest start control
# "YYYY_MM_DD" or "max"
start_date = "2025_01_01"


# ------------------------------------------- #
# Utilities
# ------------------------------------------- #

def days_to_hours_multiple_of_1000(days: int) -> int:
    hours = int(days * 24)
    return ((hours + 999) // 1000) * 1000


def apply_start_date(df: pd.DataFrame, start_date: str) -> pd.DataFrame:
    if start_date == "max":
        return df
    start_ts = pd.to_datetime(start_date, format="%Y_%m_%d")
    return df.loc[df.index >= start_ts]


number_of_hours = days_to_hours_multiple_of_1000(number_of_days)
print("number_of_hours:", number_of_hours)

# ------------------------------------------- #
# Data Download / Load
# ------------------------------------------- #

dl = BinanceRestDataDownload()
all_data: dict[str, pd.DataFrame] = {}

if download_data:
    for asset in assets:
        print(f"\n=== DOWNLOADING {asset} ===")
        df = dl.download_full_asset(
            asset=asset,
            number_of_rows=number_of_hours,
            frequency=60,
            data_path="./funding_rate/local_data/individual/",
            merged_data_path="./funding_rate/local_data/merged/",
        )
        df = apply_start_date(df, start_date)
        all_data[asset] = df
else:
    for asset in assets:
        df = pd.read_pickle(f"{_PATH}/{asset}_full.pkl")
        df = apply_start_date(df, start_date)
        all_data[asset] = df

# ------------------------------------------- #
# Plot Basis & Funding
# ------------------------------------------- #

if plot_data:
    plot_all_assets_basis_and_funding(all_data)

# ------------------------------------------- #
# Backtesting
# ------------------------------------------- #

if _backtest:

    # ============================================================
    # SINGLE-ASSET BACKTEST
    # ============================================================
    if not AGGREGATED:
        asset = assets[_asset_index]

        df = pd.read_pickle(f"{_PATH}/{asset}_full.pkl")
        df = apply_start_date(df, start_date)

        backtest_spot_perp_basis(
            df=df,
            investment=investment,
            leverage=leverage,
            annual_borrow_rate=annual_borrow_rate,
            fee_bps=trading_fees_bps,
            asset_name=asset,
            earn_yield=earn_yield[_asset_index],
            COMPOUNDING=COMPOUNDING_MONTHLY,
            plot=True,
        )

    # ============================================================
    # AGGREGATED PORTFOLIO BACKTEST
    # ============================================================
    else:
        results = {}
        equity_curves = []

        for i, asset in enumerate(assets):
            print(f"\n=== BACKTESTING {asset} ===")

            df = pd.read_pickle(f"{_PATH}/{asset}_full.pkl")
            df = apply_start_date(df, start_date)

            result = backtest_spot_perp_basis(
                df=df,
                investment=investment,
                leverage=leverage,
                annual_borrow_rate=annual_borrow_rate,
                fee_bps=trading_fees_bps,
                asset_name=asset,
                earn_yield=earn_yield[i],
                COMPOUNDING=COMPOUNDING_MONTHLY,
                plot=False,
            )

            results[asset] = result
            equity_curves.append(result["equity"].rename(asset))

        # --------------------------------------------------------
        # Aggregate Equity
        # --------------------------------------------------------
        equity_df = (
            pd.concat(equity_curves, axis=1)
            .sort_index()
            .ffill()
        )
        equity_df["portfolio_equity"] = equity_df.sum(axis=1)

        # --------------------------------------------------------
        # Plot
        # --------------------------------------------------------
        fig, (ax1, ax2) = plt.subplots(
            2,
            1,
            figsize=(16, 10),
            sharex=True,
            gridspec_kw={"height_ratios": [2, 3]},
        )

        for asset in assets:
            ax1.plot(
                equity_df.index,
                equity_df[asset],
                label=asset,
                linewidth=1.2,
            )

        ax1.set_title("Individual Asset Equity Curves")
        ax1.set_ylabel("Equity (USD)")
        ax1.grid(alpha=0.3)
        ax1.legend(ncol=3)

        ax2.plot(
            equity_df.index,
            equity_df["portfolio_equity"],
            linewidth=2.2,
            label="Portfolio",
        )

        ax2.set_title("Aggregated Portfolio Equity Curve")
        ax2.set_ylabel("Equity (USD)")
        ax2.set_xlabel("Time")
        ax2.grid(alpha=0.3)
        ax2.legend()

        ax2.xaxis.set_major_formatter(mdates.DateFormatter("%Y-%m-%d"))
        plt.xticks(rotation=45)

        plt.tight_layout()
        plt.show()
