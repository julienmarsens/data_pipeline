from .binance_data_download import BinanceRestDataDownload
from .binance_data_download import plot_all_assets_basis_and_funding
from .backtesting_engine import backtest_spot_perp_basis

import pandas as pd

# ------------------------------------------- #

# python3 -m funding_rate

_path = "./funding_rate/local_data/merged"

download_data = False
plot_data = False
_backtest = True

assets = ["BTC", "ETH", "SOL", "DOGE", "XRP"]
earn_yield = [0.01, 0.05, 0.055, 0.01, 0.01]

# backtest parameters

investment = 100_000  # USD
leverage = 4
annual_borrow_rate = 0.035  # 3.5% per year
_asset_index = 0
trading_fees_bps = 10
number_of_days = 800

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

    df = pd.read_pickle(f"{_path}/{assets[_asset_index]}_full.pkl")

    result_btc = backtest_spot_perp_basis(
        df=df,
        investment=investment,
        leverage=leverage,
        annual_borrow_rate=annual_borrow_rate,
        fee_bps=trading_fees_bps,
        asset_name=assets[_asset_index],
        earn_yield=earn_yield[_asset_index]
    )

