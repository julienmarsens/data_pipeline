import sys
import os
import itertools
import subprocess
from datetime import datetime, timedelta
from pathlib import Path
import time
import concurrent.futures
import pandas as pd

from ruamel.yaml import YAML
from ruamel.yaml.comments import CommentedSeq

from common.spread_binance_data_download import SpreadBinanceRestDataDownload
from common.download_internal_market_data import get_internal_market_data
from optima_finder.tools.results_analyser_new import select_best_params, plot_and_save_pnls
from optima_finder.tools.sync_internal_market_data import sync_pairs
import optima_finder.build_signal_angle_10 as signal_angle
import prod_report_module.tools.google_sheet_connector as sheet_connector
from optima_finder.grid_worker import run_grid_search_for_pair
import subprocess
from datetime import datetime

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

		ignore_prod = optima_finder_config["ignore_prod"]

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

		number_of_config_per_pair = optima_finder_config["number_of_config_per_pair"]
		number_of_top_pairs = optima_finder_config["number_of_top_pairs"]
		number_of_days = optima_finder_config["number_of_days"]
		split_date = optima_finder_config["split_date"]

		# Initialize the downloader
		data_download_obj = SpreadBinanceRestDataDownload()

		if ignore_prod:
			print("Ignoring prod pairs as per config.")

			optima_finder_config["prod_pairs"] = []
			with open(f"./optima_finder/config/{config_version}.yml", "w") as f:
				yaml.dump(optima_finder_config, f)
		else:
			print("Not ignoring prod pairs.")

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

		def get_in_out_sample_dates(n_days: int, split_date=None):
			today = datetime.today().date()
			end_day = today - timedelta(days=1)
			start_day = end_day - timedelta(days=n_days - 1)

			fmt = "%Y_%m_%d"

			if split_date == "None":
				# Split evenly in half
				in_sample_len = n_days // 2
				out_sample_len = n_days - in_sample_len

				in_sample_start = start_day
				in_sample_end = start_day + timedelta(days=in_sample_len - 1)

				out_sample_start = in_sample_end + timedelta(days=1)
				out_sample_end = end_day

			else:
				# 🔧 Coerce to date if it's a pandas Timestamp, int, or scalar
				if isinstance(split_date, (pd.Timestamp, datetime)):
					split_day = split_date.date()
				else:
					split_day = datetime.strptime(str(split_date), "%Y%m%d").date()

				in_sample_start = start_day
				in_sample_end = split_day - timedelta(days=1)

				out_sample_start = split_day + timedelta(days=1)
				out_sample_end = end_day

				out_sample_len = (out_sample_end - out_sample_start).days + 1

			return (
				in_sample_start.strftime(fmt),
				in_sample_end.strftime(fmt),
				out_sample_start.strftime(fmt),
				out_sample_end.strftime(fmt),
				out_sample_len,
			)

		in_sample_start_day, \
		in_sample_end_day, \
		out_sample_start_day, \
		out_sample_end_day, \
		out_sample_len	= get_in_out_sample_dates(n_days=number_of_days, split_date=split_date)

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

		if run_grid_search:
			from concurrent.futures import ProcessPoolExecutor, as_completed

			_file = open(f'./optima_finder/results/optima_finder_pairs.yml', 'r')
			optima_finder_pairs_config = yaml.load(_file)
			pairs_to_grid = optima_finder_pairs_config["pairs"]

			_file = open(f'./common/config/local_path.yml', 'r')
			local_path_config = yaml.load(_file)

			perso_local_path = local_path_config["paths"]["local_user"]
			perso_disk_path = local_path_config["paths"]["external_disk"]

			saving_folder_name = datetime.now().strftime("%Y_%m_%d__%H_%M")
			results_path = os.path.join(perso_disk_path, "results")
			full_path = os.path.join(results_path, saving_folder_name)
			os.makedirs(full_path, exist_ok=True)

			# Ensure R parallel packages are installed
			subprocess.run(["Rscript", "-e",
				'if (!require("doParallel", quietly=TRUE)) install.packages(c("doParallel","foreach"), repos="https://cloud.r-project.org")'],
				check=True)

			# Pre-compile and install quoterPkg (C++ functions) before launching workers
			pkg_path = os.path.join(perso_local_path, "data_pipeline", "optima_finder", "tools", "grid_search", "quoterPkg")
			print(f"Installing quoterPkg from {pkg_path}...")
			subprocess.run(["R", "CMD", "INSTALL", "--no-multiarch", pkg_path], check=True)

			print(f"Running grid search for {len(pairs_to_grid)} pairs in parallel...")
			start_time = time.time()

			tasks = [
				(
					pair,
					in_sample_start_day,
					in_sample_end_day,
					out_sample_start_day,
					out_sample_end_day,
					perso_local_path,
					perso_disk_path,
					out_sample_len,
					saving_folder_name
				)
				for pair in pairs_to_grid
			]

			max_workers = min(3, len(tasks))  # Adjust for available cores / R load
			with ProcessPoolExecutor(max_workers=max_workers) as executor:
				futures = {executor.submit(run_grid_search_for_pair, args): args[0] for args in tasks}
				for i, future in enumerate(as_completed(futures), 1):
					pair, error = future.result()
					if error:
						print(f"[ERROR] {pair}: {error}")
					else:
						print(f"[COMPLETE] {i}/{len(pairs_to_grid)} — {pair}")

			print(f"✅ All grid searches completed in {time.time() - start_time:.2f} sec\n")

		if best_parameter_analysis:

			def get_latest_folder(parent_folder: str) -> str:
				parent = Path(parent_folder)
				subdirs = [d for d in parent.iterdir() if d.is_dir()]
				if not subdirs:
					return None
				latest = max(subdirs, key=os.path.getctime)
				# latest = "/Volumes/disk_ext/results/2025_11_27__15_07"
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
					min_sharpe=grid_config["filtering"]["min_sharpe"],
					number_of_config_per_pair=number_of_config_per_pair
				)

				if flag and best:
					sigs = []
					params = []

					for row in best:
						sigs.append(row["absolute.parameters"])
						params.append(
							f"{row['relative.signal.angle']}#"
							f"{row['relative.margin']}#"
							f"{row['relative.step.back']}#"
							f"{row['relative.trading.angle']}#"
							f"{row['relative.order.size']}#"
							f"{row['num.crossing.2.limit']}"
						)

					signatures.append(sigs)
					parameters.append(params)

				else:
					signatures.append(["null"])
					parameters.append(["null"])

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