import pandas as pd
from sklearn.preprocessing import MinMaxScaler
import os
import os
import pandas as pd
import matplotlib.pyplot as plt
from matplotlib.backends.backend_pdf import PdfPages
from ruamel.yaml import YAML

def select_best_params(config_version, csv_path: str, min_r2: float = 0.9, min_num_trades: int = 25, min_sharpe: float = 0.2):
    # Check if file exists and not empty
    if not os.path.exists(csv_path) or os.stat(csv_path).st_size == 0:
        print("ERROR: Path does not exist or csv is empty")
        return None, False

    df = pd.read_csv(csv_path)

    if df.empty:
        print("ERROR: DF empty")
        return None, False

    # Apply filters
    df = df[
        (df["r2"] >= min_r2) &
        (df["num.crossing.oos"] >= min_num_trades) &
        (df["sharpe.ratio.oos"] >= min_sharpe)
    ]

    if df.empty:
        print("ERROR: DF empty")
        return None, False

    # Normalize metrics
    scaler = MinMaxScaler()
    df_norm = df.copy()
    df_norm[["sharpe.ratio", "pnl", "sharpe.ratio.oos", "pnl.oos", "r2"]] = scaler.fit_transform(
        df[["sharpe.ratio", "pnl", "sharpe.ratio.oos", "pnl.oos", "r2"]]
    )
    df_norm["num.crossing.oos"] = df["num.crossing.oos"].apply(lambda x: min(x, 1000))  # cap extreme values
    df_norm["num.crossing.oos"] = scaler.fit_transform(df_norm[["num.crossing.oos"]])

    # Stability penalty: Sharpe IS → OOS difference
    df_norm["sharpe_drop"] = (df["sharpe.ratio"] - df["sharpe.ratio.oos"]).abs()
    df_norm["sharpe_drop"] = scaler.fit_transform(df_norm[["sharpe_drop"]])

    yaml_rt = YAML()
    with open(f'./optima_finder/config/{config_version}.yml', 'r') as f:
        optima_finder_config = yaml_rt.load(f)

    # Composite score (adjust weights as needed)

    df_norm["score"] = (
            optima_finder_config["selection_utility_function"]["sharpe_ratio_out_of_sample"] * df_norm["sharpe.ratio.oos"] +
            optima_finder_config["selection_utility_function"]["sharpe_ratio_in_sample"] * df_norm["sharpe.ratio"] +
            optima_finder_config["selection_utility_function"]["num_crossing_out_of_sample"] * df_norm["num.crossing.oos"] +
            optima_finder_config["selection_utility_function"]["r2_out_of_sample"] * df_norm["r2"] +
            optima_finder_config["selection_utility_function"]["pnl_out_of_sample"] * df_norm["pnl.oos"]  -
            optima_finder_config["selection_utility_function"]["sharpe_drop_out_sample_to_in_sample"] * df_norm["sharpe_drop"]
    )

    # Pick best row
    best_row = df.loc[df_norm["score"].idxmax()]

    return best_row.to_dict(), True

# Example usage:
"""
best, flag = select_best_params(
    "/Volumes/disk_ext/results/2025_09_06__23_34/gs_adausd-perp_suiusd-perp_grid.csv",
    min_r2=0.2,
    min_num_trades=50,
    min_sharpe=0.3
)

if flag:
    print("Best parameters found:", best)
else:
    print("CSV is empty, invalid, or no rows passed the filters")
"""


import os
from pathlib import Path
import pandas as pd
import matplotlib.pyplot as plt
from matplotlib.backends.backend_pdf import PdfPages
from ruamel.yaml import YAML


def plot_and_save_pnls(yaml_path, folder, output_pdf="pnl_plot_results.pdf"):
    """
     yaml_path: path to optima_finder_pairs.yml containing 'signature' and 'parameters'
     folder: folder where pnl_*.csv files are stored
     output_pdf: name of output pdf
     """

    # --- load YAML ---
    yaml_rt = YAML()
    with open(yaml_path, "r") as f:
        yml = yaml_rt.load(f)

    signatures = yml["signature"]
    parameters = yml["parameters"]

    # --- find all gs_* files in folder ---
    folder_path = Path(folder)
    gs_files = sorted([f.name for f in folder_path.iterdir() if f.is_file() and f.name.startswith("gs_")])

    if len(signatures) != len(gs_files):
        raise ValueError(f"Length mismatch: {len(signatures)} signatures vs {len(gs_files)} gs_files")

    with PdfPages(output_pdf) as pdf:
        for gs_file, sig, rel_param in zip(gs_files, signatures, parameters):
            pair_name = gs_file.replace("gs_", "pnl_")
            pnl_path = os.path.join(folder, pair_name)

            if not os.path.exists(pnl_path):
                print(f"⚠️ Missing pnl file for {gs_file}")
                continue

            df = pd.read_csv(pnl_path)

            if sig not in df.columns:
                print(f"⚠️ Signature {sig} not found in {pnl_path}")
                continue

            # Raw data from CSV
            raw_series = df[sig]

            # Plot raw pnl series
            plt.figure(figsize=(10, 6))
            plt.plot(df.index, raw_series, label=f"{gs_file}\nparam={rel_param}")
            plt.title(f"Raw PnL series for {gs_file}")
            plt.xlabel("Line number")
            plt.ylabel("PnL (raw from CSV)")
            plt.legend()
            plt.grid(True)
            pdf.savefig()
            plt.close()

    print(f"✅ Saved all plots into {output_pdf}")


# Example usage
"""best_results = {
    "gs_adausd-perp_suiusd-perp_grid.csv": {
        "absolute.parameters": "0.98322569730394#-0.182393059520315#0.00393290278921533#0.0039325094989364#0.923879532511287#-0.38268343236509#1000#8"
    },
    "gs_suiusd-perp_ltcusd-perp_grid.csv": {
        "absolute.parameters": "0.999592454435215#-0.0285468918830607#0.0228375135064278#0.0228352297550771#0.923879532511287#-0.38268343236509#1000#6"
    },
}

plot_pnls(best_results, "/Volumes/disk_ext/results/2025_09_07__00_13", output_pdf="pnl_results.pdf")"""

# pyhton3 optima_finder/tools/results_analysis.py

