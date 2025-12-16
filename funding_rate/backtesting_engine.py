import numpy as np
import pandas as pd
import matplotlib.pyplot as plt
import matplotlib.dates as mdates


def backtest_spot_perp_basis(
    df,
    investment,
    leverage,
    annual_borrow_rate,
    fee_bps,
    earn_yield,
    asset_name="ASSET",
    plot=True,
    COMPOUNDING=False,
):
    """
    Additive monthly compounding:
    - At each month-end, if USD PnL since last rebalance > 0,
      increase notional by (PnL × leverage)
    - No deleveraging on losses
    """

    df = df.copy().sort_index()

    fee_rate = fee_bps / 10_000.0

    # ================================================================
    # INITIAL STATE
    # ================================================================
    equity = float(investment)

    current_notional = investment * leverage

    spot_prev = float(df["spot_close"].iloc[0])
    perp_prev = float(df["perp_close"].iloc[0])

    qty_spot = current_notional / spot_prev
    qty_perp = current_notional / perp_prev

    borrowed_usd = max(0.0, current_notional - equity)

    # Entry fees
    equity -= current_notional * fee_rate * 2.0

    # Track equity at last rebalance
    last_rebalance_equity = equity

    # Month-end detection
    month_ends = df.index.to_period("M").to_timestamp("M")
    is_month_end = df.index == month_ends

    # Storage
    equity_curve = []
    price_pnl_cum = 0.0
    funding_pnl_cum = 0.0
    interest_cum = 0.0
    earn_cum = 0.0

    # ================================================================
    # MAIN LOOP (INCREMENTAL ACCOUNTING)
    # ================================================================
    for i, (ts, row) in enumerate(df.iterrows()):
        spot = float(row["spot_close"])
        perp = float(row["perp_close"])

        # ------------------------------------------------------------
        # Price PnL (incremental)
        # ------------------------------------------------------------
        pnl_spot = qty_spot * (spot - spot_prev)
        pnl_perp = qty_perp * (perp_prev - perp)
        price_pnl = pnl_spot + pnl_perp
        price_pnl_cum += price_pnl

        # ------------------------------------------------------------
        # Funding (event-based)
        # ------------------------------------------------------------
        funding_pnl = qty_perp * perp * float(row["fundingEvent"])
        funding_pnl_cum += funding_pnl

        # ------------------------------------------------------------
        # Time delta
        # ------------------------------------------------------------
        if i == 0:
            dt_days = 0.0
        else:
            dt_days = (ts - df.index[i - 1]).total_seconds() / 86400.0

        # ------------------------------------------------------------
        # Borrow interest + earned yield
        # ------------------------------------------------------------
        interest = borrowed_usd * annual_borrow_rate * (dt_days / 365.0)
        earn = current_notional * earn_yield * (dt_days / 365.0)

        interest_cum += interest
        earn_cum += earn

        # ------------------------------------------------------------
        # Update equity
        # ------------------------------------------------------------
        equity += price_pnl + funding_pnl - interest + earn

        # ------------------------------------------------------------
        # MONTH-END ADDITIVE COMPOUNDING
        # ------------------------------------------------------------
        if COMPOUNDING and is_month_end[i]:
            delta_pnl = equity - last_rebalance_equity

            if delta_pnl > 0:
                delta_notional = delta_pnl * leverage

                # Fees on incremental resize
                equity -= delta_notional * fee_rate * 2.0

                # Increase position
                current_notional += delta_notional
                qty_spot += delta_notional / spot
                qty_perp += delta_notional / perp

                borrowed_usd = max(0.0, current_notional - equity)

            last_rebalance_equity = equity

        # ------------------------------------------------------------
        # Store
        # ------------------------------------------------------------
        equity_curve.append(equity)

        spot_prev = spot
        perp_prev = perp

    # ================================================================
    # OUTPUT COLUMNS
    # ================================================================
    df["equity"] = equity_curve
    df["price_pnl"] = price_pnl_cum
    df["funding_pnl_cum"] = funding_pnl_cum
    df["interest_cost_cum"] = interest_cum
    df["earn_income_cum"] = earn_cum
    df["spread_pct"] = (df["perp_close"] / df["spot_close"] - 1.0) * 100.0

    print("Last Equity: ", df["equity"][-1])


    # ================================================================
    # BTC PRICE + 50D MA (FOR REGIME SHADING)
    # ================================================================
    ma_window = 50 * 24
    df["btc_price"] = df["spot_close"]
    df["btc_ma_50d"] = df["btc_price"].rolling(ma_window).mean()
    df["btc_ma_slope"] = df["btc_ma_50d"].diff()
    bearish_mask = (df["btc_ma_slope"] < 0) & df["btc_ma_slope"].notna()

    def shade_bearish(ax):
        in_region = False
        start = None
        for t, m in zip(df.index, bearish_mask):
            if m and not in_region:
                start = t
                in_region = True
            elif not m and in_region:
                ax.axvspan(start, t, color="red", alpha=0.08, linewidth=0)
                in_region = False
        if in_region:
            ax.axvspan(start, df.index[-1], color="red", alpha=0.08, linewidth=0)

    # ================================================================
    # PLOTS
    # ================================================================
    if plot:
        fig, (ax1, ax2, ax3, ax4) = plt.subplots(
            4, 1, figsize=(16, 13), sharex=True,
            gridspec_kw={"height_ratios": [1, 1, 2, 2]}
        )

        ax1.plot(df.index, df["spread_pct"])
        ax1.set_title(f"{asset_name} Spot–Perp Spread (%)")
        ax1.grid(alpha=0.3)

        ax2.plot(df.index, df["fundingRate"])
        ax2.set_title("Funding Rate")
        ax2.grid(alpha=0.3)

        ax3.plot(df.index, df["equity"])
        ax3.set_title(
            f"Equity Curve ({'Additive Compounding' if COMPOUNDING else 'Static'})"
        )
        ax3.set_ylabel("USD")
        ax3.grid(alpha=0.3)

        ax4.plot(df.index, df["btc_price"], label="BTC Price")
        ax4.plot(df.index, df["btc_ma_50d"], label="BTC 50D MA")
        ax4.legend()
        ax4.grid(alpha=0.3)

        for ax in (ax1, ax2, ax3, ax4):
            shade_bearish(ax)

        ax4.xaxis.set_major_formatter(mdates.DateFormatter("%Y-%m-%d"))
        plt.xticks(rotation=45)
        plt.tight_layout()
        plt.show()

    return df
