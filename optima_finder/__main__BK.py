import sys
import os
import itertools
import subprocess
from datetime import datetime, timedelta
from pathlib import Path
import time
import concurrent.futures

from ruamel.yaml import YAML
from ruamel.yaml.comments import CommentedSeq

from common.spread_binance_data_download import SpreadBinanceRestDataDownload
from common.download_internal_market_data import get_internal_market_data
from optima_finder.tools.results_analyser import select_best_params, plot_and_save_pnls
from optima_finder.tools.sync_internal_market_data import sync_pairs
import optima_finder.build_signal_angle_10 as signal_angle
import prod_report_module.tools.google_sheet_connector as sheet_connector


TODO: "get prod pairs from google sheet + add optima_pairs_in git"


class OptimaFinderPipeline():

	def __init__(self):

		yaml = YAML(typ="rt")  # round-trip (preserve formatting/comments)
		yaml.preserve_quotes = True
		yaml.width = 10 ** 6  # avoid wrapping; keep lists on one line

		config_version = str(sys.argv[1])

		with open(f'./optima_finder/config/{config_version}.yml', 'r') as f:
			optima_finder_config = yaml.load(f)

		download_iteration = 0

		# config

		rolling_angle_windows_in_days = optima_finder_config["rolling_angle_windows_in_days"]
		volatility_window_minutes = optima_finder_config["volatility_window_minutes"]

		if optima_finder_config["from_scratch"] :

			pull_prod_pairs = True
			download_1m_binance_data = True
			download_internal_data = True
			build_signal_angles = True
			sync_internal_data = True
			run_grid_search = True
			best_parameter_analysis = True

		else:
			pull_prod_pairs = optima_finder_config["pull_prod_pairs"]
			download_1m_binance_data = optima_finder_config["download_1m_binance_data"]
			download_internal_data = optima_finder_config["download_internal_data"]
			build_signal_angles = optima_finder_config["find_best_pairs"]
			sync_internal_data = optima_finder_config["sync_internal_data"]
			run_grid_search = optima_finder_config["run_grid_search"]
			best_parameter_analysis = optima_finder_config["best_parameter_analysis"]

		number_of_top_pairs = optima_finder_config["number_of_top_pairs"]
		number_of_days = optima_finder_config["number_of_days"]

		# Initialize the downloader
		data_download_obj = SpreadBinanceRestDataDownload()

		if pull_prod_pairs:
			_updates = sheet_connector.pull_prod_config("Kernel_4")

			# formatting helper
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

			optima_finder_config["prod_pairs"] = to_flow_seq(_updates["pairs"])

			with open(f"./optima_finder/config/{config_version}.yml", "w") as f:
				yaml.dump(optima_finder_config, f)

		number_of_data_points = round(number_of_days * 24 * 60, -3)

		# Generate all unique 2-asset combinations
		_assets = optima_finder_config["assets"]
		_pairs = list(itertools.combinations(_assets, 2))

		# define first
		prod_pairs = [tuple(p) for p in optima_finder_config.get("prod_pairs", [])]
		print("prod_pairs: ", prod_pairs)

		# then filter
		_pairs = [p for p in _pairs if p not in prod_pairs]
		fitting_dates = None
		print("_pairs: ", _pairs)
		number_of_pairs = len(_pairs)
		print('number_of_pairs: ', number_of_pairs)

		def get_in_out_sample_dates(n_days: int):
			today = datetime.today().date()
			end_day = today - timedelta(days=1)  # yesterday is the last day
			start_day = end_day - timedelta(days=n_days - 1)

			# split evenly in half
			in_sample_len = n_days // 2
			out_sample_len = n_days - in_sample_len

			in_sample_start = start_day
			in_sample_end = start_day + timedelta(days=in_sample_len - 1)

			out_sample_start = in_sample_end + timedelta(days=1)
			out_sample_end = end_day

			fmt = "%Y_%m_%d"
			return (in_sample_start.strftime(fmt),
			        in_sample_end.strftime(fmt),
			        out_sample_start.strftime(fmt),
			        out_sample_end.strftime(fmt),
			        out_sample_len
			)

		in_sample_start_day, \
		in_sample_end_day, \
		out_sample_start_day, \
		out_sample_end_day, \
		out_sample_len	= get_in_out_sample_dates(n_days=number_of_days)

		if download_1m_binance_data:

			"""for _asset in _assets:

				data_download_obj.download_data(
					asset=_asset,
					number_of_rows=number_of_data_points,
					frequency='1m'
				)

			# merge data for each pair
			for asset_1, asset_2 in _pairs:
				print(f"Downloading data for: {asset_1} - {asset_2}")
				data_download_obj.merge_data(
					asset_1=asset_1,
					asset_2=asset_2
				)

				download_iteration += 1
				print(f"merge_iteration: {download_iteration}/{number_of_pairs}")"""

			from concurrent.futures import ThreadPoolExecutor, as_completed

			def download_single_asset(asset):
				try:
					data_download_obj.download_data(
						asset=asset,
						number_of_rows=number_of_data_points,
						frequency='1m'
					)
					return asset, None
				except Exception as e:
					return asset, str(e)

			def merge_pair(pair):
				asset_1, asset_2 = pair
				try:
					print(f"Merging data for: {asset_1} - {asset_2}")
					data_download_obj.merge_data(asset_1=asset_1, asset_2=asset_2)
					return pair, None
				except Exception as e:
					return pair, str(e)

			print(f"Downloading {len(_assets)} Binance assets in parallel...")
			start_time = time.time()

			# --- Parallel asset downloads ---
			with ThreadPoolExecutor(max_workers=min(8, len(_assets))) as executor:
				futures = {executor.submit(download_single_asset, asset): asset for asset in _assets}
				for future in as_completed(futures):
					asset, error = future.result()
					if error:
						print(f"[ERROR] {asset}: {error}")
					else:
						print(f"[DONE] {asset}")

			print(f"All downloads completed in {time.time() - start_time:.2f} sec\n")

			# --- Parallel pair merges ---
			print(f"Merging {len(_pairs)} pairs in parallel...")
			start_time = time.time()

			with ThreadPoolExecutor(max_workers=min(8, len(_pairs))) as executor:
				futures = {executor.submit(merge_pair, pair): pair for pair in _pairs}
				for i, future in enumerate(as_completed(futures), 1):
					pair, error = future.result()
					if error:
						print(f"[ERROR] {pair}: {error}")
					else:
						print(f"[DONE] merge {i}/{len(_pairs)} for {pair}")

			print(f"All merges completed in {time.time() - start_time:.2f} sec\n")

		if build_signal_angles:

			signal_angle.loop_over_pairs(
				pairs=_pairs,
				rolling_angle_windows_in_days=rolling_angle_windows_in_days,
				fitting_dates=fitting_dates,
				vol_window_minutes=volatility_window_minutes,
				top_n=number_of_top_pairs
			)

		if download_internal_data:
			get_internal_market_data()
			time.sleep(5)

		if sync_internal_data:
			sync_pairs(
				start_date=str(datetime.strptime(in_sample_start_day.replace("_", "-"), "%Y-%m-%d").date()),
				end_date= str(datetime.strptime(out_sample_end_day.replace("_", "-"), "%Y-%m-%d").date())
			)

		run_grid_search_new = False
		if run_grid_search_new:

			def to_perp_name(symbol: str) -> str:
				if not symbol.endswith("USDT"):
					raise ValueError(f"Symbol {symbol} does not end with USDT")
				_base = symbol[:-4]
				return _base.lower() + "usd-perp"

			_file = open(f'./optima_finder/results/optima_finder_pairs.yml', 'r')
			optima_finder_pairs_config = yaml.load(_file)
			pairs_to_grid = optima_finder_pairs_config["pairs"]

			_file = open(f'./common/config/local_path.yml', 'r')
			local_path_config = yaml.load(_file)

			perso_local_path = local_path_config["paths"]["local_user"]
			perso_disk_path = local_path_config["paths"]["external_disk"]

			saving_folder_name = datetime.now().strftime("%Y_%m_%d__%H_%M")
			results_path = perso_disk_path + "/results"
			full_path = os.path.join(results_path, saving_folder_name)
			os.makedirs(full_path, exist_ok=True)

			def run_rscript(pair_to_grid):
				asset_1 = to_perp_name(pair_to_grid[0])
				asset_2 = to_perp_name(pair_to_grid[1])
				r_script = "./optima_finder/tools/grid_search/grid_engine.R"
				grid_config_version = "grid_config"
				argv = [
					"Rscript",
					r_script,
					asset_1,
					asset_2,
					in_sample_start_day,
					in_sample_end_day,
					out_sample_start_day,
					out_sample_end_day,
					perso_local_path,
					perso_disk_path,
					grid_config_version,
					out_sample_len,
					saving_folder_name
				]
				argv = list(map(str, argv))
				print("argv:", argv)
				subprocess.run(argv, check=True)
				return pair_to_grid

			# Adjust max_workers to your CPU/R resource limits
			with concurrent.futures.ProcessPoolExecutor(max_workers=4) as executor:
				futures = [executor.submit(run_rscript, pair) for pair in pairs_to_grid]
				for f in concurrent.futures.as_completed(futures):
					try:
						result = f.result()
						print(f"Completed: {result}")
					except Exception as e:
						print(f"Error: {e}")

		run_grid_search_old = False

		if run_grid_search:

			def to_perp_name(symbol: str) -> str:
				"""Convert Binance symbol (e.g. 'ADAUSDT') into perp naming convention (e.g. 'adausd-perp').
				"""
				if not symbol.endswith("USDT"):
					raise ValueError(f"Symbol {symbol} does not end with USDT")
				_base = symbol[:-4]  # drop 'USDT'
				return _base.lower() + "usd-perp"

			_file = open(f'./optima_finder/results/optima_finder_pairs.yml', 'r')
			optima_finder_pairs_config = yaml.load(_file)
			pairs_to_grid = optima_finder_pairs_config["pairs"]

			_file = open(f'./common/config/local_path.yml', 'r')
			local_path_config = yaml.load(_file)

			perso_local_path = local_path_config["paths"]["local_user"]
			perso_disk_path = local_path_config["paths"]["external_disk"]

			saving_folder_name = datetime.now().strftime("%Y_%m_%d__%H_%M")
			results_path = perso_disk_path + "/results"
			full_path = os.path.join(results_path, saving_folder_name)

			# Create the folder for this run
			os.makedirs(full_path, exist_ok=True)

			for pair_to_grid in pairs_to_grid:
				asset_1 = to_perp_name(pair_to_grid[0])
				asset_2 = to_perp_name(pair_to_grid[1])

				r_script = "./optima_finder/tools/grid_search/grid_engine.R"

				grid_config_version = "grid_config"

				argv = [
					"Rscript",
					r_script,
					asset_1, # 1
					asset_2,
					in_sample_start_day,
					in_sample_end_day,
					out_sample_start_day,
					out_sample_end_day,
					perso_local_path,
					perso_disk_path,
					grid_config_version,
					out_sample_len,
					saving_folder_name
				]

				argv = list(map(str, argv))

				print("argv: ", argv)

				subprocess.run(argv, check=True)

		if best_parameter_analysis:

			def get_latest_folder(parent_folder: str) -> str:
				parent = Path(parent_folder)
				subdirs = [d for d in parent.iterdir() if d.is_dir()]
				if not subdirs:
					return None
				latest = max(subdirs, key=os.path.getctime)
				return str(latest)

			def get_gs_files(folder: str):
				folder_path = Path(folder)
				gs_files = [f.name for f in folder_path.iterdir() if f.is_file() and f.name.startswith("gs_")]
				return sorted(gs_files)  # keep consistent order

			# --- load configs ---
			with open('./common/config/local_path.yml', 'r') as f:
				local_path_config = yaml.load(f)

			results_path = os.path.join(local_path_config["paths"]["external_disk"], "results")
			target_result_path = get_latest_folder(results_path)

			files = get_gs_files(target_result_path)

			with open('./optima_finder/config/grid_config.yaml', 'r') as f:
				grid_config = yaml.load(f)

			signatures = []
			parameters = []

			for f in files:
				best, flag = select_best_params(
					config_version= config_version,
					csv_path=os.path.join(target_result_path, f),
					min_r2=grid_config["filtering"]["minimum_pnl_curve_r2"],
					min_num_trades=out_sample_len * grid_config["filtering"]["min_crossing_per_day"],
					min_sharpe=grid_config["filtering"]["min_sharpe"],
				)

				if flag and best:
					# Valid result → extract parameters
					signatures.append(best["absolute.parameters"])
					parameters.append(
						f"{best['relative.signal.angle']}#{best['relative.margin']}#{best['relative.step.back']}#"
						f"{best['relative.trading.angle']}#{best['relative.order.size']}#{best['num.crossing.2.limit']}"
					)
				else:
					# No valid result → keep placeholder
					signatures.append("null")
					parameters.append("null")

			# --- write to YAML ---
			yaml.default_flow_style = True  # force inline list format
			yaml.preserve_quotes = True
			yaml.width = 10 ** 6

			yaml_path = "./optima_finder/results/optima_finder_pairs.yml"
			output_pdf_path = "optima_finder/results/pnl_plot_results.pdf"

			# Load existing file
			with open(yaml_path, "r") as f:
				data = yaml.load(f)

			# Update only the two keys
			data["parameters"] = parameters
			data["signature"] = signatures

			# Write back
			with open(yaml_path, "w") as f:
				yaml.dump(data, f)


			plot_and_save_pnls(
				yaml_path=yaml_path,
				folder=target_result_path,
				output_pdf_path=output_pdf_path
			)


if __name__ == "__main__":

	# python3 -m optima_finder master

    OptimaFinderPipeline()