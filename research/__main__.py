import yaml
import sys
import os
import itertools


from research.tools.binance_data_download import SpreadBinanceRestDataDownload
import research.tools.build_cointegration_test as cointegration_test
import research.tools.build_signal_angle as signal_angle


class ResearchPipeline():

	config_version = str(sys.argv[1])


	file = open(f'./stat_arb_pipeline/config/{config_version}.yml', 'r')
	config = yaml.load(file, Loader=yaml.FullLoader)

	download_iteration = 0

	# config

	saving_path = config["saving_path"]

	_prod = config["production_analysis"]

	rolling_angle_windows_in_days = config["rolling_angle_windows_in_days"]

	download_data = config["download_data"]
	plot_data = config["plot_data"]
	build_cointegration_test = config["build_cointegration_test"]
	build_signal_angles = config["build_signal_angles"]

	number_of_data_points = config["number_of_data_points"]

	half_life_window = config["cointegration_test"]["half_life_window"]
	half_life_step = config["cointegration_test"]["half_life_step"]

	# Initialize the downloader
	data_download_obj = SpreadBinanceRestDataDownload()

	# Generate all unique 2-asset combinations
	if not _prod:
		_assets = config["assets"]
		_pairs = list(itertools.combinations(_assets, 2))
		fitting_dates=None
	else:
		_pairs = config["pairs"]
		fitting_dates = config["fitting_dates"]
		# print(_assets)
		_assets = list({asset for pair in _pairs for asset in pair})
		# print(_assets)

	number_of_pairs = len(_pairs)
	print('number_of_pairs: ', number_of_pairs)

	if download_data:

		for _asset in _assets:

			data_download_obj.download_data(
				asset=_asset,
				number_of_rows=number_of_data_points,
				frequency='1m',
				path=saving_path
			)

		# merge data for each pair
		for asset_1, asset_2 in _pairs:
			print(f"Downloading data for: {asset_1} - {asset_2}")
			data_download_obj.merge_data(
				asset_1=asset_1,
				asset_2=asset_2,
				path=saving_path
			)

			download_iteration += 1
			print(f"merge_iteration: {download_iteration}/{number_of_pairs}")

	if build_cointegration_test:

		cointegration_test.loop_over_pairs(
			_pairs=_pairs,
			half_life_window=half_life_window,
			half_life_step=half_life_step
		)

	if build_signal_angles:

		signal_angle.loop_over_pairs(
			pairs=_pairs,
			rolling_angle_windows_in_days=rolling_angle_windows_in_days,
			fitting_dates=fitting_dates
		)


if __name__ == "__main__":

	# python3 -m stat_arb_pipeline

    ResearchPipeline()
