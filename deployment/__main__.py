import sys
import os
from datetime import datetime, timedelta
from ruamel.yaml import YAML
import subprocess
import json

from ruamel.yaml.comments import CommentedSeq

import deployment.tools.live_assets_data_download as live_assets_data_download
import deployment.tools.google_sheet_connector as google_sheet_connector
import deployment.tools.config_pusher as config_pusher
from deployment.tools.trade_size_and_account import TradeSizeAccount

class DeploymentPipeline():

	# python3 -m deployment

	# config_version = str(sys.argv[1])

	yaml = YAML(typ="rt")  # round-trip (preserve formatting/comments)
	yaml.preserve_quotes = True
	yaml.width = 10 ** 6  # avoid wrapping; keep lists on one line

	with open(f'./deployment/config/config_to_deploy.yml', 'r') as f:
		config_to_deploy = yaml.load(f)

	with open(f'./deployment/config/mapping.yml', 'r') as f:
		mapping = yaml.load(f)

	target_kernel = config_to_deploy["target_kernel"]
	run_optimization = config_to_deploy["run_optimization"]
	update_client_account_value = config_to_deploy["update_client_account_value"]
	update_trader_naming_convention = config_to_deploy["update_trader_naming_convention"]
	trade_size_and_account_split = config_to_deploy["trade_size_and_account_split"]
	write_to_sheet = config_to_deploy["write_to_sheet"]

	_pairs = config_to_deploy["pairs"]
	_parameters = config_to_deploy["parameters"]
	model_fitting_dates = config_to_deploy["model_fitting_dates"]
	validation_dates = config_to_deploy["validation_dates"]

	if run_optimization:

		in_sample_start_date = model_fitting_dates[0][0]
		in_sample_end_date = model_fitting_dates[0][1]

		out_of_sample_start_date = validation_dates[0][0]
		out_of_sample_end_date = validation_dates[0][1]

		with open(f'./common/config/local_path.yml', 'r') as f:
			config = yaml.load(f)

		perso_local_path = config["paths"]["local_user"]
		perso_disk_path = config["paths"]["external_disk"]

		json_parameters = json.dumps(_parameters)

		@staticmethod
		def to_r_product_pairs(pairs):
			formatted = []
			for a, b in pairs:
				a_r = a.lower().replace("usdt", "usd-perp")
				b_r = b.lower().replace("usdt", "usd-perp")
				formatted.append(f'  c("{a_r}", "{b_r}")')
			return "list(\n" + ",\n".join(formatted) + "\n)"


		r_pairs_str = to_r_product_pairs(_pairs)

		@staticmethod
		def normalize_date(date_str: str) -> str:
			"""Convert 2025_07_01 -> 20250701"""
			return date_str.replace("_", "")

		r_script = "./deployment/tools/optimization/optimizer.R"

		argv = [
			"Rscript",
			r_script,

			normalize_date(in_sample_start_date),
			normalize_date(in_sample_end_date),

			normalize_date(out_of_sample_start_date),
			normalize_date(out_of_sample_end_date),

			perso_local_path,
			perso_disk_path,

			r_pairs_str,
			json_parameters

		]

		argv = list(map(str, argv))

		subprocess.run(argv, check=True)

		config_pusher.run()

	if update_client_account_value:

		total_aum = 0
		# Iterate over all kernels
		for kernel, values in mapping.items():
			if "name" in values:
				client_name = values["name"]
				print(f"Updating {kernel} (name={client_name})")

				assets_usd_equivalent = live_assets_data_download.get_latest_assets(client=client_name)
				values["assets_usd_equivalent"] = assets_usd_equivalent
				total_aum += assets_usd_equivalent if assets_usd_equivalent is not None else 0

		print("Total AuM: ", total_aum)
		# Save back to file with formatting intact
		with open("./deployment/config/mapping.yml", "w") as f:
			yaml.dump(mapping, f)

	if update_trader_naming_convention:

		google_sheet_connector.update_last_naming()

	if trade_size_and_account_split:

		trade_size_and_account_split_obj = TradeSizeAccount()
		trade_size_and_account_split_obj.run()

	if write_to_sheet:
		google_sheet_connector.create_new_deployment_tab()