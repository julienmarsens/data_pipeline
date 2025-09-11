import subprocess

r_script = "./prod_report_module/tools/backtesting/backtesting_engine.R"

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

raw_param = "−0.25#30#0.9999#0.3#1000#7"

list_param = parse_param_string(raw_param)

subprocess.run(
    [
        "Rscript",
        r_script,
        "adausd-perp",
        "dotusd-perp",
        "2025_07_20",
        "2025_08_03",
        "2025_08_07",
        "2025_08_20",
        "17:00:00",

        *map(str, list_param),  # <-- use the parsed values
        "trader_620"
    ],
    check=True
)