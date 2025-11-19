# optima_finder/grid_worker.py
import subprocess

def run_grid_search_for_pair(args):
    pair_to_grid, in_sample_start_day, in_sample_end_day, out_sample_start_day, out_sample_end_day, \
        perso_local_path, perso_disk_path, out_sample_len, saving_folder_name = args

    def to_perp_name(symbol: str) -> str:
        if not symbol.endswith("USDT"):
            raise ValueError(f"Symbol {symbol} does not end with USDT")
        _base = symbol[:-4]
        return _base.lower() + "usd-perp"

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
        saving_folder_name,
    ]
    argv = list(map(str, argv))
    print(f"[START] {asset_1}-{asset_2}")

    try:
        subprocess.run(argv, check=True)
        print(f"[DONE]  {asset_1}-{asset_2}")
        return pair_to_grid, None
    except subprocess.CalledProcessError as e:
        return pair_to_grid, f"Subprocess failed: {e}"
    except Exception as e:
        return pair_to_grid, str(e)