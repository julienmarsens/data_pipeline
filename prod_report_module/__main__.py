import sys
import os
import itertools
import math
import subprocess
from datetime import datetime, timedelta
from ruamel.yaml import YAML
from ruamel.yaml.comments import CommentedSeq

from prod_report_module.tools.binance_data_download import SpreadBinanceRestDataDownload
from prod_report_module.tools.live_trading_data_download import get_live_pnl
from common.download_internal_market_data import get_internal_market_data
from common.sync_internal_market_data import sync_pairs

import prod_report_module.tools.build_cointegration_test as cointegration_test
import prod_report_module.tools.build_prod_report_1 as signal_angle
import prod_report_module.tools.google_sheet_connector as sheet_connector


class ProdReportPipeline():

    # python3 -m prod_report_module prod_report

    config_version = str(sys.argv[1])

    yaml = YAML(typ="rt")  # round-trip (preserve formatting/comments)
    yaml.preserve_quotes = True
    yaml.width = 10 ** 6  # avoid wrapping; keep lists on one line

    with open(f'./prod_report_module/config/{config_version}.yml', 'r') as f:
        config = yaml.load(f)

    download_iteration = 0

    if config["pull_parameters"]:

        target_kernel = config["target_kernel"]
        _updates = sheet_connector.pull_prod_config(target_kernel)

        # formatting helper
        @staticmethod
        def to_flow_seq(seq):
            cs = CommentedSeq(seq)
            cs.fa.set_flow_style()
            # also mark inner lists (for pairs, model_fitting_dates, pair_start_times)
            for i, val in enumerate(cs):
                if isinstance(val, list):
                    inner = CommentedSeq(val)
                    inner.fa.set_flow_style()
                    cs[i] = inner
            return cs

        # normalize configs minus sign
        config["pairs"] = to_flow_seq(_updates["pairs"])
        config["configs"] = to_flow_seq([s.replace("−", "-") for s in _updates["configs"]])
        config["trader_ids"] = to_flow_seq(_updates["trader_ids"])
        config["model_fitting_dates"] = to_flow_seq(_updates["model_fitting_dates"])
        config["pair_start_times"] = to_flow_seq(_updates["pair_start_times"])   # NEW

        with open(f"./prod_report_module/config/{config_version}.yml", "w") as f:
            yaml.dump(config, f)

        with open(f'./prod_report_module/config/{config_version}.yml', 'r') as f:
            config = yaml.load(f)

    if config_version == "prod_report":

        _pairs = config["pairs"]
        _configs = config["configs"]
        trader_ids = config["trader_ids"]
        fitting_dates = config["model_fitting_dates"]
        pair_start_times = config["pair_start_times"]   # NEW

        rolling_angle_windows_in_days = config["signal_angle"]["rolling_angle_windows_in_days"]

        download_binance_1m_market_data = config["download_binance_1m_market_data"]
        download_internal_market_data = config["download_internal_market_data"]
        sync_internal_market_data = config["sync_internal_market_data"]
        run_daily_backtest = config["run_daily_backtest"]

        build_signal_angles = True
        build_cointegration_test = False

        half_life_window = config["cointegration_test"]["half_life_window"]
        half_life_step = config["cointegration_test"]["half_life_step"]

        _assets = list({asset for pair in _pairs for asset in pair})

    else:
        sys.exit()

    # Initialize the downloader
    data_download_obj = SpreadBinanceRestDataDownload()

    # Generate all unique 2-asset combinations
    number_of_pairs = len(_pairs)
    print('number_of_pairs: ', number_of_pairs)

    if download_binance_1m_market_data:

        # For each pair, compute how many rows since its start_day
        for pair, pair_start_time in zip(_pairs, pair_start_times):
            start_day, _ = pair_start_time
            date_obj = datetime.strptime(start_day, "%Y_%m_%d")
            now = datetime.now()
            diff = now - date_obj
            minutes_till_now = diff.total_seconds() / 60
            number_of_data_points = int(math.ceil(minutes_till_now / 1000.0)) * 1000

            # add buffer before start
            add_data_prior_to_start = True
            if add_data_prior_to_start:
                number_of_data_points = number_of_data_points + 30*24*60
                number_of_data_points = int(math.ceil(number_of_data_points / 1000.0)) * 1000

            for _asset in pair:
                data_download_obj.download_data(
                    asset=_asset,
                    number_of_rows=number_of_data_points,
                    frequency='1m'
                )

            # merge data for the pair
            print(f"Downloading data for: {pair[0]} - {pair[1]}")
            data_download_obj.merge_data(
                asset_1=pair[0],
                asset_2=pair[1]
            )

            download_iteration += 1
            print(f"merge_iteration: {download_iteration}/{number_of_pairs}")

    if download_internal_market_data:
        get_internal_market_data()

    if sync_internal_market_data:

        dates = [datetime.strptime(d, "%Y_%m_%d") for d, _ in pair_start_times]
        earliest = min(dates)
        earliest_formatted = earliest.strftime("%Y%m%d")

        yesterday = datetime.utcnow() - timedelta(days=1)
        yesterday_formatted = yesterday.strftime("%Y%m%d")

        sync_pairs(
            pairs=_pairs,
            start_date=earliest_formatted,
            end_date=yesterday_formatted,
        )

    if run_daily_backtest:

        for trader_id, pair_start_time in zip(trader_ids, pair_start_times):
            start_day, _ = pair_start_time  # only the date part
            get_live_pnl(
                trader_id,
                start_date_str=start_day
            )

        @staticmethod
        def to_perp_name(symbol: str) -> str:
            """Convert Binance symbol (e.g. 'ADAUSDT') into perp naming convention (e.g. 'adausd-perp')."""
            if not symbol.endswith("USDT"):
                raise ValueError(f"Symbol {symbol} does not end with USDT")
            base = symbol[:-4]  # drop 'USDT'
            return base.lower() + "usd-perp"

        @staticmethod
        def parse_param_string(raw_param: str):
            raw_param = raw_param.replace("−", "-")
            parts = raw_param.split("#")
            out = []
            for x in parts:
                try:
                    out.append(int(x))
                except ValueError:
                    out.append(float(x))
            return out

        with open(f'./common/config/local_path.yml', 'r') as f:
            config = yaml.load(f)

        perso_local_path = config["paths"]["local_user"]
        perso_disk_path = config["paths"]["external_disk"]

        today_date = datetime.today()
        tomorrow_date = today_date + timedelta(days=1)
        tomorrow_str = tomorrow_date.strftime("%Y_%m_%d")

        # Loop with per-pair start times
        for pair, config, trader_id, fitting_date, pair_start_time in zip(_pairs, _configs, trader_ids, fitting_dates, pair_start_times):
            asset_1 = to_perp_name(pair[0])
            asset_2 = to_perp_name(pair[1])
            print(asset_1, asset_2)

            list_param = parse_param_string(config)

            r_script = "./prod_report_module/tools/backtesting/backtesting_engine.R"

            # unpack start_day and start_time
            start_day, start_time = pair_start_time

            argv = [
                "Rscript",
                r_script,
                asset_1,
                asset_2,
                fitting_date[0],
                fitting_date[1],
                start_day,
                tomorrow_str,
                start_time,
                *map(str, list_param),  # numeric params as strings
                trader_id,
                perso_local_path,
                perso_disk_path
            ]

            argv = list(map(str, argv))

            print("argv: ", argv)

            subprocess.run(argv, check=True)

    if build_cointegration_test:

        cointegration_test.loop_over_pairs(
            _pairs=_pairs,
            half_life_window=half_life_window,
            half_life_step=half_life_step
        )

    if build_signal_angles:

        signal_angle.loop_over_pairs(
            pairs=_pairs,
            trader_ids=trader_ids,
            rolling_angle_windows_in_days=rolling_angle_windows_in_days,
            fitting_dates=fitting_dates
        )


if __name__ == "__main__":

    # python3 -m prod_report_module prod_report

    ProdReportPipeline()
