from ruamel.yaml import YAML
from ruamel.yaml.comments import CommentedSeq

class TradeSizeAccount:

    def __init__(self):
        yaml = YAML(typ="rt")  # round-trip
        yaml.preserve_quotes = True
        yaml.width = 10 ** 6
        self.yaml = yaml

        with open('./deployment/config/config_to_deploy.yml', 'r') as f:
            self.config = yaml.load(f)

        with open('./deployment/config/mapping.yml', 'r') as f:
            self.mapping = yaml.load(f)

        self.base_size = self.config["base_size"]
        self.deployment_percentage = self.config["deployment_percentage"]
        self.pairs = self.config["pairs"]
        self.backtest_max_inventory = self.config["backtest_max_inventory"]
        self.backtest_stop_loss = self.config["backtest_stop_loss"]  # <-- NEW

        if len(self.backtest_stop_loss) != len(self.pairs):  # <-- NEW
            raise ValueError(
                f"backtest_stop_loss length ({len(self.backtest_stop_loss)}) "
                f"must match pairs length ({len(self.pairs)})."
            )

        gross_exposures = [sum(inv) for inv in self.backtest_max_inventory]
        self.denom = sum(
            w * gross_exposures[i] for i, w in enumerate(self.config["optimization_weights"])
        )

        self.optimization_weights = self.config["optimization_weights"]

    def split_pairs_per_account(self):
        accounts = [[], [], []]
        account_assets = [set(), set(), set()]

        for pair in self.pairs:
            asset1, asset2 = pair
            placed = False
            for i in range(3):
                if asset1 not in account_assets[i] and asset2 not in account_assets[i]:
                    accounts[i].append(pair)
                    account_assets[i].update(pair)
                    placed = True
                    break
            if not placed:
                raise ValueError(f"Cannot allocate pair {pair} across 3 accounts without asset conflicts.")
        return accounts

    def compute_trade_sizes(self, assets_usd_equivalent, max_leverage_x):
        max_usd_exposure = assets_usd_equivalent * max_leverage_x
        k = max_usd_exposure / self.denom

        # scale by deployment percentage
        deployment_factor = self.deployment_percentage / 100.0

        trade_sizes = [
            round(self.base_size * k * w * deployment_factor, -2)
            for w in self.optimization_weights
        ]
        return trade_sizes

    def compute_stop_losses(self, assets_usd_equivalent, max_leverage_x):  # <-- NEW
        """Scale stop losses per pair with the same multiplier used for trade sizes"""
        max_usd_exposure = assets_usd_equivalent * max_leverage_x
        k = max_usd_exposure / self.denom
        deployment_factor = self.deployment_percentage / 100.0

        stop_losses = [
            round(self.backtest_stop_loss[i] * k * w * deployment_factor, -1)
            for i, w in enumerate(self.optimization_weights)
        ]
        return stop_losses

    def _to_flow_seq(self, items):
        """Convert Python list (or nested list) to ruamel flow-style [a, b] or [[a, b], [c, d]]"""
        if not items:  # empty list
            seq = CommentedSeq([])
            seq.fa.set_flow_style()
            return seq

        if isinstance(items[0], list):  # nested list
            seq = CommentedSeq([CommentedSeq(sub) for sub in items])
            for sub in seq:
                sub.fa.set_flow_style()
        else:
            seq = CommentedSeq(items)
        seq.fa.set_flow_style()
        return seq

    def update_mapping(self):
        accounts_split = self.split_pairs_per_account()

        for kernel in self.config["target_kernel"]:
            kernel_data = self.mapping[kernel]
            assets_usd_equivalent = kernel_data["assets_usd_equivalent"]
            max_leverage_x = kernel_data["constrain"]["max_leverage_x"]

            trade_sizes = self.compute_trade_sizes(assets_usd_equivalent, max_leverage_x)
            stop_losses = self.compute_stop_losses(assets_usd_equivalent, max_leverage_x)  # <-- NEW

            pair_to_size = {tuple(self.pairs[i]): trade_sizes[i] for i in range(len(self.pairs))}
            pair_to_stop = {tuple(self.pairs[i]): stop_losses[i] for i in range(len(self.pairs))}  # <-- NEW

            for idx, account_pairs in enumerate(accounts_split, start=1):
                nested_pairs = []
                nested_sizes = []
                nested_stops = []  # <-- NEW
                for pair in account_pairs:
                    size = pair_to_size[tuple(pair)]
                    stop = pair_to_stop[tuple(pair)]  # <-- NEW
                    nested_pairs.append(pair)
                    nested_sizes.append([size] * len(pair))
                    nested_stops.append([stop] * len(pair))  # <-- NEW

                kernel_data[f"account_{idx}"]["pairs"] = self._to_flow_seq(nested_pairs)
                kernel_data[f"account_{idx}"]["trade_size"] = self._to_flow_seq(nested_sizes)
                kernel_data[f"account_{idx}"]["backtest_stop_loss"] = self._to_flow_seq(nested_stops)  # <-- NEW

        with open('./deployment/config/mapping.yml', 'w') as f:
            self.yaml.dump(self.mapping, f)

    def run(self):
        self.update_mapping()
        print("✅ mapping.yml updated with scaled trade_size & backtest_stop_loss (deployment % applied)")  # <-- NEW message


if __name__ == "__main__":
    TradeSizeAccount().run()
