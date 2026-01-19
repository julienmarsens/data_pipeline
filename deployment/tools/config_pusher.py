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

    # Sort absolute_parameters by pair_1, pair_2, ...
    pair_keys = sorted(
        data["absolute_parameters"].keys(),
        key=lambda x: int(x.split("_")[1])
    )

    abs_params = [data["absolute_parameters"][k] for k in pair_keys]

    # -----------------------------
    # NEW: max_stats is now {pair_key: [ {pair_instance: "pair_1", ...}, ... ], ...}
    # Build a lookup by pair_instance so we can align with pair_keys
    # -----------------------------
    stats_by_pair_instance = {}

    max_stats = data.get("max_stats", {})
    for pair_key, entries in max_stats.items():
        if not isinstance(entries, list):
            raise ValueError(
                f"Expected max_stats['{pair_key}'] to be a list, got {type(entries)}"
            )

        for entry in entries:
            if not isinstance(entry, dict):
                raise ValueError(
                    f"Expected an entry dict under max_stats['{pair_key}'], got {type(entry)}"
                )

            pi = entry.get("pair_instance")
            if not pi:
                raise ValueError(f"Missing 'pair_instance' in max_stats['{pair_key}'] entry: {entry}")

            if pi in stats_by_pair_instance:
                raise ValueError(
                    f"Duplicate stats for '{pi}' found in max_stats. "
                    f"Already had one, found another under '{pair_key}'."
                )

            stats_by_pair_instance[pi] = entry

    # Extract max_inventory and stop_loss in the same order as absolute_parameters (pair_1..pair_n)
    max_inventory = []
    stop_losses = []

    for pk in pair_keys:
        entry = stats_by_pair_instance.get(pk)
        if entry is None:
            raise ValueError(
                f"Missing max_stats entry for '{pk}'. "
                f"Have: {sorted(stats_by_pair_instance.keys())}"
            )

        max_inventory.append(entry["max_inventory"])
        stop_losses.append(entry["stop_loss"])

    # Load YAML file
    with open(yml_path, "r") as f:
        yml_data = yaml.load(f)

    # Wrap lists in CommentedSeq to force flow style
    sig_seq = CommentedSeq(abs_params)
    sig_seq.fa.set_flow_style()

    inv_seq = CommentedSeq(max_inventory)
    inv_seq.fa.set_flow_style()

    stop_seq = CommentedSeq(stop_losses)
    stop_seq.fa.set_flow_style()

    weights_seq = CommentedSeq(optimization_weights)
    weights_seq.fa.set_flow_style()

    # Update only autofill sections
    yml_data["signature"] = sig_seq
    yml_data["backtest_max_inventory"] = inv_seq
    yml_data["backtest_stop_loss"] = stop_seq
    yml_data["optimization_weights"] = weights_seq

    # Write YAML back (preserve format + comments)
    with open(yml_path, "w") as f:
        yaml.dump(yml_data, f)


if __name__ == "__main__":
    run()
