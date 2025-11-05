###########################################################################################################
###########  Product specification file
###########################################################################################################

###########################################################################################################
###########  Define products specs (i.e. exchange information, e.g. deribit)
###########################################################################################################
# product specs container
product.specs <- list()

###########################################################################################################
# DERIBIT
# for deribit perpetual mkt: https://www.deribit.com/kb/deribit-inverse-perpetual
# inverse swap min order size in quote (i.e. usd) == 1 contract
###########################################################################################################
product.specs[["BTC_PERPETUAL_INVERSE_DERIBIT"]] <- list(
  fx.rate = 1,
  min.order.size = 10, # USD
  lot.size = 1,
  tick.size = 0.5,
  t.fees.maker = 0.0,
  t.fees.taker = 0.05/100,
  contract.den = FALSE
)

product.specs[["ETH_PERPETUAL_INVERSE_DERIBIT"]] <- list(
  fx.rate = 1,
  min.order.size = 1, # USD
  lot.size = 1,
  tick.size = 0.05,
  t.fees.maker = 0.0,
  t.fees.taker = 0.05/100,
  contract.den = FALSE
)

###########################################################################################################
# BINANCE
#
###########################################################################################################
# binance coin futures:

# binance-coin-futures__adausd-perp__2023-04-01__market-depth.csv	
product.specs[["ADAUSD_PERP_INVERSE_BINANCE-COIN-FUTURES"]] <- list(
  fx.rate = 1,
  min.order.size = 10, # 10 usd
  lot.size = 1,
  tick.size = 0.0001,
  t.fees.maker = 0.02/100,
  t.fees.taker = 0.05/100,
  contract.den = TRUE
)

# binance-coin-futures__bchusd-perp__2023-04-01__market-depth.csv	
product.specs[["BCHUSD_PERP_INVERSE_BINANCE-COIN-FUTURES"]] <- list(
  fx.rate = 1,
  min.order.size = 10, # 10 usd
  lot.size = 1,
  tick.size = 0.01,
  t.fees.maker = 0.02/100,
  t.fees.taker = 0.05/100,
  contract.den = TRUE
)

# binance-coin-futures__bnbusd-perp__2023-04-01__market-depth.csv
product.specs[["BNBUSD_PERP_INVERSE_BINANCE-COIN-FUTURES"]] <- list(
  fx.rate = 1,
  min.order.size = 10, # 10 usd
  lot.size = 1,
  tick.size = 0.01,
  t.fees.maker = 0.02/100,
  t.fees.taker = 0.05/100,
  contract.den = TRUE
)

# binance-coin-futures__btcusd-perp__2023-04-01__market-depth.csv	
product.specs[["BTCUSD_PERP_INVERSE_BINANCE-COIN-FUTURES"]] <- list(
  fx.rate = 1,
  min.order.size = 100, # 100 usd
  lot.size = 1,
  tick.size = 0.1,
  t.fees.maker = 0.02/100,
  t.fees.taker = 0.05/100,
  contract.den = TRUE
)

# binance-coin-futures__dogeusd-perp__2023-04-01__market-depth.csv	
product.specs[["DOGEUSD_PERP_INVERSE_BINANCE-COIN-FUTURES"]] <- list(
  fx.rate = 1,
  min.order.size = 10, # 10 usd
  lot.size = 1,
  tick.size = 0.00001,
  t.fees.maker = 0.02/100,
  t.fees.taker = 0.05/100,
  contract.den = TRUE
)

# binance-coin-futures__dotusd-perp__2023-04-01__market-depth.csv	
product.specs[["DOTUSD_PERP_INVERSE_BINANCE-COIN-FUTURES"]] <- list(
  fx.rate = 1,
  min.order.size = 10, # 10 usd
  lot.size = 1,
  tick.size = 0.001,
  t.fees.maker = 0.02/100,
  t.fees.taker = 0.05/100,
  contract.den = TRUE
)

# binance-coin-futures__eosusd-perp__2023-04-01__market-depth.csv	
product.specs[["EOSUSD_PERP_INVERSE_BINANCE-COIN-FUTURES"]] <- list(
  fx.rate = 1,
  min.order.size = 10, # 10 usd
  lot.size = 1,
  tick.size = 0.001,
  t.fees.maker = 0.02/100,
  t.fees.taker = 0.05/100,
  contract.den = TRUE
)

# binance-coin-futures__ethusd-perp__2023-04-01__market-depth.csv
product.specs[["ETHUSD_PERP_INVERSE_BINANCE-COIN-FUTURES"]] <- list(
  fx.rate = 1,
  min.order.size = 10, # 10 usd
  lot.size = 1,
  tick.size = 0.01,
  t.fees.maker = 0.02/100,
  t.fees.taker = 0.05/100,
  contract.den = TRUE
)

# binance-coin-futures__ltcusd-perp__2023-04-01__market-depth.csv
product.specs[["LTCUSD_PERP_INVERSE_BINANCE-COIN-FUTURES"]] <- list(
  fx.rate = 1,
  min.order.size = 10, # 10 usd
  lot.size = 1,
  tick.size = 0.01,
  t.fees.maker = 0.02/100,
  t.fees.taker = 0.05/100,
  contract.den = TRUE
)

# binance-coin-futures__solusd-perp__2023-04-01__market-depth.csv	
product.specs[["SOLUSD_PERP_INVERSE_BINANCE-COIN-FUTURES"]] <- list(
  fx.rate = 1,
  min.order.size = 10, # 10 usd
  lot.size = 1,
  tick.size = 0.01,
  t.fees.maker = 0.02/100,
  t.fees.taker = 0.05/100,
  contract.den = TRUE
)

# binance-coin-futures__suiusd-perp__2023-04-01__market-depth.csv
product.specs[["SUIUSD_PERP_INVERSE_BINANCE-COIN-FUTURES"]] <- list(
  fx.rate = 1,
  min.order.size = 10, # 10 usd
  lot.size = 1,
  tick.size = 0.0001,
  t.fees.maker = 0.02/100,
  t.fees.taker = 0.05/100,
  contract.den = TRUE
)

# binance-coin-futures__uniusd-perp__2023-04-01__market-depth.csv
product.specs[["UNIUSD_PERP_INVERSE_BINANCE-COIN-FUTURES"]] <- list(
  fx.rate = 1,
  min.order.size = 10, # 10 usd
  lot.size = 1,
  tick.size = 0.001,
  t.fees.maker = 0.02/100,
  t.fees.taker = 0.05/100,
  contract.den = TRUE
)

# binance-coin-futures__xrpusd-perp__2023-04-01__market-depth.csv
product.specs[["XRPUSD_PERP_INVERSE_BINANCE-COIN-FUTURES"]] <- list(
  fx.rate = 1,
  min.order.size = 10, # 10 usd
  lot.size = 1,
  tick.size = 0.0001,
  t.fees.maker = 0.02/100,
  t.fees.taker = 0.05/100,
  contract.den = TRUE
)

product.specs[["XRPUSD_PERP_INVERSE_BINANCE-COIN-FUTURES"]] <- list(
  fx.rate = 1,
  min.order.size = 10, # 10 usd
  lot.size = 1,
  tick.size = 0.0001,
  t.fees.maker = 0.02/100,
  t.fees.taker = 0.05/100,
  contract.den = TRUE
)

## 4 new pairs
product.specs[["TRXUSD_PERP_INVERSE_BINANCE-COIN-FUTURES"]] <- list(
  fx.rate = 1,
  min.order.size = 10, # 10 usd
  lot.size = 1,
  tick.size = 0.00001,
  t.fees.maker = 0.02/100,
  t.fees.taker = 0.05/100,
  contract.den = TRUE
)

product.specs[["LINKUSD_PERP_INVERSE_BINANCE-COIN-FUTURES"]] <- list(
  fx.rate = 1,
  min.order.size = 10, # 10 usd
  lot.size = 1,
  tick.size = 0.001,
  t.fees.maker = 0.02/100,
  t.fees.taker = 0.05/100,
  contract.den = TRUE
)

product.specs[["AVAXUSD_PERP_INVERSE_BINANCE-COIN-FUTURES"]] <- list(
  fx.rate = 1,
  min.order.size = 10, # 10 usd
  lot.size = 1,
  tick.size = 0.01,
  t.fees.maker = 0.02/100,
  t.fees.taker = 0.05/100,
  contract.den = TRUE
)

product.specs[["XLMUSD_PERP_INVERSE_BINANCE-COIN-FUTURES"]] <- list(
  fx.rate = 1,
  min.order.size = 10, # 10 usd
  lot.size = 1,
  tick.size = 0.00001,
  t.fees.maker = 0.02/100,
  t.fees.taker = 0.05/100,
  contract.den = TRUE
)

###########################################################################################################
###########################################################################################################
