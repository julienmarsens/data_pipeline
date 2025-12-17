from .binance_data_download import BinanceRestDataDownload
from .binance_data_download import plot_all_assets_basis_and_funding
from .backtesting_engine import backtest_spot_perp_basis

import pandas as pd
import matplotlib.pyplot as plt
import matplotlib.dates as mdates

# ------------------------------------------- #

# python3 -m funding_rate

_path = "./funding_rate/local_data/merged"

# ------ Action Flags ------ #

download_data = False
plot_data = True
_backtest = True

# -------------------------- #

assets = ["BTC", "ETH", "SOL", "DOGE", "XRP"]
earn_yield = [0.01, 0.015, 0.01, 0.01, 0.01]

# backtest parameters

investment = 100_000  # USD
leverage = 3
annual_borrow_rate = 0.035  # 3.5% per year
_asset_index = 4
trading_fees_bps = 10
number_of_days = 800

AGGREGATED = False
COMPOUNDING_MONTHLY = False

def days_to_hours_multiple_of_1000(days):
    hours = int(days * 24)
    return ((hours + 999) // 1000) * 1000

number_of_hours = days_to_hours_multiple_of_1000(number_of_days)

print("number_of_hours: ",number_of_hours)


# ------------------------------------------- #

dl = BinanceRestDataDownload()
all_data = {}

if download_data:
    for a in assets:
        print(f"\n=== DOWNLOADING {a} ===")
        df = dl.download_full_asset(
            asset=a,
            number_of_rows=number_of_hours,
            frequency=60,
            data_path="./funding_rate/local_data/individual/",
            merged_data_path="./funding_rate/local_data/merged/"
        )

        all_data[a] = df

else:
    # Load existing
    for a in assets:
        all_data[a] = pd.read_pickle(f"{_path}/{a}_full.pkl")

if plot_data:

    for a in assets:
        all_data[a] = pd.read_pickle(f"{_path}/{a}_full.pkl")
    plot_all_assets_basis_and_funding(all_data)

if _backtest:

    # ============================================================
    # SINGLE-ASSET BACKTEST
    # ============================================================
    if not AGGREGATED:
        asset = assets[_asset_index]
        df = pd.read_pickle(f"{_path}/{asset}_full.pkl")

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

            df = pd.read_pickle(f"{_path}/{asset}_full.pkl")

            result = backtest_spot_perp_basis(
                df=df,
                investment=investment,
                leverage=leverage,
                annual_borrow_rate=annual_borrow_rate,
                fee_bps=trading_fees_bps,
                asset_name=asset,
                earn_yield=earn_yield[i],
                COMPOUNDING=COMPOUNDING_MONTHLY,
                plot=False,   # suppress individual plots
            )

            results[asset] = result
            equity_curves.append(result["equity"].rename(asset))

        # --------------------------------------------------------
        # Aggregate equity
        # --------------------------------------------------------
        equity_df = pd.concat(equity_curves, axis=1).sort_index().ffill()
        equity_df["portfolio_equity"] = equity_df.sum(axis=1)

        # --------------------------------------------------------
        # Plot
        # --------------------------------------------------------
        fig, (ax1, ax2) = plt.subplots(
            2, 1, figsize=(16, 10),
            sharex=True,
            gridspec_kw={"height_ratios": [2, 3]}
        )

        for asset in assets:
            ax1.plot(equity_df.index, equity_df[asset], label=asset, linewidth=1.2)

        ax1.set_title("Individual Asset Equity Curves")
        ax1.set_ylabel("Equity (USD)")
        ax1.grid(alpha=0.3)
        ax1.legend(ncol=3)

        ax2.plot(
            equity_df.index,
            equity_df["portfolio_equity"],
            color="black",
            linewidth=2.2,
            label="Portfolio"
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


