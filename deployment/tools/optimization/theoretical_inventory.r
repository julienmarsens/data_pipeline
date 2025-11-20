#!/usr/bin/env Rscript
cat("[BOOT] --- entering inventory_from_equilibrium.R ---\n"); flush.console()

# -----------------------------------------------------------
# Imports
# -----------------------------------------------------------
suppressMessages({
  library(Rcpp)
  library(zoo)
  library(stats)
})

# -----------------------------------------------------------
# Args and paths
# -----------------------------------------------------------
args <- commandArgs(trailingOnly = TRUE)
ASSET_1 <- args[1]; ASSET_2 <- args[2]
IN_SAMPLE_DATE_START  <- args[3]; IN_SAMPLE_DATE_END  <- args[4]
OUT_SAMPLE_DATE_START <- args[5]; OUT_SAMPLE_DATE_END <- args[6]
OUT_SAMPLE_TIME_START <- args[7]
param_vals <- suppressWarnings(as.numeric(args[8:13]))
SIGNAL_ANGLE  <- param_vals[1]
MARGIN <- param_vals[2]
STEP_BACK <- param_vals[3]
TRADING_ANGLE <- param_vals[4]
ORDER_SIZE <- param_vals[5]
MAX_CROSSING <- as.integer(param_vals[6])
TRADER_ID <- args[14]
perso_local_path <- args[15]
perso_disk_path <- args[16]

repo_base_path  <- file.path(perso_local_path, "data_pipeline", "deployment", "tools", "optimization")
path.to.source  <- file.path(perso_disk_path, "market_data", "sync_market_data")

# -----------------------------------------------------------
# Load modules
# -----------------------------------------------------------
Rcpp::sourceCpp(file.path(repo_base_path, "quoter_algo.cpp"))
Rcpp::sourceCpp(file.path(repo_base_path, "quoter_rm.cpp"))
source(file.path(repo_base_path, "data_tools.r"))
source(file.path(repo_base_path, "product_specs.r"))

# -----------------------------------------------------------
# Load IN-SAMPLE data
# -----------------------------------------------------------
exchange.name <- "binance-coin-futures"
exchange.names <- c(exchange.name, exchange.name)
product.names  <- c(ASSET_1, ASSET_2)

dates.range.is <- c(gsub("_", "", IN_SAMPLE_DATE_START),
                    gsub("_", "", IN_SAMPLE_DATE_END))
files <- list.files(path = path.to.source, full.names = FALSE)
available <- grep(paste(exchange.name, exchange.name, ASSET_1, ASSET_2, sep="__"), files, value=TRUE)
dates <- gsub("\\..*$", "", sapply(strsplit(available, "__"), `[`, 5))
dates.is <- dates[dates >= dates.range.is[1] & dates <= dates.range.is[2]]
if (!length(dates.is)) stop("No dates available in IS range.")
prices.is <- read_r_bbo_cpp_repl(exchange.names, product.names, dates.is, path.to.source)
names(prices.is) <- c("time_seconds","bid.a","ask.a","bid.b","ask.b")
cat(sprintf("[DATA] Loaded %d rows of IS BBO data\n", nrow(prices.is)))

# -----------------------------------------------------------
# Fit regression on IN-SAMPLE
# -----------------------------------------------------------
lm_b_a <- lm(bid.b ~ bid.a, data = prices.is)
intercept <- coef(lm_b_a)[1]
slope <- coef(lm_b_a)[2]

# -----------------------------------------------------------
# Load OUT-OF-SAMPLE data
# -----------------------------------------------------------
dates.range.oos <- c(gsub("_", "", OUT_SAMPLE_DATE_START),
                     gsub("_", "", OUT_SAMPLE_DATE_END))
dates.oos <- dates[dates >= dates.range.oos[1] & dates <= dates.range.oos[2]]
if (!length(dates.oos)) stop("No dates available in OOS range.")
prices.oos <- read_r_bbo_cpp_repl(exchange.names, product.names, dates.oos, path.to.source)
names(prices.oos) <- c("time_seconds","bid.a","ask.a","bid.b","ask.b")
cat(sprintf("[DATA] Loaded %d rows of OOS BBO data\n", nrow(prices.oos)))

# -----------------------------------------------------------
# Find equilibrium point in OOS (closest to regression line, backwards)
# -----------------------------------------------------------
spread.oos <- prices.oos$bid.b - (intercept + slope * prices.oos$bid.a)
abs_spread <- abs(spread.oos)

idx_start <- which.min(rev(abs_spread))
idx_start <- length(abs_spread) - idx_start + 1

if (is.na(idx_start) || length(idx_start) == 0)
  stop("No near-equilibrium found in OOS segment.")

closest_spread <- spread.oos[idx_start]
closest_time <- as.POSIXct(prices.oos$time_seconds[idx_start]/1000, origin="1970-01-01", tz="UTC")
cat(sprintf("[INFO] Closest-to-equilibrium (OOS) at %s (spread = %.6f)\n",
            closest_time, closest_spread))

prices_segment <- prices.oos[idx_start:nrow(prices.oos), ]
boot_ts <- closest_time
cat(sprintf("[INFO] Starting backfill from equilibrium at %s (%d rows)\n",
            boot_ts, nrow(prices_segment)))

# -----------------------------------------------------------
# Load product specs
# -----------------------------------------------------------
prod.spec.a <- product.specs[[paste(toupper(gsub("-", "_", ASSET_1)), "_INVERSE_BINANCE-COIN-FUTURES", sep="")]]
prod.spec.b <- product.specs[[paste(toupper(gsub("-", "_", ASSET_2)), "_INVERSE_BINANCE-COIN-FUTURES", sep="")]]
tick.size.a <- as.numeric(prod.spec.a$tick.size)
tick.size.b <- as.numeric(prod.spec.b$tick.size)

# -----------------------------------------------------------
# Recompute signal, margin, stepback
# -----------------------------------------------------------
signal.vector <- c(cos(SIGNAL_ANGLE*pi/4 + pi/4),
                   - (1/slope) * sin(SIGNAL_ANGLE*pi/4 + pi/4))
normalized.signal.vector <- signal.vector / sqrt(sum(signal.vector^2))
margin <- MARGIN * min(diff(range(prices_segment$bid.a)), diff(range(prices_segment$bid.b)))
stepback <- STEP_BACK * margin
theo.price <- (prices_segment$bid.a[1] + prices_segment$bid.b[1]) / 2
margin.inv.vector <- c(-normalized.signal.vector[2], normalized.signal.vector[1])
margin.inv.slope <- margin.inv.vector[2] / margin.inv.vector[1]

# -----------------------------------------------------------
# Replay quoting & inventory accumulation
# -----------------------------------------------------------
dPrice <- generateCrossing(
  theOrderLevelIncrementA = tick.size.a,
  theOrderLevelIncrementB = tick.size.b,
  prices_segment$bid.a,
  prices_segment$ask.a,
  prices_segment$bid.b,
  prices_segment$ask.b,
  theo.price, margin, stepback,
  margin.inv.vector[1], margin.inv.vector[2],
  margin.inv.slope,
  normalized.signal.vector[1], normalized.signal.vector[2],
  tick.size.a, tick.size.b
)

quote.level <- data.frame(
  prices_segment,
  aSellLevelA = pmax(dPrice$aSellLevelA, 0),
  aBuyLevelA  = pmax(dPrice$aBuyLevelA, 0),
  aSellLevelB = pmax(dPrice$aSellLevelB, 0),
  aBuyLevelB  = pmax(dPrice$aBuyLevelB, 0)
)

safe.position <- nc2lInventoryControl(
  quote.level$bid.a, quote.level$ask.a,
  quote.level$bid.b, quote.level$ask.b,
  quote.level$aSellLevelA, quote.level$aBuyLevelA,
  quote.level$aSellLevelB, quote.level$aBuyLevelB,
  MAX_CROSSING, ORDER_SIZE, ORDER_SIZE
)

inventory_path <- data.frame(
  time = as.POSIXct(quote.level$time_seconds/1000, origin="1970-01-01", tz="UTC"),
  inv_A = cumsum(safe.position$theOrderVecA),
  inv_B = cumsum(safe.position$theOrderVecB)
)

# -----------------------------------------------------------
# Output
# -----------------------------------------------------------
current_inventory <- tail(inventory_path, 1)
cat(sprintf("\nIf you had started quoting at %s,\ncurrent inventory would be:\n", boot_ts))
print(current_inventory)

plot(inventory_path$time, inventory_path$inv_A, type='l', col='red',
     main=sprintf("Hypothetical Inventory since %s", boot_ts),
     ylab="Inventory", xlab="Time", lwd=1.2)
lines(inventory_path$time, inventory_path$inv_B, col='green', lwd=1.2)
legend("topleft", legend=c(ASSET_1, ASSET_2), col=c("red","green"), lty=1, bty="n")

# Rscript deployment/tools/optimization/theoretical_inventory.r adausd-perp trxusd-perp 2025_06_29 2025_08_17 2025_10_13 2025_11_04 09:00:00 -0.777568861860349 110 0.9999 0.5 600 6 trader_557 /Users/julienmarsens /Volumes/disk_ext