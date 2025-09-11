import requests
import sys
import os
import pandas as pd
import plotly.graph_objects as go
import matplotlib.pyplot as plt
import yaml
import shutil, random
import time
import matplotlib.dates as mdates


try:
    from urllib import urlencode
except ImportError:
    from urllib.parse import urlencode


class SpreadBinanceRestDataDownload:

    def download_data(
            self,
            asset,
            number_of_rows,
            frequency
    ):
        df = self._get_data(
            instrument=asset,
            frequency=frequency,
            number_of_rows=number_of_rows
        )

        df.to_pickle(f"./prod_report_module/local_data/individual_market_data/{asset}_data.pkl")
        print(f'Data downloaded for: {asset}')

    def merge_data(self, asset_1, asset_2):

        df_1 = pd.read_pickle((f"./prod_report_module/local_data/individual_market_data/{asset_1}_data.pkl"))
        df_2 = pd.read_pickle((f"./prod_report_module/local_data/individual_market_data/{asset_2}_data.pkl"))

        # Reset index to bring datetime back into a column named 'open_time'
        df_1 = df_1.reset_index().rename(columns={'index': 'open_time'})
        df_2 = df_2.reset_index().rename(columns={'index': 'open_time'})

        # Merge on open_time
        merged_df = pd.merge(df_1, df_2, on='open_time', how='inner', suffixes=('_1', '_2'))

        # merged_df["spread_pct"] = merged_df["close_1"] / merged_df["close_2"]

        # Compute raw spread
        merged_df['spread'] = merged_df['close_1'] - merged_df['close_2']

        # Option 1: Normalized by price
        merged_df['normalized_spread'] = merged_df['spread'] / merged_df['close_2']

        merged_df.to_pickle(f"./prod_report_module/local_data/sync_market_data/{asset_1}_{asset_2}_data.pkl")
        print('Data handling done and saved: ', merged_df)


    def _get_data(
            self,
            instrument,
            frequency,
            number_of_rows
    ):
        _url = 'https://api.binance.com'
        _endpoint = '/api/v3/uiKlines'
        total_endpoint = _url + _endpoint

        if number_of_rows % 1000 > 0:
            print('ERROR: number_of_rows must be multiple of 1000!')
            sys.exit()

        _last = int(time.time()*1000)  # now in ms

        df = pd.DataFrame(columns=[
            'open_time',
            'open',
            'high',
            'low',
            'close',
            'volume',
            'close_time',
            'quote_asset_volume',
            'number_of_trades',
            'taker_buy_base_asset_volume',
            'taker_buy_quote_asset_volume',
            'unused_field'
        ])

        while len(df) < number_of_rows:
            params = {
                "symbol": instrument,
                "interval": frequency,
                "endTime": _last,
                "limit": 1000
            }

            query = urlencode(params)

            resp = requests.request('GET', total_endpoint + "?" + query)
            resp = resp.json()

            df = df.append(
                pd.DataFrame(resp, columns=[
                    'open_time',
                    'open',
                    'high',
                    'low',
                    'close',
                    'volume',
                    'close_time',
                    'quote_asset_volume',
                    'number_of_trades',
                    'taker_buy_base_asset_volume',
                    'taker_buy_quote_asset_volume',
                    'unused_field'
                ]),
                ignore_index=True
            )

            print(len(df))

            if len(df) == 0:
                print("Symbol ERROR")
                sys.exit()

            if frequency == '1s':
                _last -= 1_000_000  # 1000 seconds
            elif frequency == '1m':
                _last -= 1_000_000 * 60  # 1000 minutes
            else:
                print('ERROR: frequency issue!')
                sys.exit()

        df = df[['open_time', 'close']]
        df['open_time'] = df['open_time'].astype(float)
        df['close'] = df['close'].astype(float)

        # Convert open_time to datetime and set as index, then drop open_time column
        df.index = pd.to_datetime(df['open_time'], unit='ms')
        df.drop(columns=['open_time'], inplace=True)

        return df

    def download_data_by_pair(
            self,
            asset_1,
            asset_2,
            number_of_rows,
            frequency,
            path,
            plot_spead=False
    ):
        df_1 = self._get_data(
            instrument=asset_1,
            frequency=frequency,
            number_of_rows=number_of_rows,
            path=path
        )

        print("data asset 1 done")

        df_2 = self._get_data(
            instrument=asset_2,
            frequency=frequency,
            number_of_rows=number_of_rows,
            path=path
        )

        print("data asset 2 done")

        # Reset index to bring datetime back into a column named 'open_time'
        df_1 = df_1.reset_index().rename(columns={'index': 'open_time'})
        df_2 = df_2.reset_index().rename(columns={'index': 'open_time'})

        # Merge on open_time
        merged_df = pd.merge(df_1, df_2, on='open_time', how='inner', suffixes=('_1', '_2'))

        # merged_df["spread_pct"] = merged_df["close_1"] / merged_df["close_2"]

        # Compute raw spread
        merged_df['spread'] = merged_df['close_1'] - merged_df['close_2']

        # Option 1: Normalized by price
        merged_df['normalized_spread'] = merged_df['spread'] / merged_df['close_2']

        if plot_spead:
            self._plot_data(merged_df)

        merged_df.to_pickle(f"{path}{asset_1}_{asset_2}_data.pkl")
        print('Data handling done and saved: ', merged_df)

    def _plot_data(self, merged_df):

        # Ensure open_time is the index and it's a datetime
        merged_df['open_time'] = pd.to_datetime(merged_df['open_time'])
        merged_df.set_index('open_time', inplace=True)
        merged_df.sort_index(inplace=True)

        # Plot
        plt.figure(figsize=(12, 6))
        plt.plot(merged_df['normalized_spread'], label='Normalized Spread')

        # Formatting the x-axis
        plt.gca().xaxis.set_major_formatter(mdates.DateFormatter('%Y-%m-%d %H:%M'))
        plt.gca().xaxis.set_major_locator(mdates.AutoDateLocator())

        plt.xticks(rotation=45)
        plt.title('Normalized Spread Over Time')
        plt.xlabel('Time')
        plt.ylabel('Normalized Spread')
        plt.legend()
        plt.grid(True)
        plt.tight_layout()
        plt.show()


if __name__ == "__main__":

    saving_path = f'./common/data/binance/spread/'

    _obj = SpreadBinanceRestDataDownload()

    _obj.download_data_by_pair(
        asset_1='SOLUSDT',
        asset_2='DOGEUSDT',
        number_of_rows=10_000,
        frequency='1s',
        path=saving_path,
        plot_spead=True
    )

    # To run: python -m common.spread_binance_data_download
