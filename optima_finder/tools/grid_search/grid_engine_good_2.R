###########  Cleanup working environment
Sys.setenv (LANGUAGE="en")
remove(list=ls(all=T))
graphics.off()

options(digits=10)
Sys.setenv(TZ='UTC')

###########  load libs

library(zoo)
library(yaml)

###########  constant configuration

# Get arguments
args <- commandArgs(trailingOnly = TRUE)

perso_local_path <- args[7]
perso_disk_path <- args[8]

config_version <- args[9]

repo_base_path  <- file.path(perso_local_path, "data_pipeline", "optima_finder", "tools", "grid_search")
grid_config_path <- file.path(perso_local_path, "data_pipeline", "optima_finder", "config")

config_path <- paste0(grid_config_path, "/", config_version, ".yaml")
config_file <- yaml::read_yaml(config_path)

MIN_SHARPE <- config_file$filtering$min_sharpe
MIN_CROSSING <- config_file$filtering$min_crossing_per_day * as.integer(args[10])

#  Choose from pairs list & export statement
# plot
export2pdf <- config_file$output$export_to_pdf

is.daily.hedged       <- TRUE

if (export2pdf) {
  is.plot.results       <- TRUE
  is.export.pnl.surface <- TRUE
} else {
  is.plot.results       <- FALSE
  is.export.pnl.surface <- TRUE
}

export.name.flag   <- "grid" # ref. c(50, seq(from=100, to=500, by=100))

# if false don't reload data at each grid search (not each loop within a grid)
RELOAD             <- TRUE

# over one day
is_start_num <- as.integer(gsub("_", "", args[3]))
is_end_num <- as.integer(gsub("_", "", args[4]))
oos_start_num <- as.integer(gsub("_", "", args[5]))
oos_end_num <- as.integer(gsub("_", "", args[6]))

oos.dates      <- c(oos_start_num, oos_end_num) #
is.dates       <- c(is_start_num, is_end_num) #

# update conf parameters at the end of the file accordingly
DISPLAY_CONFIGURATION_CPP <- FALSE
# legacy filtering test
max.bid.ask.filter        <- FALSE

###########  Core HFT statarb quoter strategy - Init
library(rstudioapi)
library(zoo)

# home build libs
if (rstudioapi::isAvailable()) {
  PATH_2_ROOT <- dirname(dirname(dirname(rstudioapi::getSourceEditorContext()$path)))
} else {
  PATH_2_ROOT <- dirname(dirname(dirname(getwd())))
}

setwd(PATH_2_ROOT)
# data source
path.to.source  <- file.path(perso_disk_path, "market_data", "sync_market_data")
path.to.results  <- file.path(perso_disk_path, "results", args[11])

###########  Load and/or install R packages and libs

# C++
Rcpp::sourceCpp(paste(repo_base_path, "/", "quoter_algo.cpp", sep=""))
Rcpp::sourceCpp(paste(repo_base_path, "/", "quoter_rm_increasing_size.cpp", sep=""))
# R utils
source(paste(repo_base_path, "/", "data_tools.r", sep=""))
source(paste(repo_base_path, "/", "product_specs.r", sep=""))
source(paste(repo_base_path, "/", "stats.r", sep=""))
# oos slave script
source(paste(repo_base_path, "/", "core_quoter_slave_release.r", sep=""))

# -------------------------------------------------
# Auto margin calibration
# -------------------------------------------------
calibrate_margin_range <- function(prices,
                                   normalized.signal.vector,
                                   margin.inv.vector,
                                   margin.inv.slope,
                                   tick.size.a,
                                   tick.size.b,
                                   fx.a,
                                   fx.b,
                                   min_crossings,
                                   max_crossings,
                                   max_range = 5000,   # keep default
                                   stepback.factors = c(0.5, 0.75, 0.9999)) {

  cat("\n[Calibration] --- Starting margin calibration ---\n")

  # --- construct signal prices ---
  signal.prices <- cbind(
    prices[,"bid.a"] * normalized.signal.vector[1] +
      prices[,"ask.b"] * normalized.signal.vector[2],
    prices[,"ask.a"] * normalized.signal.vector[1] +
      prices[,"bid.b"] * normalized.signal.vector[2]
  )

  theo.price <- mean(c(signal.prices[,1], signal.prices[,2]), na.rm=TRUE)
  cat("[Calibration] theo.price =", theo.price, "\n")
  cat("[Calibration] min_crossings =", min_crossings,
      "| max_crossings =", max_crossings, "\n")

  # --- build exploration sets ---
  finer_region    <- seq(4, 20, by = 4)
  fine_region     <- seq(20, 100, by = 10)
  coarse_region   <- seq(100, 1000, by = 50)
  coarser_region  <- seq(1000, max_range, by = 200)
  coarse_levels   <- unique(c(finer_region, fine_region, coarse_region, coarser_region))

  cat("[Calibration] candidate relative margins (multipliers) =",
      paste(coarse_levels, collapse = ", "), "\n")

  # --- compute minimum spreads (same as in loop) ---
  idx.bid.ask.not.null.a <- which(prices[,"bid.a"] < prices[,"ask.a"])
  idx.bid.ask.not.null.b <- which(prices[,"bid.b"] < prices[,"ask.b"])

  minimum.spreads <- c(
    min(prices[idx.bid.ask.not.null.a,"ask.a"] - prices[idx.bid.ask.not.null.a,"bid.a"], na.rm=TRUE),
    min(prices[idx.bid.ask.not.null.b,"ask.b"] - prices[idx.bid.ask.not.null.b,"bid.b"], na.rm=TRUE)
  )

  # --- base margin (identical to loop logic) ---
  base.margin <- max(
    abs(normalized.signal.vector[1] * minimum.spreads[1]/fx.a),
    abs(normalized.signal.vector[2] * minimum.spreads[2]/fx.b)
  )
  cat("[Calibration] base.margin =", base.margin, "\n")

  # --- evaluate all candidates across stepback factors ---
  test_scores <- sapply(coarse_levels, function(margin_candidate) {
    margin <- base.margin * margin_candidate

    scores_per_stepback <- sapply(stepback.factors, function(sb) {
      stepback <- sb * margin
      dPrice <- generateCrossing(signal.prices[,1], signal.prices[,2],
                                 prices[,"bid.a"], prices[,"ask.a"],
                                 prices[,"bid.b"], prices[,"ask.b"],
                                 theo.price,
                                 theMargin   = margin,
                                 theStepback = stepback,
                                 margin.inv.vector[1], margin.inv.vector[2],
                                 margin.inv.slope,
                                 normalized.signal.vector[1], normalized.signal.vector[2],
                                 tick.size.a, tick.size.b)

      score <- sum(dPrice$moveTheoPriceVec != 0)

      if (score < min_crossings || score > max_crossings) {
        return(-Inf)
      } else {
        return(score)
      }
    })

    best_score <- max(scores_per_stepback)
    return(best_score)
  })

  cat("[Calibration] test_scores by margin:\n")
  print(data.frame(
    margin_rel = coarse_levels,
    margin_abs = coarse_levels * base.margin,
    score = test_scores
  ))

  # --- choose best ---
  if (all(is.infinite(test_scores))) {
    # fallback: select margin whose raw crossings are closest to the band
    raw_scores <- sapply(coarse_levels, function(margin_candidate) {
      margin <- base.margin * margin_candidate
      stepback <- max(stepback.factors) * margin
      dPrice <- generateCrossing(signal.prices[,1], signal.prices[,2],
                                 prices[,"bid.a"], prices[,"ask.a"],
                                 prices[,"bid.b"], prices[,"ask.b"],
                                 theo.price,
                                 theMargin   = margin,
                                 theStepback = stepback,
                                 margin.inv.vector[1], margin.inv.vector[2],
                                 margin.inv.slope,
                                 normalized.signal.vector[1], normalized.signal.vector[2],
                                 tick.size.a, tick.size.b)
      return(sum(dPrice$moveTheoPriceVec != 0))
    })

    dist_to_band <- sapply(raw_scores, function(x) {
      if (x < min_crossings) return(min_crossings - x)  # below band
      if (x > max_crossings) return(x - max_crossings)  # above band
      return(0)
    })

    best_idx   <- which.min(dist_to_band)
    best_margin <- coarse_levels[best_idx]
    best_abs    <- best_margin * base.margin
    cat("[Calibration] ⚠️ WARNING: no valid margins in criteria, fallback to closest-to-band crossings =",
        best_margin, "| absolute =", best_abs, "| raw crossings =", raw_scores[best_idx], "\n")

    # refine around fallback best_margin
    if (best_margin %in% finer_region) {
      step_size <- 2
    } else if (best_margin %in% fine_region) {
      step_size <- 10
    } else if (best_margin %in% coarse_region) {
      step_size <- 30
    } else if (best_margin %in% coarser_region) {
      step_size <- 200
    } else {
      step_size <- 200
    }
    candidates <- seq(best_margin - 2*step_size,
                      best_margin + 2*step_size,
                      by = step_size)
    fine_range <- candidates[candidates > 0 & candidates <= max_range]

  } else {
    best_margin <- coarse_levels[which.max(test_scores)]
    best_abs    <- best_margin * base.margin
    cat("[Calibration] ✅ Best relative margin found =", best_margin,
        "| absolute =", best_abs,
        "| score =", max(test_scores, na.rm=TRUE), "\n")

    # Decide step size based on region
    if (best_margin %in% finer_region) {
      step_size <- 2
    } else if (best_margin %in% fine_region) {
      step_size <- 10
    } else if (best_margin %in% coarse_region) {
      step_size <- 30
    } else if (best_margin %in% coarser_region) {
      step_size <- 200
    } else {
      step_size <- 200
    }

    candidates <- seq(best_margin - 2*step_size,
                      best_margin + 2*step_size,
                      by = step_size)
    fine_range <- candidates[candidates > 0 & candidates <= max_range]
  }

  cat("[Calibration] Refined search grid (relative) =",
      paste(fine_range, collapse = ", "), "\n")
  cat("[Calibration] Refined search grid (absolute) =",
      paste(round(fine_range * base.margin, 4), collapse = ", "), "\n")
  cat("[Calibration] --- Calibration complete ---\n\n")

  return(fine_range)
}



###########  Setup configuration parameters
###########  Grid parameters

# relative.margin.range    <- seq(from=config_file$grid$margin_range[1], to=config_file$grid$margin_range[2], by=config_file$grid$margin_range_step)
# relative.margin.range <- fine_range
relative.step.back.range      <- c(config_file$grid$step_back_range[1], config_file$grid$step_back_range[2], config_file$grid$step_back_range[3])#c(0.5, 0.75, .9999)
relative.trading.angle.range  <- c(0, seq(from=config_file$grid$trading_angle_range[1], to=config_file$grid$trading_angle_range[2], by=config_file$grid$trading_angle_range_step))
relative.order.size.range     <- config_file$grid$order_size_range
num.crossing.2.limit.range    <- seq(from=config_file$grid$crossing_to_limit_range[1], to=config_file$grid$crossing_to_limit_range[2], by=config_file$grid$crossing_to_limit_range_step)

# single/production run
if(config_file$single_run_param$run) {

  relative.margin.range         <- config_file$single_run_param$margin
  relative.step.back.range      <- config_file$single_run_param$step_back
  relative.trading.angle.range  <- config_file$single_run_param$trading_angle
  relative.order.size.range     <- config_file$single_run_param$order_size
  num.crossing.2.limit.range    <- config_file$single_run_param$crossing_to_limit
}

# Products grid implementation

# build combination of product
build.comb <- function(products) {
  grid.prod <- expand.grid(products,products) # All permutations

  # remove pairs with the same product
  idx.same.prod  <- grid.prod[,1]==grid.prod[,2]
  grid.prod.dupl <- grid.prod[!idx.same.prod,]

  # remove same pairs but inverse prod
  grid.prod.unq <- grid.prod.dupl[1,]
  for(i in 2:nrow(grid.prod.dupl)) {
    condition <- is.na(match(grid.prod.dupl[i,1], grid.prod.unq[,2]) == match(grid.prod.dupl[i,2], grid.prod.unq[,1]))

    if(!condition) next

    grid.prod.unq <- rbind(grid.prod.unq, grid.prod.dupl[i,])
  }
  names(grid.prod.unq) <- c("product.a", "product.b")

  return(grid.prod.unq)
}

###########  Read product specs and build combination
###########  REM: at this stage, don't consider deribit
if(FALSE) {
  product.names    <- names(product.specs)[-c(1,2)]
  prod.combination <- build.comb(product.names)

  # create object list for input to function
  product.exchange.names.lst <- list()
  exchange.names.lst         <- list()
  product.names.lst          <- list()
  instrument.type.lst        <- list()

  prod.combination <- prod.combination[31:35,]

  for(i in 1:nrow(prod.combination)) {
    product.exchange.names.lst[[i]]        <- c(as.character(prod.combination[i,1]), as.character(prod.combination[i,2]))

    # format names
    prod.splt.a                            <- tolower(strsplit(product.exchange.names.lst[[i]][1], "_")[[1]][1:2])
    prod.splt.b                            <- tolower(strsplit(product.exchange.names.lst[[i]][2], "_")[[1]][1:2])

    exchange.names.lst[[i]]                <- c("binance-coin-futures", "binance-coin-futures")
    product.names.lst[[i]]                 <- c(paste(prod.splt.a[1], prod.splt.a[2], sep="-"), paste(prod.splt.b[1], prod.splt.b[2], sep="-"))
    instrument.type.lst[[i]]               <- c("INVERSE", "INVERSE") # i.e. SPOT, LINEAR, INVERSE
  }
}

###########################################################################################################
exchange.names.lst    <- list()
instrument.type.lst   <- list()
product.names.lst     <- list()

###########  New combination 2025.04.13
instrument.type.lst[[1]] <- c("INVERSE", "INVERSE")
exchange.names.lst[[1]]  <- c(config_file$assets$exchange_name, config_file$assets$exchange_name)
product.names.lst[[1]]   <- c(args[1], args[2])

######### HELPER FUNCTIONS
find.available.dates <- function(dates.range, path.to.source, product.name, exchange.name) {
  file_names           <- list.files(path = path.to.source)
  file_names_raw       <- strsplit(file_names, "__")
  product.exchange     <- paste(exchange.name[1], "__", exchange.name[2], "__", product.name[1], "__", product.name[2], sep="")
  file.name.prod.ex    <- apply(do.call(rbind, file_names_raw)[,1:4], 1, paste, collapse="__")
  idx.prod.ex          <- product.exchange == file.name.prod.ex

  raw_dates <- do.call(rbind, file_names_raw)[,5]
  raw_dates <- sub("\\..*$", "", raw_dates)      # drop extension
  raw_dates <- gsub("_", "", raw_dates)          # remove underscores
  available.date <- raw_dates[idx.prod.ex]

  idx.date <- available.date >= dates.range[1] & available.date <= dates.range[2]
  return(available.date[idx.date])
}

###########  Main cross exchange market making algorithm
###########  Main loop through each pair

for(z in 1:length(product.names.lst)) {

    exchange.names       <- exchange.names.lst[[z]]
    instrument.type      <- instrument.type.lst[[z]]
    product.names        <- product.names.lst[[z]]

    # utility surface to be exported
    grid.util.surface    <- NULL
    pnl.util.surface     <- list()

    ######### FILTER IS AND OOS DATES

    is.dates.vect         <- find.available.dates(is.dates, path.to.source, product.names, exchange.names)
    oos.dates.vect        <- find.available.dates(oos.dates, path.to.source, product.names, exchange.names)
    # verbose run pair names and is-oos dates
    cat(file=stderr(), "\n###########################################################################################################\n")
    cat(file=stderr(), paste("[",Sys.time(),"] Starting new grid search for pair: ", args[1], "-", args[2], "\n", sep=""))
    cat(file=stderr(), paste("[",Sys.time(),"] IS start: ", head(is.dates.vect,1), " end: ", tail(is.dates.vect,1),"\n", sep=""))
    cat(file=stderr(), paste("[",Sys.time(),"] OOS start: ", head(oos.dates.vect,1), " end: ", tail(oos.dates.vect,1),"\n", sep=""))
    cat(file=stderr(), "###########################################################################################################\n")

    # check that data source is available, skip otherwise
    if(length(is.dates.vect)==0 || length(oos.dates.vect)==0) next

    ###########  naming convention
    product.name.std.a  <- paste(toupper(gsub("-", "_", product.names[1])), "_", instrument.type[1], "_", toupper(exchange.names[1]), sep="")
    product.name.std.b  <- paste(toupper(gsub("-", "_", product.names[2])), "_", instrument.type[2], "_", toupper(exchange.names[2]), sep="")

    ###########  Export to pdf file
    if(export2pdf) pdf(file = paste(path.to.results, "/chart_", args[1], "_", args[2], "_", export.name.flag, ".pdf", sep=""))

    ###########  lookup product specs
    prod.spec.a         <- product.specs[[product.name.std.a]]
    prod.spec.b         <- product.specs[[product.name.std.b]]

    type.a              <- instrument.type[1]
    type.b              <- instrument.type[2]

    fx.a                <- as.numeric(prod.spec.a$fx.rate)
    fx.b                <- as.numeric(prod.spec.b$fx.rate)

    min.order.size.a    <- as.numeric(prod.spec.a$min.order.size)
    min.order.size.b    <- as.numeric(prod.spec.b$min.order.size)

    lot.size.a          <-  as.numeric(prod.spec.a$lot.size)
    lot.size.b          <-  as.numeric(prod.spec.b$lot.size)

    tick.size.a         <-  as.numeric(prod.spec.a$tick.size) # to be considered as minimum price increment...in this implementation
    tick.size.b         <-  as.numeric(prod.spec.b$tick.size)

    t.fees.maker.a      <- as.numeric(prod.spec.a$t.fees.maker)
    t.fees.maker.b      <- as.numeric(prod.spec.b$t.fees.maker)

    t.fees.taker.a      <- as.numeric(prod.spec.a$t.fees.taker)
    t.fees.taker.b      <- as.numeric(prod.spec.b$t.fees.taker)

    ###########  Load data source
    if(RELOAD) {
      prices.bbo.a.b           <- read_r_bbo_cpp_repl(exchange.names, product.names, is.dates.vect, path.to.source)
      prices.bbo.a.b.oos       <- read_r_bbo_cpp_repl(exchange.names, product.names, oos.dates.vect, path.to.source)

      # comply with legacy naming convention
      names(prices.bbo.a.b)      <- c("time_seconds", "bid.a", "ask.a", "bid.b", "ask.b")
      names(prices.bbo.a.b.oos)  <- c("time_seconds", "bid.a", "ask.a", "bid.b", "ask.b")

      prices.bbo.a.b.org       <- prices.bbo.a.b
      prices.bbo.a.b.org.oos   <- prices.bbo.a.b.oos
    } else {
      prices.bbo.a.b           <- prices.bbo.a.b.org
      prices.bbo.a.b.oos       <- prices.bbo.a.b.org.oos
    }

    if(is.daily.hedged) {

      idx.is.eod   <- c(which(!duplicated(substr(as.POSIXct(prices.bbo.a.b[,"time_seconds"]/1000, origin="1970-01-01"),1,10)))[-1]-1,
                        length(prices.bbo.a.b[,"time_seconds"]))
      idx.oos.eod  <- c(which(!duplicated(substr(as.POSIXct(prices.bbo.a.b.oos[,"time_seconds"]/1000, origin="1970-01-01"),1,10)))[-1]-1,
                        length(prices.bbo.a.b.oos[,"time_seconds"]))

      # for daily sharpe calculation
      daily.date.is     <- unique(substr(as.POSIXct(prices.bbo.a.b[,"time_seconds"]/1000, origin="1970-01-01"),1,10))
      daily.date        <- unique(substr(as.POSIXct(prices.bbo.a.b.oos[,"time_seconds"]/1000, origin="1970-01-01"),1,10))
    } else {
      idx.is.eod   <- NULL
      idx.oos.eod  <- NULL
    }

    ###########  Compute best cointegration direction based on linear regression

    # use full sample for regression
    regression.length <- nrow(prices.bbo.a.b)

    # === compute regression slope ===
    lm_b_a <- lm(bid.b ~ bid.a, data = head(prices.bbo.a.b, regression.length))
    intercept.regression <- lm_b_a$coefficients[1]
    slope.regression     <- lm_b_a$coefficients[2]
    base.direction <- 1/slope.regression

    # # Convert regression slope into absolute angle (deg and rad)
    # slope_angle_rad <- atan(slope.regression)
    # slope_angle_deg <- slope_angle_rad * 180/pi
    #
    # # Distance to nearest boundary (0 or 90 deg)
    # dist_to_edge <- min(slope_angle_deg, 90 - slope_angle_deg)
    #
    # # Band = 100% of distance to edge, capped at 5°
    # band_deg <- min(dist_to_edge, 5)
    #
    # # Step = band/2 so that 5 values cover ±band
    # angles_deg <- slope_angle_deg + seq(-2, 2) * (band_deg/2)
    #
    # # Keep only angles strictly inside (0, 90)
    # angles_deg <- angles_deg[angles_deg > 0 & angles_deg < 90]
    #
    # # Convert back to relative units for loop
    # relative.signal.angle.range <- (angles_deg*pi/180 - pi/4) / (pi/4)
    #
    # # --- Verbose logging ---
    # cat(file=stderr(), "\n[Calibration] --- Signal angle setup ---\n")
    # cat(file=stderr(), "[Calibration] regression slope =", slope.regression, "\n")
    # cat(file=stderr(), "[Calibration] slope_angle (deg) =", round(slope_angle_deg, 6), "\n")
    # cat(file=stderr(), "[Calibration] adaptive band (deg) = ±", round(band_deg, 6), "\n")
    # cat(file=stderr(), "[Calibration] testing absolute signal angles (deg) =",
    #     paste(round(angles_deg, 6), collapse = ", "), "\n")
    # cat(file=stderr(), "[Calibration] testing relative.signal.angle.range (raw) =",
    #     paste(round(relative.signal.angle.range, 6), collapse = ", "), "\n")
    # cat(file=stderr(), "[Calibration] ------------------------------\n\n")

      # # Convert regression slope into absolute angle (deg and rad)
    slope_angle_rad <- atan(slope.regression)
    slope_angle_deg <- slope_angle_rad * 180/pi

    # --- Build a stable angle grid via multiplicative β perturbations ---
    # Baseline angle (deg) and slope β
    theta0_deg <- slope_angle_deg
    beta0 <- tan(theta0_deg * pi/180)

    # User knobs (unchanged elsewhere):
    r_max <- 0.60      # max relative change in β for the outer points (e.g., ±20%)
    n_vals <- 5        # number of angles to test (keep 5 to match your ±2 setup)

    # Geometric multipliers around 1, symmetric in log-space:
    delta <- log(1 + r_max)
    mult  <- exp(seq(-delta, delta, length.out = n_vals))

    # Apply to β, map back to angles; preserve β sign automatically
    beta_grid   <- beta0 * mult
    angles_deg  <- atan(beta_grid) * 180/pi

    # Keep strictly inside (0, 90) and add a tiny safety margin
    eps <- 1e-6
    angles_deg <- angles_deg[angles_deg > eps & angles_deg < 90 - eps]

    # (Optional) If θ is *extremely* close to 0° or 90°, ensure at least a tiny band:
    if (length(angles_deg) == 1) {
      # Use local small-angle approximation: Δθ ≈ 0.5 * r_max * sin(2θ)
      dtheta_deg <- 0.5 * r_max * sin(2 * theta0_deg * pi/180) * 180/pi
      dtheta_deg <- max(dtheta_deg, 0.01)  # minimum 0.01°
      angles_deg <- sort(unique(c(theta0_deg - 2*dtheta_deg,
                                  theta0_deg - 1*dtheta_deg,
                                  theta0_deg,
                                  theta0_deg + 1*dtheta_deg,
                                  theta0_deg + 2*dtheta_deg)))
      angles_deg <- angles_deg[angles_deg > eps & angles_deg < 90 - eps]
    }

    # Convert to your relative units if needed (unchanged)
    relative.signal.angle.range <- (angles_deg*pi/180 - pi/4) / (pi/4)

    # --- Verbose logging (kept in your style) ---
    cat(file=stderr(), "\n[Calibration] --- Stable angle grid (log-β) ---\n")
    cat(file=stderr(), "[Calibration] slope_angle (deg) =", round(theta0_deg, 6), "\n")
    cat(file=stderr(), "[Calibration] r_max (β rel. change) = ±", r_max, "\n")
    cat(file=stderr(), "[Calibration] testing absolute signal angles (deg) =",
        paste(round(angles_deg, 6), collapse = ", "), "\n")
    cat(file=stderr(), "[Calibration] mapped β grid =",
        paste(signif(beta_grid, 6), collapse = ", "), "\n")
    cat(file=stderr(), "[Calibration] -----------------------------------\n\n")

    # -------------------------- new -------------------------- #

    # === build normalized signal vector for calibration ===
    base.signal.angle <- atan(slope.regression)  # regression angle in radians
    signal.vector <- c(cos(base.signal.angle), -base.direction * sin(base.signal.angle))
    normalized.signal.vector <- signal.vector / sqrt(sum(signal.vector^2))

    # === margin inverse vector ===
    margin.inv.vector <- c(-normalized.signal.vector[2], normalized.signal.vector[1])
    margin.inv.slope  <- margin.inv.vector[2] / margin.inv.vector[1]

    # === construct signal prices (needed for calibration) ===
    signal.prices <- cbind(
      prices.bbo.a.b[,"bid.a"] * normalized.signal.vector[1] +
        prices.bbo.a.b[,"ask.b"] * normalized.signal.vector[2],
      prices.bbo.a.b[,"ask.a"] * normalized.signal.vector[1] +
        prices.bbo.a.b[,"bid.b"] * normalized.signal.vector[2]
    )

    theo.price <- mean(signal.prices[1,])

    # === run calibration to set relative.margin.range ===
     relative.margin.range <- calibrate_margin_range(
       prices = prices.bbo.a.b,
       normalized.signal.vector = normalized.signal.vector,
       margin.inv.vector = margin.inv.vector,
       margin.inv.slope = margin.inv.slope,
       tick.size.a = tick.size.a,
       tick.size.b = tick.size.b,
       fx.a = fx.a,
       fx.b = fx.b,
       min_crossings = MIN_CROSSING,
       max_crossings = MIN_CROSSING*2
     )

    # relative.margin.range    <- seq(from=10, to=100, by=10)

    # -------------------------- end new -------------------------- #

    ###########  Display progress bar for each pair

    # loop over all grid of parameters -> brut force calculation, keep track of run id
    num.loops               <- length(relative.signal.angle.range) * length(relative.margin.range) * length(relative.step.back.range) *
      length(relative.trading.angle.range) * length(relative.order.size.range) * length(num.crossing.2.limit.range)
    iter.num                <- 1
    num.optima              <- 0

    ###########  max bid-ask filter -> not used as maker...

    if(max.bid.ask.filter) {
      max.spread.prob <- 0.9995
      window.size     <- 100 # number of ticks

      # smooth spread
      roll.smooth     <- function(x, k) rollapply(x, k, function(x) mean(x), partial=TRUE)

      quantile.a      <- quantile(roll.smooth(prices.bbo.a.b[,3]-prices.bbo.a.b[,2], window.size), probs=max.spread.prob)
      quantile.b      <- quantile(roll.smooth(prices.bbo.a.b[,5]-prices.bbo.a.b[,4], window.size), probs=max.spread.prob)

      idx.max.bid.ask.spread.a <- roll.smooth(prices.bbo.a.b[,3]-prices.bbo.a.b[,2], window.size)>quantile.a
      idx.max.bid.ask.spread.b <- roll.smooth(prices.bbo.a.b[,5]-prices.bbo.a.b[,4], window.size)>quantile.b

      # filter outliers
      prices.bbo.a.b.filter                                   <- prices.bbo.a.b
      prices.bbo.a.b.filter[idx.max.bid.ask.spread.a, c(2,3)] <- NA
      prices.bbo.a.b.filter[idx.max.bid.ask.spread.b, c(4,5)] <- NA

      prices.bbo.a.b.filter <- na.locf(prices.bbo.a.b.filter)

      par(mfrow=c(2,1))
      idx <- 1:nrow(prices.bbo.a.b)
      plot((prices.bbo.a.b[idx,2]+prices.bbo.a.b[idx,3])/2, type='l')
      plot((prices.bbo.a.b[idx,4]+prices.bbo.a.b[idx,5])/2, type='l')
    }

    ###########  plot prices and regression for the in-sample period

    # plot mid-price
    if(export2pdf) {
      par(mfrow=c(3,1))
      plot((prices.bbo.a.b[,2]+prices.bbo.a.b[,3])/2, type='l')
      plot((prices.bbo.a.b[,4]+prices.bbo.a.b[,5])/2, type='l')

      idx.order.a <- order(prices.bbo.a.b[,"bid.a"])
      plot(cbind(prices.bbo.a.b[idx.order.a,"bid.a"], prices.bbo.a.b[idx.order.a,"bid.b"]))
      lines(cbind(prices.bbo.a.b[, "bid.a"], intercept.regression+slope.regression*prices.bbo.a.b[, "bid.a"]), col=2)
    }

    ###########  In case the products are not quoted in USD -> deal with FX rates conversion

    prices.bbo.a.b[, c("bid.a", "ask.a", "bid.b", "ask.b")]       <- t(t(prices.bbo.a.b[, c("bid.a", "ask.a", "bid.b", "ask.b")]) * c(fx.a,fx.a,fx.b,fx.b))

    if(max.bid.ask.filter) {
      prices.bbo.a.b.filter[, c("bid.a", "ask.a", "bid.b", "ask.b")]       <- t(t(prices.bbo.a.b.filter[, c("bid.a", "ask.a", "bid.b", "ask.b")]) * c(fx.a,fx.a,fx.b,fx.b))
      names(prices.bbo.a.b.filter) <- c("time_seconds", "bid.a", "ask.a", "bid.b", "ask.b") # backward compatibility
    }

    ###########  Set init parameters from product information

    # Determine Order.size.range, Max.order.in.delta, delta is the product lot size
    #order.size.range       <- cbind(relative.order.size.range * min.order.size.a, relative.order.size.range * min.order.size.b)

    # for crypto currencies don't consider lot size/delta and keep ordersize a,b relativelly aligned...
    order.size.range       <- relative.order.size.range

    #max.delta.orders       <- c(max.order.size.a * delta.a, max.order.size.b * delta.b)

    # Compute minimum bid-ask spreads for Margin calculation (2 versions)
    # if one tick...minimum.spreads            <- c(tick.size.a, tick.size.b)
    # beware: we disregard null bid-ask spreads
    idx.bid.ask.not.null.a  <- which(prices.bbo.a.b[,"bid.a"]-prices.bbo.a.b[,"ask.a"]!=0)
    idx.bid.ask.not.null.b  <- which(prices.bbo.a.b[,"bid.b"]-prices.bbo.a.b[,"ask.b"]!=0)

    minimum.spreads         <- c(min(prices.bbo.a.b[idx.bid.ask.not.null.a,"ask.a"]-prices.bbo.a.b[idx.bid.ask.not.null.a,"bid.a"]),
                                 min(prices.bbo.a.b[idx.bid.ask.not.null.b,"ask.b"]-prices.bbo.a.b[idx.bid.ask.not.null.b,"bid.b"])) # one tick for liquid products

    # ensure that tick size as calculated is smaller or equal to the minimum spreads seen (i.e. min(dp) versus min(ask-bid))
    # will be used within the MM pnl calculation
    tickSize.a              <- min(tick.size.a*fx.a, minimum.spreads[1])
    tickSize.b              <- min(tick.size.b*fx.b, minimum.spreads[2])

    ###########  Core algorithm

    ###########  Compute signal angle

    for(i in 1:length(relative.signal.angle.range)) {

      relative.signal.angle <- relative.signal.angle.range[i]

      # base signal angle is constructed such that it is always in quadrant I
      base.signal.angle     <- relative.signal.angle*pi/4+pi/4

      if(max.bid.ask.filter) {
        price.2.use <- prices.bbo.a.b.filter
      } else {
        price.2.use <- prices.bbo.a.b
      }

      # determine whether positively or negatively correlated pair
      if(FALSE) {
        # positive base direction
        signal.vector            <- c(cos(base.signal.angle), base.direction * sin(base.signal.angle))
        signal.angle             <- atan(signal.vector[2]/signal.vector[1]) # don't add pi
        normalized.signal.vector <- signal.vector/sqrt(sum(signal.vector^2))
        signal.prices            <- cbind(price.2.use[, "bid.a"] * normalized.signal.vector[1] +
                                            price.2.use[, "bid.b"] * normalized.signal.vector[2],
                                          price.2.use[, "ask.a"] * normalized.signal.vector[1] +
                                            price.2.use[, "ask.b"] * normalized.signal.vector[2])
      } else {
        # negative base direction
        signal.vector            <- c(cos(base.signal.angle), -base.direction * sin(base.signal.angle))
        signal.angle             <- atan(signal.vector[2]/signal.vector[1]) + pi # add pi
        normalized.signal.vector <- signal.vector/sqrt(sum(signal.vector^2))
        signal.prices            <- cbind(price.2.use[, "bid.a"] * normalized.signal.vector[1] +
                                            price.2.use[, "ask.b"] * normalized.signal.vector[2],
                                          price.2.use[, "ask.a"] * normalized.signal.vector[1] +
                                            price.2.use[, "bid.b"] * normalized.signal.vector[2])
      }

      ###########  Compute margin

      for(j in 1:length(relative.margin.range)) {

        # TODO: factorize minimum profitability margin
        relative.margin            <- relative.margin.range[j]
        base.margin                <- max(abs(normalized.signal.vector[1] * minimum.spreads[1]/fx.a),
                                          abs(normalized.signal.vector[2] * minimum.spreads[2]/fx.b))

        # ensure that margin is a multiple of min spread or ticksize projected to the NSV
        margin                     <- base.margin * relative.margin

        ###########  Stepback calculation

        for(k in 1:length(relative.step.back.range)) {

          stepback <- relative.step.back.range[k] * margin # (ev. Multiplied by the margin or not)

          ###########  Prepare theoretical price and inputs

          if (nrow(signal.prices) == 0) {
            warning("signal.prices is empty, skipping iteration")
            next
          }
          if (ncol(signal.prices) < 2) {
            warning("signal.prices malformed (ncol < 2), skipping iteration")
            next
          }

          theo.price <- (signal.prices[1,1] + signal.prices[1,2]) / 2

          # sanity checks before calling C++ crossing
          if (length(theo.price) == 0 || is.na(theo.price)) {
            warning("theo.price missing, skipping iteration")
            next
          }
          if (is.na(margin) || is.na(stepback)) {
            warning("margin/stepback invalid, skipping iteration")
            next
          }

          ###########  Quoter
          margin.inv.vector <- c(-normalized.signal.vector[2], normalized.signal.vector[1])
          margin.inv.slope  <- margin.inv.vector[2] / margin.inv.vector[1]

          # extra input validation
          if (any(sapply(list(signal.prices[,1], signal.prices[,2],
                              price.2.use[,"bid.a"], price.2.use[,"ask.a"],
                              price.2.use[,"bid.b"], price.2.use[,"ask.b"]),
                         function(x) length(x) == 0))) {
            warning("One or more price vectors empty, skipping iteration")
            next
          }

          dPrice <- generateCrossing(signal.prices[,1], signal.prices[,2],
                                     price.2.use[,"bid.a"],
                                     price.2.use[,"ask.a"],
                                     price.2.use[,"bid.b"],
                                     price.2.use[,"ask.b"],
                                     theo.price, margin, stepback,
                                     margin.inv.vector[1], margin.inv.vector[2],
                                     margin.inv.slope,
                                     normalized.signal.vector[1], normalized.signal.vector[2],
                                     tick.size.a, tick.size.b)

        }
          # check crossings and how long a quote would have become a trade
          # compute trade price on the maker and taker side
          quote.level        <- data.frame(price.2.use,
                                           dPrice$aSellLevelA,
                                           dPrice$aBuyLevelA,
                                           dPrice$aSellLevelB,
                                           dPrice$aBuyLevelB)

          names(quote.level) <- c("timedate", "bid.a", "ask.a", "bid.b", "ask.b", "aSellLevelA", "aBuyLevelA", "aSellLevelB", "aBuyLevelB")

          ###########  Compute order size new version
          # if no crossing at all for that rp...go to the next iteration
          if(!any(dPrice$moveTheoPriceVec!=0)) next

          # to speed up process keep only non zero crossing
          #idx.occurence  <- dPrice$moveTheoPriceVec!=0

          ###########  Compute trading angle

          for(l in 1:length(relative.trading.angle.range)) {

            # Base.trading.angle is contructed such that it is always in quadrant I
            base.trading.angle        <- relative.trading.angle.range[l]*1/4*pi+1/4*pi
            #base.trading.angle        <- -0.99*1/4*pi+1/4*pi
            trading.vector            <- c(cos(base.trading.angle), -sin(base.trading.angle))
            normalized.trading.vector <- trading.vector/sqrt(sum(trading.vector^2)) # no need...

            ###########  Compute order size (legacy & Q)

            for(m in 1:length(relative.order.size.range)) {

              # consider min order size see above
              # modif USD: order.size.range should be a percentage invesment of the contract value
              order.size                <- abs(order.size.range[m] * normalized.trading.vector)
              typical.order.size        <- order.size.range[m]

              ###########  RM - SAG - i.e. number of crossing...prevent martingal mechanism...

              for(n in 1:length(num.crossing.2.limit.range)) {

                nc2l                      <- num.crossing.2.limit.range[n]

                base.order.size.a         <- order.size[1]
                base.order.size.b         <- order.size[2]

                max.inventories           <- abs(num.crossing.2.limit.range[n] * c(base.order.size.a, base.order.size.b))

                safe.position             <- nc2lInventoryControl(quote.level[,"bid.a"], quote.level[,"ask.a"], quote.level[,"bid.b"], quote.level[,"ask.b"],
                                                                  quote.level[,"aSellLevelA"], quote.level[,"aBuyLevelA"], quote.level[,"aSellLevelB"], quote.level[,"aBuyLevelB"],
                                                                  nc2l, base.order.size.a, base.order.size.b)

                # constrains to min order size

                clean.order.rounded       <- cbind(sign(safe.position$theOrderVecA)*floor(abs(safe.position$theOrderVecA)/min.order.size.a)*min.order.size.a,
                                                   sign(safe.position$theOrderVecB)*floor(abs(safe.position$theOrderVecB)/min.order.size.b)*min.order.size.b)

                safe.position.a.b         <- cbind(cumsum(clean.order.rounded[,1]), cumsum(clean.order.rounded[,2]))

                # necessary for pnl & fees calculation -> crossing index
                idx.xing                   <- clean.order.rounded[,1] !=0 | clean.order.rounded[,2] !=0

                ###########  PnL calculation

                precision.prices        <-  1.e-5
                idx.1                   <- quote.level[-nrow(quote.level),"aSellLevelA"] - quote.level[-1,"bid.a"] <= precision.prices
                idx.2                   <- quote.level[-nrow(quote.level),"aBuyLevelA"] - quote.level[-1,"ask.a"] >= -precision.prices
                idx.3                   <- quote.level[-nrow(quote.level),"aSellLevelB"] - quote.level[-1,"bid.b"] <= precision.prices
                idx.4                   <- quote.level[-nrow(quote.level),"aBuyLevelB"] - quote.level[-1,"ask.b"] >= -precision.prices

                trade.price.raw         <- cbind(rep(0, time=nrow(safe.position.a.b)),
                                                 rep(0, time=nrow(safe.position.a.b)),
                                                 rep(0, time=nrow(safe.position.a.b)),
                                                 rep(0, time=nrow(safe.position.a.b)))

                trade.price.raw[which(idx.1)+1,] <- cbind(quote.level[which(idx.1),"aSellLevelA"],
                                                          quote.level[which(idx.1),"aSellLevelA"],
                                                          quote.level[which(idx.1)+1,"ask.b"],
                                                          quote.level[which(idx.1)+1,"ask.b"])
                trade.price.raw[which(idx.2)+1,] <- cbind(quote.level[which(idx.2),"aBuyLevelA"],
                                                          quote.level[which(idx.2),"aBuyLevelA"],
                                                          quote.level[which(idx.2)+1,"bid.b"],
                                                          quote.level[which(idx.2)+1,"bid.b"])
                trade.price.raw[which(idx.3)+1,] <- cbind(quote.level[which(idx.3)+1,"ask.a"],
                                                          quote.level[which(idx.3)+1,"ask.a"],
                                                          quote.level[which(idx.3),"aSellLevelB"],
                                                          quote.level[which(idx.3),"aSellLevelB"])
                trade.price.raw[which(idx.4)+1,] <- cbind(quote.level[which(idx.4)+1,"bid.a"],
                                                          quote.level[which(idx.4)+1,"bid.a"],
                                                          quote.level[which(idx.4),"aBuyLevelB"],
                                                          quote.level[which(idx.4),"aBuyLevelB"])

                trade.price                      <- data.frame(prices.bbo.a.b[,1], trade.price.raw)

                # compute the transaction fees. -> retrieved dynamically from product_specs.r file
                transaction.fees                  <- cbind(rep(0, time=nrow(safe.position.a.b)),
                                                           rep(0, time=nrow(safe.position.a.b)))

                order.fees                   <- clean.order.rounded *0

                order.fees[which(idx.1),1]   <- clean.order.rounded[which(idx.1)+1,1]
                order.fees[which(idx.1)+1,2] <- clean.order.rounded[which(idx.1)+1,2]

                order.fees[which(idx.2),1]   <- clean.order.rounded[which(idx.2)+1,1]
                order.fees[which(idx.2)+1,2] <- clean.order.rounded[which(idx.2)+1,2]

                order.fees[which(idx.3)+1,1] <- clean.order.rounded[which(idx.3)+1,1]
                order.fees[which(idx.3),2]   <- clean.order.rounded[which(idx.3)+1,2]

                order.fees[which(idx.4)+1,1] <- clean.order.rounded[which(idx.4)+1,1]
                order.fees[which(idx.4),2]   <- clean.order.rounded[which(idx.4)+1,2]


                transaction.fees[which(idx.1)+1,] <- cbind(abs(order.fees[which(idx.1),1])*t.fees.maker.a, abs(order.fees[which(idx.1)+1,2])*t.fees.taker.b)
                transaction.fees[which(idx.2)+1,] <- cbind(abs(order.fees[which(idx.2),1])*t.fees.maker.a, abs(order.fees[which(idx.2)+1,2])*t.fees.taker.b)

                transaction.fees[which(idx.3)+1,] <- cbind(abs(order.fees[which(idx.3)+1,1])*t.fees.taker.a, abs(order.fees[which(idx.3),2])*t.fees.maker.b)
                transaction.fees[which(idx.4)+1,] <- cbind(abs(order.fees[which(idx.4)+1,1])*t.fees.taker.a, abs(order.fees[which(idx.4),2])*t.fees.maker.b)

                transaction.fees.cum              <- cbind(cumsum(transaction.fees[,1]), cumsum(transaction.fees[,2]))

                ###########  PnL calculation -> Version 2.0
                ###########  Inverse swap -> i.e. coin margined products

                if(!is.daily.hedged) {
                  t.fees.bc.a <- transaction.fees[idx.xing,1]/trade.price[idx.xing,3]
                  t.fees.bc.b <- transaction.fees[idx.xing,2]/trade.price[idx.xing,5]

                  pnl.dsc.a   <- cbind(-clean.order.rounded[idx.xing,1], clean.order.rounded[idx.xing,1]/trade.price[idx.xing,2]-t.fees.bc.a)
                  pnl.dsc.b   <- cbind(-clean.order.rounded[idx.xing,2], clean.order.rounded[idx.xing,2]/trade.price[idx.xing,4]-t.fees.bc.b)

                  pnl.cum.a <- cbind(cumsum(pnl.dsc.a[,1]),cumsum(pnl.dsc.a[,2]))
                  pnl.cum.b <- cbind(cumsum(pnl.dsc.b[,1]),cumsum(pnl.dsc.b[,2]))

                  # format to legacy format, WARNING: remove/comment 2 following lines if proceed to validation, see below...
                  pnl.cum.a <- pnl.cum.a[,1]+pnl.cum.a[,2]*trade.price[idx.xing,3]
                  pnl.cum.b <- pnl.cum.b[,1]+pnl.cum.b[,2]*trade.price[idx.xing,5]

                  plot(pnl.cum.a+pnl.cum.b, type='l')

                } else {

                  ###########################################################################################################
                  ###########  PnL calculation -> Version 3.0
                  ###########################################################################################################

                  # loop through days and compute intraday cummulative pnl
                  previous.quote.position.a <- 0
                  previous.quote.position.b <- 0

                  liquidation.mid.price.a <- NULL
                  liquidation.mid.price.b <- NULL

                  # dbg
                  trade.serie.a <- NULL
                  trade.serie.b <- NULL

                  fees.serie.a  <- NULL
                  fees.serie.b  <- NULL

                  # end dbg

                  # keep track of daily pnl for daily pnl sharpe calculation
                  daily.date.serie  <- NULL

                  for(w in 1:length(idx.is.eod)) {

                    # find crossings within that day (if any)
                    if(w==1){
                      idx.day      <- 1:idx.is.eod[1]
                    } else {
                      idx.day      <- (idx.is.eod[w-1]+1):idx.is.eod[w]
                    }

                    # daily crossing
                    idx.xing.day <- idx.xing[idx.day]

                    # compute liquidation price and entry price
                    entry.mid.price.a <- ifelse(is.null(liquidation.mid.price.a), head(prices.bbo.a.b[idx.day,2]+prices.bbo.a.b[idx.day,3],1)/2, liquidation.mid.price.a)
                    entry.mid.price.b <- ifelse(is.null(liquidation.mid.price.b), head(prices.bbo.a.b[idx.day,4]+prices.bbo.a.b[idx.day,5],1)/2, liquidation.mid.price.b)

                    liquidation.mid.price.a <- tail(prices.bbo.a.b[idx.day,2]+prices.bbo.a.b[idx.day,3],1)/2
                    liquidation.mid.price.b <- tail(prices.bbo.a.b[idx.day,4]+prices.bbo.a.b[idx.day,5],1)/2

                    pnl.dsc.a   <- NULL
                    pnl.dsc.b   <- NULL

                    # check that at least one crossing occured during that day
                    if(any(idx.xing.day)) {

                      # beware if there is only one crossing that day...not a matrix but a vector...
                      if(length(idx.xing.day)==1) {
                        t.fees.bc.a <- as.numeric(transaction.fees[idx.day,][1]/trade.price[idx.day,][3])
                        t.fees.bc.b <- as.numeric(transaction.fees[idx.day,][2]/trade.price[idx.day,][5])
                      } else {
                        t.fees.bc.a <- transaction.fees[idx.day,][idx.xing.day,1]/trade.price[idx.day,][idx.xing.day,3]
                        t.fees.bc.b <- transaction.fees[idx.day,][idx.xing.day,2]/trade.price[idx.day,][idx.xing.day,5]
                      }

                      fees.serie.a  <- c(fees.serie.a, 0)
                      fees.serie.b  <- c(fees.serie.b, 0)

                      # dbg

                      if(is.null(trade.serie.a)) {
                        trade.serie.a <- c(previous.quote.position.a, entry.mid.price.a)
                      } else {
                        trade.serie.a <- rbind(trade.serie.a, c(previous.quote.position.a, entry.mid.price.a))
                      }

                      if(is.null(trade.serie.b)) {
                        trade.serie.b <- c(previous.quote.position.b, entry.mid.price.b)
                      } else {
                        trade.serie.b <- rbind(trade.serie.b, c(previous.quote.position.b, entry.mid.price.b))
                      }

                      # dbg

                      # beware if there is only one crossing that day...not a matrix but a vector...
                      if(length(idx.xing.day)==1) {
                        trade.serie.a <- rbind(trade.serie.a, as.numeric(cbind(-clean.order.rounded[idx.day,][1], trade.price[idx.day,][2])))
                        trade.serie.b <- rbind(trade.serie.b, as.numeric(cbind(-clean.order.rounded[idx.day,][2], trade.price[idx.day,][4])))
                      } else {
                        trade.serie.a <- rbind(trade.serie.a, cbind(-clean.order.rounded[idx.day,][idx.xing.day,1], trade.price[idx.day,][idx.xing.day,2]))
                        trade.serie.b <- rbind(trade.serie.b, cbind(-clean.order.rounded[idx.day,][idx.xing.day,2], trade.price[idx.day,][idx.xing.day,4]))
                      }

                      fees.serie.a  <- c(fees.serie.a, t.fees.bc.a)
                      fees.serie.b  <- c(fees.serie.b, t.fees.bc.b)

                      # end dbg

                      # keep track of positions in quote w/o liquidation
                      previous.quote.position.a <- sum(pnl.dsc.a[,1])
                      previous.quote.position.b <- sum(pnl.dsc.b[,1])

                      # dbg
                      fees.serie.a  <- c(fees.serie.a, 0)
                      fees.serie.b  <- c(fees.serie.b, 0)

                      trade.serie.a <- rbind(trade.serie.a, c(-previous.quote.position.a, liquidation.mid.price.a))
                      trade.serie.b <- rbind(trade.serie.b, c(-previous.quote.position.b, liquidation.mid.price.b))
                      # end dbg

                    } else {

                      # dbg
                      fees.serie.a  <- c(fees.serie.a, 0)
                      fees.serie.b  <- c(fees.serie.b, 0)

                      if(is.null(trade.serie.a)) {
                        trade.serie.a <- c(previous.quote.position.a, entry.mid.price.a)
                      } else {
                        trade.serie.a <- rbind(trade.serie.a, c(previous.quote.position.a, entry.mid.price.a))
                      }

                      if(is.null(trade.serie.b)) {
                        trade.serie.b <- c(previous.quote.position.b, entry.mid.price.b)
                      } else {
                        trade.serie.b <- rbind(trade.serie.b, c(previous.quote.position.b, entry.mid.price.b))
                      }

                      # dbg

                      trade.serie.a <- rbind(trade.serie.a, c(-previous.quote.position.a, liquidation.mid.price.a))
                      trade.serie.b <- rbind(trade.serie.b, c(-previous.quote.position.b, liquidation.mid.price.b))

                      # jfa
                      fees.serie.a  <- c(fees.serie.a, 0)
                      fees.serie.b  <- c(fees.serie.b, 0)

                    }
                    # JF: TO BE UPDATED 20250801
                    daily.date.serie  <- c(daily.date.serie, rep(daily.date.is[w], nrow(trade.serie.a)-length(daily.date.serie)))

                  }

                  pnl.dsc.a   <- cbind(trade.serie.a[,1], -trade.serie.a[,1]/trade.serie.a[,2]-fees.serie.a)
                  pnl.dsc.b   <- cbind(trade.serie.b[,1], -trade.serie.b[,1]/trade.serie.b[,2]-fees.serie.b)

                  pnl.cum.a <- cbind(cumsum(pnl.dsc.a[,1]),cumsum(pnl.dsc.a[,2]))
                  pnl.cum.b <- cbind(cumsum(pnl.dsc.b[,1]),cumsum(pnl.dsc.b[,2]))

                  pnl.cum.dbg.a <- as.numeric(pnl.cum.a[,1]+pnl.cum.a[,2]*trade.serie.a[,2])
                  pnl.cum.dbg.b <- as.numeric(pnl.cum.b[,1]+pnl.cum.b[,2]*trade.serie.b[,2])

                  # format for legacy naming
                  pnl.cum.a     <- pnl.cum.dbg.a
                  pnl.cum.b     <- pnl.cum.dbg.b

                }

                pnl.wo.mh   <- pnl.cum.a + pnl.cum.b #usd.profit.cum[,1] + usd.profit.cum[,2]

                trade.serie.a_1 <- trade.serie.a
                trade.serie.b_1 <- trade.serie.b

                cat(file=stderr(), paste("[",Sys.time(),"]",
                                         paste0(" -- [", iter.num, "/", num.loops, '] completed --- optima # ',
                                                num.optima, " PnL: ", tail(pnl.wo.mh,1),
                                                " --- i:",i," # j:",j," # k:", k," # l:",l," # m:", m, "# n:", n), "\n", sep=""))

                # if (iter.num == num.loops) cat(': Done')
                iter.num         <- iter.num + 1 # increment grid search counter

                # move to next iteration if pnl <= 0
                if(length(pnl.wo.mh)<MIN_CROSSING) next # prevent error when pnl vector is empty...
                if(is.na(tail(pnl.wo.mh,1)) || tail(pnl.wo.mh,1)<=0) next

                ###########  Compute return statistics

                # eventually compute daily pnl to compute daily sharpe (sharpe distorision due to occurence frequency...)
                pnl.all            <- c(0, diff(pnl.wo.mh))
                pnl.daily          <- aggregate(as.numeric(pnl.all), by=list(daily.date.serie), FUN=sum)
                daily.sharpe       <- mean(pnl.daily[,2])/sd(pnl.daily[,2])

                if(daily.sharpe<MIN_SHARPE) next

                sharpe.ratio     <- daily.sharpe

                # # compute sharpe ratio or sortino ratio for non zero aggregated (2 legs) pnl
                pnl.dsc          <- diff(pnl.wo.mh)
                pnl.dsc.nz       <- pnl.dsc[pnl.dsc!=0]

                ###########################################################################################################
                # compute sortino ratio
                N                <- length(pnl.dsc.nz)
                mean_N           <- mean(pnl.dsc.nz) * N
                std_neg          <- sd(pnl.dsc.nz[pnl.dsc.nz<0]) * sqrt(N)
                sortino.ratio    <- mean_N / std_neg

                # compute max drawdown
                return.factor      <- mean(pnl.dsc.nz)/maxdrawdown(pnl.dsc)

                pnl.wo.mh.oos.lst <- run.oos.sim(prices.bbo.a.b.oos,
                                                 normalized.signal.vector,
                                                 margin,
                                                 stepback,
                                                 tick.size.a,
                                                 tick.size.b,
                                                 normalized.trading.vector,
                                                 base.order.size.a,
                                                 base.order.size.b,
                                                 nc2l,
                                                 fx.a,
                                                 fx.b,
                                                 min.order.size.a,
                                                 min.order.size.b,
                                                 t.fees.maker.a,
                                                 t.fees.maker.b,
                                                 t.fees.taker.a,
                                                 t.fees.taker.b,
                                                 type.a,
                                                 type.b,
                                                 is.daily.hedged,
                                                 idx.oos.eod,
                                                 daily.date)

                # legacy
                pnl.wo.mh.oos <- pnl.wo.mh.oos.lst$pnl.wo.mh

                # Always export PnL surface, even if bad
                if(is.export.pnl.surface) {
                  titl <- paste(normalized.signal.vector[1],"#",normalized.signal.vector[2],"#",margin,"#",
                                stepback,"#",normalized.trading.vector[1],"#", normalized.trading.vector[2],"#",
                                order.size.range[m],"#",num.crossing.2.limit.range[n], sep="")

                  if(length(pnl.wo.mh.oos) == 0) {
                    # create a zero series aligned to OOS timestamps
                    pnl.util.surface[[titl]] <- zoo(rep(0, nrow(prices.bbo.a.b.oos)),
                                                    order.by=prices.bbo.a.b.oos[,"time_seconds"])
                  } else {
                    idx.seq.oos <- trunc(seq(from = 1, to = nrow(prices.bbo.a.b.oos), length.out = length(pnl.wo.mh.oos)))
                    pnl.util.surface[[titl]] <- zoo(pnl.wo.mh.oos, order.by=prices.bbo.a.b.oos[idx.seq.oos,"time_seconds"])
                  }
                }



                num.crossing_oos <- sum(diff(pnl.wo.mh.oos) != 0)

                # === NEW: Linearity / monotonicity check on OOS pnl ===
                t <- 1:length(pnl.wo.mh.oos)
                fit <- lm(pnl.wo.mh.oos ~ t)
                slope <- coef(fit)[2]
                r2 <- summary(fit)$r.squared

                ###########  Export grid results for further utility surface analysis

                grid.util.surface <- rbind(grid.util.surface,
                   c(paste(
                     normalized.signal.vector[1],"#",
                     normalized.signal.vector[2],"#",
                     margin,"#",
                     stepback,"#",
                     normalized.trading.vector[1],"#",
                     normalized.trading.vector[2],"#",
                     order.size.range[m],"#",
                     num.crossing.2.limit.range[n], sep=""
                   ),
                     relative.signal.angle.range[i],
                     relative.margin.range[j],
                     relative.step.back.range[k],
                     relative.trading.angle.range[l],
                     relative.order.size.range[m],
                     num.crossing.2.limit.range[n],
                     sharpe.ratio,
                     tail(pnl.wo.mh,1),
                     sharpe.ratio,
                     tail(pnl.wo.mh.oos,1),
                     num.crossing_oos,
                     r2   # <---- NEW
                   )
                )


                # skip reporting/plot if performance is neg or zero crossings
                if(length(pnl.wo.mh.oos) == 0 || is.na(tail(pnl.wo.mh.oos,1)) || tail(pnl.wo.mh.oos,1)<=0) next
                if(length(pnl.wo.mh.oos)<MIN_CROSSING) next # prevent error when pnl vector is empty...

                ###########  Compute return statistics

                # eventually compute daily pnl to compute daily sharpe (sharpe distorision due to occurence frequency...)
                pnl.all            <- c(0, diff(pnl.wo.mh.oos))
                pnl.daily          <- aggregate(as.numeric(pnl.all), by=list(pnl.wo.mh.oos.lst$daily.date.serie), FUN=sum)
                daily.sharpe       <- mean(pnl.daily[,2])/sd(pnl.daily[,2])

                if(daily.sharpe<MIN_SHARPE) next

                # check for R2 value for filtering

                if (slope <= 0 || r2 < config_file$filtering$minimum_pnl_curve_r2) {
                  next  # reject this run
                }

                sharpe.ratio.oos <- daily.sharpe

                pnl.dsc          <- diff(pnl.wo.mh.oos)
                pnl.dsc.nz       <- pnl.dsc[pnl.dsc!=0]

                # -------------------- #
                # ---  Export plot --- #
                # -------------------- #

                # legacy implementation
                usd.profit.cum                                <- cbind(pnl.cum.a, pnl.cum.b)

                if(is.plot.results) {
                  par(mfrow=c(4,1), cex.main=0.8)
                  # plot pnl in USD
                  titl.relative <- paste(relative.signal.angle.range[i],"#",relative.margin.range[j],"#",relative.step.back.range[k],"#",
                                         relative.trading.angle.range[l],"#",relative.order.size.range[m],"#", num.crossing.2.limit.range[n], sep="")

                  # check is data availability
                  plot(pnl.wo.mh, type='l', ylim=c(min(pnl.wo.mh), max(pnl.wo.mh)), main=paste("In-Sample run (", head(is.dates.vect,1), "-", tail(is.dates.vect,1), ")", sep=""),
                       xlab=titl.relative)

                  # signature -> relative parameters for further single simulation calibration
                  titl <- paste(normalized.signal.vector[1],"#",normalized.signal.vector[2],"#",margin,"#",
                                stepback,"#",normalized.trading.vector[1],"#", normalized.trading.vector[2],"#",
                                order.size.range[m],"#",num.crossing.2.limit.range[n], sep="")

                  plot(pnl.wo.mh, type='l', ylim=c(min(pnl.wo.mh, usd.profit.cum[,1], usd.profit.cum[,2]),
                                                   max(pnl.wo.mh, usd.profit.cum[,1], usd.profit.cum[,2])),
                       main=titl, cex.main=0.5)
                  lines(usd.profit.cum[,1], col=2, lty=2)
                  lines(usd.profit.cum[,2], col=3, lty=2)

                  plot(safe.position.a.b[,1], col=2, type='l', ylim=c(min(safe.position.a.b[,1], safe.position.a.b[,2]),
                                                                      max(safe.position.a.b[,1], safe.position.a.b[,2])),
                       ylab="inventory", xlab="red -> product 1, green -> product 2",
                       main=paste("max inventories (USD) : (", max(abs(safe.position.a.b[,1])), " - ", max(abs(safe.position.a.b[,2])), ")", sep=""))
                  lines(safe.position.a.b[,2], col=3)

                  # before plotting check that there are crossings for the oos period...
                  plot(pnl.wo.mh.oos, type='l', ylim=c(min(pnl.wo.mh.oos), max(pnl.wo.mh.oos)), main=paste("OOS-Run(", head(oos.dates.vect,1), "-", tail(oos.dates.vect,1), ")", sep=""))

                }



                status.log <- paste(relative.signal.angle.range[i],"#",relative.margin.range[j],"#",relative.step.back.range[k],"#",
                                    relative.trading.angle.range[l],"#",relative.order.size.range[m],"#",num.crossing.2.limit.range[n],
                                    " ### pnl is: ", tail(pnl.wo.mh, 1),
                                    " ### pnl oos: ", tail(pnl.wo.mh.oos, 1),
                                    " ### sharpe.ratio: ", sharpe.ratio,
                                    " ### sharpe.ratio oos: ", sharpe.ratio.oos,
                                    " sortino.ratio: ", sortino.ratio, " return.factor: ", return.factor, sep="")

                cat(file=stderr(), paste("[",Sys.time(),"] New Optima found : ", status.log, "\n", sep=""))

                num.optima <- num.optima + 1 # keep track on number of optima/candidates...

                ###########################################################################################################
                ###########  End main loop
                ###########################################################################################################

              } # end n loop, i.e. num.crossing.2.limit.range
            } # end m loop, i.e. relative.order.size.range
          } # end l loop, i.e. relative.trading.angle.range
        } # end k loop, i.e. relative.step.back.range
      } # end j loop, i.e. relative.margin.range
    } # end i loop, i.e. relative.signal.angle.range

    ###########################################################################################################
    ###########  Export utility surface file and close pdf stream writer
    ###########################################################################################################
    if(!is.null(grid.util.surface)) {
    colnames(grid.util.surface) <- c(
      "absolute.parameters",
      "relative.signal.angle",
      "relative.margin",
      "relative.step.back",
      "relative.trading.angle",
      "relative.order.size",
      "num.crossing.2.limit",
      "sharpe.ratio",
      "pnl",
      "sharpe.ratio.oos",
      "pnl.oos",
      "num.crossing.oos",
      "r2"   # <---- NEW
    )

      to_export  <- paste(path.to.results, "/gs_", args[1], "_", args[2], "_", export.name.flag, ".csv", sep="")
      write.table(grid.util.surface, to_export, quote = FALSE, sep = ",", row.names = FALSE, append = FALSE)
    }

    #if(is.export.pnl.surface) {
    if(length(pnl.util.surface)) {

      # concatenate pnl results and export to file
      pnl.util.surface.raw        <- do.call(merge, pnl.util.surface)

      pnl.util.surface.clean      <- na.locf0(pnl.util.surface.raw)

      pnl.util.surface.mtx        <- as.matrix(pnl.util.surface.clean)
      pnl.util.surface.mtx[is.na(pnl.util.surface.mtx)] <- 0
      pnl.util.surface.mtx                              <- cbind(as.numeric(rownames(pnl.util.surface.mtx)), pnl.util.surface.mtx)
      colnames(pnl.util.surface.mtx)[1]                 <- "time_seconds"
      head(pnl.util.surface.mtx)

      to_export                   <- paste(path.to.results, "/pnl_", args[1], "_", args[2], "_", export.name.flag, ".csv", sep="")
      write.table(pnl.util.surface.mtx, to_export, quote = FALSE, sep = ",", row.names = FALSE, append = FALSE)
    }

    # close pdf writer
    if(export2pdf) dev.off()
   # end z

  ###########################################################################################################

