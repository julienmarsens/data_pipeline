import json
from ruamel.yaml import YAML
from ruamel.yaml.comments import CommentedSeq

# -------- CONFIG --------
json_path = "./deployment/temporary/optimization_results.json"
yml_path = "./deployment/config/config_to_deploy.yml"
# ------------------------

def run():
    yaml = YAML(typ="rt")   # round-trip to preserve formatting and comments
    yaml.preserve_quotes = True
    yaml.width = 10**6

    # Load JSON results
    with open(json_path, "r") as f:
        data = json.load(f)

    optimization_weights = data["optimization_weights"]

    # Order absolute_parameters by pair_1, pair_2, ...
    abs_params = [v for k, v in sorted(
        data["absolute_parameters"].items(),
        key=lambda kv: int(kv[0].split("_")[1])
    )]

    # Extract max_inventory in the same order
    pair_keys = sorted(data["absolute_parameters"].keys(),
                       key=lambda x: int(x.split("_")[1]))
    max_stats = list(data["max_stats"].values())
    max_inventory = [max_stats[i]["max_inventory"] for i in range(len(pair_keys))]

    # Load YAML file
    with open(yml_path, "r") as f:
        yml_data = yaml.load(f)

    # Wrap lists in CommentedSeq to force flow style
    sig_seq = CommentedSeq(abs_params)
    sig_seq.fa.set_flow_style()  # <- keep [ ... ] format

    inv_seq = CommentedSeq(max_inventory)
    inv_seq.fa.set_flow_style()

    weights_seq = CommentedSeq(optimization_weights)
    weights_seq.fa.set_flow_style()

    # Update only autofill sections
    yml_data["signature"] = sig_seq
    yml_data["backtest_max_inventory"] = inv_seq
    yml_data["optimization_weights"] = weights_seq

    # Write YAML back (preserve format + comments)
    with open(yml_path, "w") as f:
        yaml.dump(yml_data, f)
