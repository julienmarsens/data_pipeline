###  Cleanup working environment

Sys.setenv (LANGUAGE="en")
remove(list=ls(all=T))
graphics.off()

options(digits=10)
Sys.setenv(TZ='UTC')

### load libs
library(zoo)
library(yaml)
library(rstudioapi)

# ------------ #
# --- PATH --- #
# ------------ #

# Get arguments
args <- commandArgs(trailingOnly = TRUE)

perso_local_path <- args[15]
perso_disk_path <- args[16]

repo_base_path  <- file.path(perso_local_path, "data_pipeline", "prod_report_module", "tools", "backtesting")
# torus data source
path.to.source  <- file.path(perso_disk_path, "market_data", "sync_market_data")
path.to.results  <- file.path(perso_disk_path, "results")

PNL_SAVING_PATH <- file.path(perso_local_path, "data_pipeline", "prod_report_module", "local_data", "backtest")

###  constant configuration

ASSET_1 <- args[1]
ASSET_2 <- args[2]

IN_SAMPLE_DATE_START  <- args[3]
IN_SAMPLE_DATE_END    <- args[4]
OUT_SAMPLE_DATE_START <- args[5]
OUT_SAMPLE_DATE_END   <- args[6]

OUT_SAMPLE_TIME_START <- args[7]

# Parse numerics once
param_vals <- as.numeric(args[8:13])
if (any(is.na(param_vals))) {
  stop("Numeric params could not be parsed: ", paste(args[8:13], collapse = ", "))
}

SIGNAL_ANGLE  <- param_vals[1]
MARGIN        <- param_vals[2]
STEP_BACK     <- param_vals[3]
TRADING_ANGLE <- param_vals[4]
ORDER_SIZE    <- param_vals[5]
MAX_CROSSING  <- as.integer(param_vals[6])  # if this should be integer

TRADER_ID <- args[14]

###

MIN_SHARPE <- 0.01
MIN_CROSSING <- 100
MIN_PNL_DRAWDOWN  <- 1

EXCHANGE_NAME <- "binance-coin-futures"

###  Choose from pairs list & export statement

export2pdf            <- TRUE
is.plot.results       <- TRUE
is.export.pnl.surface <- TRUE
is.daily.hedged       <- TRUE

export.name.flag   <- "_"
RELOAD             <- TRUE

# over one day
is_start_num <- as.integer(gsub("_", "", IN_SAMPLE_DATE_START))
is_end_num   <- as.integer(gsub("_", "", IN_SAMPLE_DATE_END))

oos_start_num <- as.integer(gsub("_", "", OUT_SAMPLE_DATE_START))
oos_end_num   <- as.integer(gsub("_", "", OUT_SAMPLE_DATE_END))

oos.start.timestamp <- OUT_SAMPLE_TIME_START

oos.dates <- c(oos_start_num, oos_end_num)
is.dates  <- c(is_start_num, is_end_num)

DISPLAY_CONFIGURATION_CPP <- FALSE
max.bid.ask.filter        <- FALSE

config_version <- paste(ASSET_1, ASSET_2)

###  Load and/or install R packages and libs

Rcpp::sourceCpp(paste(repo_base_path, "/", "quoter_algo.cpp", sep=""))
Rcpp::sourceCpp(paste(repo_base_path, "/", "quoter_rm.cpp", sep=""))
source(paste(repo_base_path, "/", "data_tools.r", sep=""))
source(paste(repo_base_path, "/", "product_specs.r", sep=""))
source(paste(repo_base_path, "/", "stats.r", sep=""))
source(paste(repo_base_path, "/", "core_quoter_slave.r", sep=""))

###  Single Run parameters

is.single.run <- TRUE

relative.signal.angle.range   <- SIGNAL_ANGLE
relative.margin.range         <- MARGIN
relative.step.back.range      <- STEP_BACK
relative.trading.angle.range  <- TRADING_ANGLE
relative.order.size.range     <- ORDER_SIZE
num.crossing.2.limit.range    <- MAX_CROSSING

###  Products grid implementation

build.comb <- function(products) {
  grid.prod <- expand.grid(products,products)
  idx.same.prod  <- grid.prod[,1]==grid.prod[,2]
  grid.prod.dupl <- grid.prod[!idx.same.prod,]
  grid.prod.unq <- grid.prod.dupl[1,]
  for(i in 2:nrow(grid.prod.dupl)) {
    condition <- is.na(match(grid.prod.dupl[i,1], grid.prod.unq[,2]) == match(grid.prod.dupl[i,2], grid.prod.unq[,1]))
    if(!condition) next
    grid.prod.unq <- rbind(grid.prod.unq, grid.prod.dupl[i,])
  }
  names(grid.prod.unq) <- c("product.a", "product.b")
  return(grid.prod.unq)
}

###  Read product specs and build combination

if(FALSE) {
  product.names    <- names(product.specs)[-c(1,2)]
  prod.combination <- build.comb(product.names)
  product.exchange.names.lst <- list()
  exchange.names.lst         <- list()
  product.names.lst          <- list()
  instrument.type.lst        <- list()
  prod.combination <- prod.combination[31:35,]
  for(i in 1:nrow(prod.combination)) {
    product.exchange.names.lst[[i]] <- c(as.character(prod.combination[i,1]), as.character(prod.combination[i,2]))
    prod.splt.a <- tolower(strsplit(product.exchange.names.lst[[i]][1], "_")[[1]][1:2])
    prod.splt.b <- tolower(strsplit(product.exchange.names.lst[[i]][2], "_")[[1]][1:2])
    exchange.names.lst[[i]]  <- c("binance-coin-futures", "binance-coin-futures")
    product.names.lst[[i]]   <- c(paste(prod.splt.a[1], prod.splt.a[2], sep="-"), paste(prod.splt.b[1], prod.splt.b[2], sep="-"))
    instrument.type.lst[[i]] <- c("INVERSE", "INVERSE")
  }
}

exchange.names.lst    <- list()
instrument.type.lst   <- list()
product.names.lst     <- list()

instrument.type.lst[[1]] <- c("INVERSE", "INVERSE")
exchange.names.lst[[1]]  <- c(EXCHANGE_NAME, EXCHANGE_NAME)
product.names.lst[[1]]   <- c(ASSET_1, ASSET_2)

###########################################################################################################
######### HELPER FUNCTIONS
###########################################################################################################
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

###########################################################################################################
###########  Main cross exchange market making algorithm
###########################################################################################################
for(z in 1:length(product.names.lst)) {

  exchange.names  <- exchange.names.lst[[z]]
  instrument.type <- instrument.type.lst[[z]]
  product.names   <- product.names.lst[[z]]

  grid.util.surface <- NULL
  pnl.util.surface  <- list()

  # Dates
  is.dates.vect  <- find.available.dates(is.dates,  path.to.source, product.names, exchange.names)
  oos.dates.vect <- find.available.dates(oos.dates, path.to.source, product.names, exchange.names)

  cat(file=stderr(), "\n###########################################################################################################\n")
  cat(file=stderr(), paste("[",Sys.time(),"] Starting new grid search for pair: ", toupper(exchange.names[1]), "-", product.names[1],
                           " versus ", toupper(exchange.names[2]), "-", product.names[2], "\n", sep=""))
  cat(file=stderr(), paste("[",Sys.time(),"] IS start date: ", head(is.dates.vect,1), " end date: ", tail(is.dates.vect,1),"\n", sep=""))
  cat(file=stderr(), paste("[",Sys.time(),"] OOS start date: ", head(oos.dates.vect,1), " end date: ", tail(oos.dates.vect,1),"\n", sep=""))
  cat(file=stderr(), "###########################################################################################################\n")

  if(length(is.dates.vect)==0 || length(oos.dates.vect)==0) next

  num.loops  <- length(relative.signal.angle.range) * length(relative.margin.range) * length(relative.step.back.range) *
                length(relative.trading.angle.range) * length(relative.order.size.range) * length(num.crossing.2.limit.range)
  iter.num   <- 1
  num.optima <- 0

  product.name.std.a  <- paste(toupper(gsub("-", "_", product.names[1])), "_", instrument.type[1], "_", toupper(exchange.names[1]), sep="")
  product.name.std.b  <- paste(toupper(gsub("-", "_", product.names[2])), "_", instrument.type[2], "_", toupper(exchange.names[2]), sep="")

  if(export2pdf) pdf(file = paste(path.to.results, "/PDF_", config_version, export.name.flag, ".pdf", sep=""))

  # specs
  prod.spec.a <- product.specs[[product.name.std.a]]
  prod.spec.b <- product.specs[[product.name.std.b]]

  type.a <- instrument.type[1]
  type.b <- instrument.type[2]

  fx.a <- as.numeric(prod.spec.a$fx.rate)
  fx.b <- as.numeric(prod.spec.b$fx.rate)

  min.order.size.a <- as.numeric(prod.spec.a$min.order.size)
  min.order.size.b <- as.numeric(prod.spec.b$min.order.size)

  lot.size.a  <- as.numeric(prod.spec.a$lot.size)
  lot.size.b  <- as.numeric(prod.spec.b$lot.size)

  tick.size.a <- as.numeric(prod.spec.a$tick.size)
  tick.size.b <- as.numeric(prod.spec.b$tick.size)

  t.fees.maker.a <- as.numeric(prod.spec.a$t.fees.maker)
  t.fees.maker.b <- as.numeric(prod.spec.b$t.fees.maker)

  t.fees.taker.a <- as.numeric(prod.spec.a$t.fees.taker)
  t.fees.taker.b <- as.numeric(prod.spec.b$t.fees.taker)

  # load data
  if(RELOAD) {
    prices.bbo.a.b     <- read_r_bbo_cpp_repl(exchange.names, product.names, is.dates.vect,  path.to.source)
    prices.bbo.a.b.oos <- read_r_bbo_cpp_repl(exchange.names, product.names, oos.dates.vect, path.to.source)

    if(is.single.run) {
      timestamp.oos <- as.POSIXct(prices.bbo.a.b.oos[,"time_seconds"]/1000, origin="1970-01-01")
      idx.oos.time  <- substr(timestamp.oos, 1, 19) >= paste(paste(substr(oos.dates[1], 1, 4), "-",
                                                                   substr(oos.dates[1], 5, 6), "-",
                                                                   substr(oos.dates[1], 7, 8), sep=""),
                                                             " ", oos.start.timestamp, sep="")
      prices.bbo.a.b.oos <- prices.bbo.a.b.oos[idx.oos.time,]
    }

    names(prices.bbo.a.b)     <- c("time_seconds", "bid.a", "ask.a", "bid.b", "ask.b")
    names(prices.bbo.a.b.oos) <- c("time_seconds", "bid.a", "ask.a", "bid.b", "ask.b")

    prices.bbo.a.b.org     <- prices.bbo.a.b
    prices.bbo.a.b.org.oos <- prices.bbo.a.b.oos
  } else {
    prices.bbo.a.b     <- prices.bbo.a.b.org
    prices.bbo.a.b.oos <- prices.bbo.a.b.org.oos
  }

  if(is.daily.hedged) {
    idx.is.eod  <- c(which(!duplicated(substr(as.POSIXct(prices.bbo.a.b[,"time_seconds"]/1000, origin="1970-01-01"),1,10)))[-1]-1,
                     nrow(prices.bbo.a.b))
    idx.oos.eod <- c(which(!duplicated(substr(as.POSIXct(prices.bbo.a.b.oos[,"time_seconds"]/1000, origin="1970-01-01"),1,10)))[-1]-1,
                     nrow(prices.bbo.a.b.oos))

    daily.date.is <- unique(substr(as.POSIXct(prices.bbo.a.b[,"time_seconds"]/1000, origin="1970-01-01"),1,10))
    daily.date    <- unique(substr(as.POSIXct(prices.bbo.a.b.oos[,"time_seconds"]/1000, origin="1970-01-01"),1,10))
  } else {
    idx.is.eod  <- NULL
    idx.oos.eod <- NULL
  }

  # regression
  regression.length <- nrow(prices.bbo.a.b)
  lm_b_a <- lm(bid.b~bid.a, data = head(prices.bbo.a.b, regression.length))
  summary(lm_b_a)

  intercept.regression <- lm_b_a$coefficients[1]
  slope.regression     <- lm_b_a$coefficients[2]
  signal.angle <- atan(slope.regression)*180/pi
  base.direction <- 1/slope.regression

  if(max.bid.ask.filter) {
    max.spread.prob <- 0.9995
    window.size     <- 100
    roll.smooth     <- function(x, k) rollapply(x, k, function(x) mean(x), partial=TRUE)
    quantile.a      <- quantile(roll.smooth(prices.bbo.a.b[,3]-prices.bbo.a.b[,2], window.size), probs=max.spread.prob)
    quantile.b      <- quantile(roll.smooth(prices.bbo.a.b[,5]-prices.bbo.a.b[,4], window.size), probs=max.spread.prob)
    idx.max.bid.ask.spread.a <- roll.smooth(prices.bbo.a.b[,3]-prices.bbo.a.b[,2], window.size)>quantile.a
    idx.max.bid.ask.spread.b <- roll.smooth(prices.bbo.a.b[,5]-prices.bbo.a.b[,4], window.size)>quantile.b
    prices.bbo.a.b.filter                                   <- prices.bbo.a.b
    prices.bbo.a.b.filter[idx.max.bid.ask.spread.a, c(2,3)] <- NA
    prices.bbo.a.b.filter[idx.max.bid.ask.spread.b, c(4,5)] <- NA
    prices.bbo.a.b.filter <- na.locf(prices.bbo.a.b.filter)
    par(mfrow=c(2,1))
    idx <- 1:nrow(prices.bbo.a.b)
    plot((prices.bbo.a.b[idx,2]+prices.bbo.a.b[idx,3])/2, type='l')
    plot((prices.bbo.a.b[idx,4]+prices.bbo.a.b[idx,5])/2, type='l')
  }

  if(export2pdf) {
    par(mfrow=c(3,1))
    plot((prices.bbo.a.b[,2]+prices.bbo.a.b[,3])/2, type='l')
    plot((prices.bbo.a.b[,4]+prices.bbo.a.b[,5])/2, type='l')
    idx.order.a <- order(prices.bbo.a.b[,"bid.a"])
    plot(cbind(prices.bbo.a.b[idx.order.a,"bid.a"], prices.bbo.a.b[idx.order.a,"bid.b"]))
    lines(cbind(prices.bbo.a.b[, "bid.a"], intercept.regression+slope.regression*prices.bbo.a.b[, "bid.a"]), col=2)
  }

  # FX conversion if needed
  prices.bbo.a.b[, c("bid.a", "ask.a", "bid.b", "ask.b")] <-
    t(t(prices.bbo.a.b[, c("bid.a", "ask.a", "bid.b", "ask.b")]) * c(fx.a,fx.a,fx.b,fx.b))
  if(max.bid.ask.filter) {
    prices.bbo.a.b.filter[, c("bid.a", "ask.a", "bid.b", "ask.b")] <-
      t(t(prices.bbo.a.b.filter[, c("bid.a", "ask.a", "bid.b", "ask.b")]) * c(fx.a,fx.a,fx.b,fx.b))
    names(prices.bbo.a.b.filter) <- c("time_seconds", "bid.a", "ask.a", "bid.b", "ask.b")
  }

  order.size.range <- relative.order.size.range

  idx.bid.ask.not.null.a <- which(prices.bbo.a.b[,"bid.a"]-prices.bbo.a.b[,"ask.a"]!=0)
  idx.bid.ask.not.null.b <- which(prices.bbo.a.b[,"bid.b"]-prices.bbo.a.b[,"ask.b"]!=0)

  minimum.spreads <- c(min(prices.bbo.a.b[idx.bid.ask.not.null.a,"ask.a"]-prices.bbo.a.b[idx.bid.ask.not.null.a,"bid.a"]),
                       min(prices.bbo.a.b[idx.bid.ask.not.null.b,"ask.b"]-prices.bbo.a.b[idx.bid.ask.not.null.b,"bid.b"]))

  tickSize.a <- min(tick.size.a*fx.a, minimum.spreads[1])
  tickSize.b <- min(tick.size.b*fx.b, minimum.spreads[2])

  ###########################################################################################################
  ###########  Core grid
  ###########################################################################################################
  for(i in 1:length(relative.signal.angle.range)) {

    relative.signal.angle <- relative.signal.angle.range[i]
    base.signal.angle     <- relative.signal.angle*pi/4+pi/4

    price.2.use <- if(max.bid.ask.filter) prices.bbo.a.b.filter else prices.bbo.a.b

    if(FALSE) {
      signal.vector            <- c(cos(base.signal.angle), base.direction * sin(base.signal.angle))
      signal.angle             <- atan(signal.vector[2]/signal.vector[1])
      normalized.signal.vector <- signal.vector/sqrt(sum(signal.vector^2))
      signal.prices            <- cbind(price.2.use[, "bid.a"] * normalized.signal.vector[1] +
                                          price.2.use[, "bid.b"] * normalized.signal.vector[2],
                                        price.2.use[, "ask.a"] * normalized.signal.vector[1] +
                                          price.2.use[, "ask.b"] * normalized.signal.vector[2])
    } else {
      signal.vector            <- c(cos(base.signal.angle), -base.direction * sin(base.signal.angle))
      signal.angle             <- atan(signal.vector[2]/signal.vector[1]) + pi
      normalized.signal.vector <- signal.vector/sqrt(sum(signal.vector^2))
      signal.prices            <- cbind(price.2.use[, "bid.a"] * normalized.signal.vector[1] +
                                          price.2.use[, "ask.b"] * normalized.signal.vector[2],
                                        price.2.use[, "ask.a"] * normalized.signal.vector[1] +
                                          price.2.use[, "bid.b"] * normalized.signal.vector[2])
    }

    for(j in 1:length(relative.margin.range)) {
      relative.margin <- relative.margin.range[j]
      base.margin     <- max(abs(normalized.signal.vector[1] * minimum.spreads[1]/fx.a),
                             abs(normalized.signal.vector[2] * minimum.spreads[2]/fx.b))
      margin          <- base.margin * relative.margin

      for(k in 1:length(relative.step.back.range)) {
        stepback <- relative.step.back.range[k]*margin

        theo.price        <- (signal.prices[1,1]+signal.prices[1,2])/2
        margin.inv.vector <- c(-normalized.signal.vector[2],normalized.signal.vector[1])
        margin.inv.slope  <- margin.inv.vector[2]/margin.inv.vector[1]




        dPrice <- generateCrossing(signal.prices[,1], signal.prices[,2],
                                   price.2.use[, "bid.a"], price.2.use[, "ask.a"],
                                   price.2.use[, "bid.b"], price.2.use[, "ask.b"],
                                   theo.price, margin, stepback,
                                   margin.inv.vector[1], margin.inv.vector[2],
                                   margin.inv.slope,
                                   normalized.signal.vector[1], normalized.signal.vector[2],
                                   tick.size.a, tick.size.b)

        quote.level <- data.frame(price.2.use,
                                  dPrice$aSellLevelA, dPrice$aBuyLevelA,
                                  dPrice$aSellLevelB, dPrice$aBuyLevelB)
        names(quote.level) <- c("timedate", "bid.a", "ask.a", "bid.b", "ask.b",
                                "aSellLevelA","aBuyLevelA","aSellLevelB","aBuyLevelB")

        if(!any(dPrice$moveTheoPriceVec!=0)) next

        for(l in 1:length(relative.trading.angle.range)) {
          base.trading.angle        <- relative.trading.angle.range[l]*1/4*pi+1/4*pi
          trading.vector            <- c(cos(base.trading.angle), -sin(base.trading.angle))
          normalized.trading.vector <- trading.vector/sqrt(sum(trading.vector^2))

          for(m in 1:length(relative.order.size.range)) {

            order.size         <- abs(order.size.range[m] * normalized.trading.vector)
            typical.order.size <- order.size.range[m]

            for(n in 1:length(num.crossing.2.limit.range)) {

              nc2l <- num.crossing.2.limit.range[n]

              base.order.size.a <- order.size[1]
              base.order.size.b <- order.size[2]

              safe.position <- nc2lInventoryControl(quote.level[,"bid.a"], quote.level[,"ask.a"],
                                                    quote.level[,"bid.b"], quote.level[,"ask.b"],
                                                    quote.level[,"aSellLevelA"], quote.level[,"aBuyLevelA"],
                                                    quote.level[,"aSellLevelB"], quote.level[,"aBuyLevelB"],
                                                    nc2l, base.order.size.a, base.order.size.b)

              clean.order.rounded <- cbind(sign(safe.position$theOrderVecA)*floor(abs(safe.position$theOrderVecA)/min.order.size.a)*min.order.size.a,
                                           sign(safe.position$theOrderVecB)*floor(abs(safe.position$theOrderVecB)/min.order.size.b)*min.order.size.b)

              safe.position.a.b <- cbind(cumsum(clean.order.rounded[,1]), cumsum(clean.order.rounded[,2]))

              idx.xing <- clean.order.rounded[,1] !=0 | clean.order.rounded[,2] !=0

              precision.prices <- 1.e-5
              idx.1 <- quote.level[-nrow(quote.level),"aSellLevelA"] - quote.level[-1,"bid.a"] <= precision.prices
              idx.2 <- quote.level[-nrow(quote.level),"aBuyLevelA"]  - quote.level[-1,"ask.a"] >= -precision.prices
              idx.3 <- quote.level[-nrow(quote.level),"aSellLevelB"] - quote.level[-1,"bid.b"] <= precision.prices
              idx.4 <- quote.level[-nrow(quote.level),"aBuyLevelB"]  - quote.level[-1,"ask.b"] >= -precision.prices

              trade.price.raw <- cbind(rep(0, nrow(safe.position.a.b)),
                                       rep(0, nrow(safe.position.a.b)),
                                       rep(0, nrow(safe.position.a.b)),
                                       rep(0, nrow(safe.position.a.b)))
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

              trade.price <- data.frame(prices.bbo.a.b[,1], trade.price.raw)

              transaction.fees <- cbind(rep(0, nrow(safe.position.a.b)),
                                        rep(0, nrow(safe.position.a.b)))

              order.fees <- clean.order.rounded*0
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

              transaction.fees.cum <- cbind(cumsum(transaction.fees[,1]), cumsum(transaction.fees[,2]))

              if(!is.daily.hedged) {
                # legacy non-daily branch (kept)
                t.fees.bc.a <- as.numeric(transaction.fees[idx.day, 1] / trade.price[idx.day, 3])
                t.fees.bc.b <- as.numeric(transaction.fees[idx.day, 2] / trade.price[idx.day, 5])
                pnl.dsc.a   <- cbind(-clean.order.rounded[idx.xing,1], clean.order.rounded[idx.xing,1]/trade.price[idx.xing,2]-t.fees.bc.a)
                pnl.dsc.b   <- cbind(-clean.order.rounded[idx.xing,2], clean.order.rounded[idx.xing,2]/trade.price[idx.xing,4]-t.fees.bc.b)
                pnl.cum.a <- cbind(cumsum(pnl.dsc.a[,1]),cumsum(pnl.dsc.a[,2]))
                pnl.cum.b <- cbind(cumsum(pnl.dsc.b[,1]),cumsum(pnl.dsc.b[,2]))
                pnl.cum.a <- pnl.cum.a[,1]+pnl.cum.a[,2]*trade.price[idx.xing,3]
                pnl.cum.b <- pnl.cum.b[,1]+pnl.cum.b[,2]*trade.price[idx.xing,5]
                plot(pnl.cum.a+pnl.cum.b, type='l')
              } else {
                ###########################################################################################################
                ###########  Daily-hedged IS PnL (build exact cumulative + record EOD event indices)
                ###########################################################################################################
                previous.quote.position.a <- 0
                previous.quote.position.b <- 0
                liquidation.mid.price.a <- NULL
                liquidation.mid.price.b <- NULL

                trade.serie.a <- NULL
                trade.serie.b <- NULL
                fees.serie.a  <- NULL
                fees.serie.b  <- NULL

                daily.date.serie <- NULL
                event_times_is   <- numeric(0)
                eod_event_idx_is <- integer(0)   # <<<<<< RECORD EOD EVENT ROWS HERE

                for(w in 1:length(idx.is.eod)) {
                  pre_n <- if (is.null(trade.serie.a)) 0 else nrow(trade.serie.a)

                  if(w==1){
                    idx.day <- 1:idx.is.eod[1]
                  } else {
                    idx.day <- (idx.is.eod[w-1]+1):idx.is.eod[w]
                  }

                  idx.xing.day   <- idx.xing[idx.day]
                  timestamps_day <- prices.bbo.a.b[idx.day, "time_seconds"]

                  entry.mid.price.a <- ifelse(is.null(liquidation.mid.price.a),
                                              head(prices.bbo.a.b[idx.day,2]+prices.bbo.a.b[idx.day,3],1)/2,
                                              liquidation.mid.price.a)
                  entry.mid.price.b <- ifelse(is.null(liquidation.mid.price.b),
                                              head(prices.bbo.a.b[idx.day,4]+prices.bbo.a.b[idx.day,5],1)/2,
                                              liquidation.mid.price.b)
                  liquidation.mid.price.a <- tail(prices.bbo.a.b[idx.day,2]+prices.bbo.a.b[idx.day,3],1)/2
                  liquidation.mid.price.b <- tail(prices.bbo.a.b[idx.day,4]+prices.bbo.a.b[idx.day,5],1)/2

                  if(any(idx.xing.day)) {
                    rows <- which(idx.xing.day)

                    t.fees.bc.a <- as.numeric(transaction.fees[idx.day[rows], 1] / trade.price[idx.day[rows], 3])
                    t.fees.bc.b <- as.numeric(transaction.fees[idx.day[rows], 2] / trade.price[idx.day[rows], 5])

                    fees.serie.a  <- c(fees.serie.a, 0)
                    fees.serie.b  <- c(fees.serie.b, 0)
                    if (is.null(trade.serie.a)) {
                      trade.serie.a <- c(previous.quote.position.a, entry.mid.price.a)
                    } else {
                      trade.serie.a <- rbind(trade.serie.a, c(previous.quote.position.a, entry.mid.price.a))
                    }
                    if (is.null(trade.serie.b)) {
                      trade.serie.b <- c(previous.quote.position.b, entry.mid.price.b)
                    } else {
                      trade.serie.b <- rbind(trade.serie.b, c(previous.quote.position.b, entry.mid.price.b))
                    }

                    trade.serie.a <- rbind(trade.serie.a,
                                           cbind(-clean.order.rounded[idx.day[rows], 1],
                                                 trade.price[idx.day[rows], 2]))
                    trade.serie.b <- rbind(trade.serie.b,
                                           cbind(-clean.order.rounded[idx.day[rows], 2],
                                                 trade.price[idx.day[rows], 4]))

                    fees.serie.a  <- c(fees.serie.a, t.fees.bc.a)
                    fees.serie.b  <- c(fees.serie.b, t.fees.bc.b)

                    delta_pos_a <- if (length(rows)) sum(-clean.order.rounded[idx.day[rows], 1]) else 0
                    delta_pos_b <- if (length(rows)) sum(-clean.order.rounded[idx.day[rows], 2]) else 0
                    previous.quote.position.a <- previous.quote.position.a + delta_pos_a
                    previous.quote.position.b <- previous.quote.position.b + delta_pos_b

                    fees.serie.a  <- c(fees.serie.a, 0)
                    fees.serie.b  <- c(fees.serie.b, 0)
                    trade.serie.a <- rbind(trade.serie.a, c(-previous.quote.position.a, liquidation.mid.price.a))
                    trade.serie.b <- rbind(trade.serie.b, c(-previous.quote.position.b, liquidation.mid.price.b))

                    # times
                    event_times_is <- c(event_times_is, timestamps_day[1], timestamps_day[rows], tail(timestamps_day, 1))
                    # RECORD EOD ROW
                    eod_event_idx_is <- c(eod_event_idx_is, nrow(trade.serie.a))

                  } else {
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
                    trade.serie.a <- rbind(trade.serie.a, c(-previous.quote.position.a, liquidation.mid.price.a))
                    trade.serie.b <- rbind(trade.serie.b, c(-previous.quote.position.b, liquidation.mid.price.b))

                    event_times_is   <- c(event_times_is, timestamps_day[1], tail(timestamps_day, 1))
                    eod_event_idx_is <- c(eod_event_idx_is, nrow(trade.serie.a))
                    fees.serie.a  <- c(fees.serie.a, 0)
                    fees.serie.b  <- c(fees.serie.b, 0)
                  }

                  post_n <- nrow(trade.serie.a)
                  daily.date.serie <- c(daily.date.serie, rep(daily.date.is[w], post_n - pre_n))
                }

                pnl.dsc.a <- cbind(trade.serie.a[,1], -trade.serie.a[,1]/trade.serie.a[,2]-fees.serie.a)
                pnl.dsc.b <- cbind(trade.serie.b[,1], -trade.serie.b[,1]/trade.serie.b[,2]-fees.serie.b)

                pnl.cum.a <- cbind(cumsum(pnl.dsc.a[,1]),cumsum(pnl.dsc.a[,2]))
                pnl.cum.b <- cbind(cumsum(pnl.dsc.b[,1]),cumsum(pnl.dsc.b[,2]))

                pnl.cum.dbg.a <- as.numeric(pnl.cum.a[,1]+pnl.cum.a[,2]*trade.serie.a[,2])
                pnl.cum.dbg.b <- as.numeric(pnl.cum.b[,1]+pnl.cum.b[,2]*trade.serie.b[,2])

                pnl.cum.a <- pnl.cum.dbg.a
                pnl.cum.b <- pnl.cum.dbg.b
              }

              ###########################################################################################################
              ###########  Aggregate
              ###########################################################################################################
              pnl.wo.mh <- pnl.cum.a + pnl.cum.b

              stopifnot(length(pnl.wo.mh) == length(event_times_is))
              event_times_is_posix <- as.POSIXct(event_times_is/1000, origin = "1970-01-01", tz = "UTC")

              cat(file=stderr(), paste("[",Sys.time(),"]",
                                       paste0(" -- [", iter.num, "/", num.loops, '] completed --- optima # ',
                                              num.optima, " PnL: ", tail(pnl.wo.mh,1),
                                              " --- i:",i," # j:",j," # k:", k," # l:",l," # m:", m, "# n:", n), "\n", sep=""))
              iter.num <- iter.num + 1

              ###########################################################################################################
              ###########  EXACT DAILY PNL (IS) FROM THE PDF SERIES USING EOD EVENT INDICES
              ###########################################################################################################
              # eod_event_idx_is was recorded exactly when the EOD liquidation snapshot was appended
              stopifnot(length(eod_event_idx_is) == length(daily.date.is))
              cum_eod_is    <- pnl.wo.mh[eod_event_idx_is]
              daily_pnl_is  <- c(cum_eod_is[1], diff(cum_eod_is))
              sharpe.ratio  <- mean(daily_pnl_is, na.rm = TRUE) / sd(daily_pnl_is, na.rm = TRUE)

              if (is.single.run) {
                # Event-level (matches PDF)
                is_df <- data.frame(
                  plot_index = seq_along(pnl.wo.mh),
                  time_ms    = as.numeric(event_times_is),
                  time_iso   = event_times_is_posix,
                  date       = substr(event_times_is_posix, 1, 10),
                  cum_pnl    = pnl.wo.mh,
                  leg_a      = as.numeric(pnl.cum.a),
                  leg_b      = as.numeric(pnl.cum.b)
                )
                to_export_is <- file.path(PNL_SAVING_PATH, paste0(TRADER_ID, "_backtest_IS.csv"))
                write.table(is_df, to_export_is, quote = FALSE, sep = ",", row.names = FALSE, append = FALSE)

                # Daily-level built from exact EOD indices (identical to PDF aggregation)
                is_daily_df <- data.frame(
                  date = daily.date.is,
                  daily_pnl = as.numeric(daily_pnl_is),
                  cum_pnl_day_eod = as.numeric(cum_eod_is),
                  stringsAsFactors = FALSE
                )
                to_export_is_daily <- file.path(PNL_SAVING_PATH, paste0(TRADER_ID, "_backtest_IS_daily.csv"))
                write.table(is_daily_df, to_export_is_daily, quote = FALSE, sep = ",", row.names = FALSE, append = FALSE)
              }

              # stats helpers
              pnl.dsc    <- diff(pnl.wo.mh)
              pnl.dsc.nz <- pnl.dsc[pnl.dsc!=0]

              ###########################################################################################################
              ###########  OOS backtest
              ###########################################################################################################
              ###########################################################################################################
              ###########  OOS backtest
              ###########################################################################################################

              # --- Debugging guards ---
              if (nrow(prices.bbo.a.b.oos) == 0) {
                stop(sprintf("[ERROR] OOS dataset is empty for %s-%s between %s and %s",
                             product.names[1], product.names[2],
                             head(oos.dates.vect,1), tail(oos.dates.vect,1)))
              }
              if (!all(c("time_seconds","bid.a","ask.a","bid.b","ask.b") %in% names(prices.bbo.a.b.oos))) {
                stop(sprintf("[ERROR] OOS dataset columns invalid. Got: %s",
                             paste(names(prices.bbo.a.b.oos), collapse=", ")))
              }
              if (length(idx.oos.eod) == 0) {
                warning(sprintf("[WARN] No OOS EOD indices found for %s-%s. Backtest may fail.",
                                product.names[1], product.names[2]))
              }

              # --- Safe run of OOS sim ---
              pnl.wo.mh.oos.lst <- tryCatch({
                run.oos.sim(prices.bbo.a.b.oos,
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
              }, error=function(e) {
                cat(file=stderr(),
                    sprintf("[ERROR] run.oos.sim failed for %s-%s OOS (%s to %s): %s\n",
                            product.names[1], product.names[2],
                            head(oos.dates.vect,1), tail(oos.dates.vect,1),
                            e$message))
                return(NULL)
              })

              # Skip if run failed
              if (is.null(pnl.wo.mh.oos.lst)) next


              pnl.wo.mh.oos <- pnl.wo.mh.oos.lst$pnl.wo.mh

              # timestamps OOS
              have_oos_events <- FALSE
              event_oos_ms <- NULL
              if (!is.null(pnl.wo.mh.oos.lst$event_times_oos)) {
                event_oos_ms <- as.numeric(pnl.wo.mh.oos.lst$event_times_oos)
                if (length(event_oos_ms) == length(pnl.wo.mh.oos)) have_oos_events <- TRUE
              }
              if (!have_oos_events) {
                idx.seq <- trunc(seq(from = 1, to = nrow(prices.bbo.a.b.oos), length.out = length(pnl.wo.mh.oos)))
                event_oos_ms <- prices.bbo.a.b.oos[idx.seq, "time_seconds"]
                cat(file = stderr(),
                    sprintf("[WARN] OOS event timestamps missing/mismatch (got %s, pnl=%s). Using resampled price timestamps.\n",
                            ifelse(is.null(pnl.wo.mh.oos.lst$event_times_oos), "NULL",
                                   length(pnl.wo.mh.oos.lst$event_times_oos)),
                            length(pnl.wo.mh.oos)))
              }
              #event_oos_posix <- as.POSIXct(event_oos_ms/1000, origin="1970-01-01", tz="UTC")

              ###########################################################################################################
              ###########  EXACT DAILY PNL (OOS) USING OOS DAY LABELS FROM SLAVE (MATCHES PDF)
              ###########################################################################################################
              #dates_vec_oos <- pnl.wo.mh.oos.lst$daily.date.serie
              #u_dates_oos   <- unique(dates_vec_oos)
              #eod_vals_oos  <- sapply(u_dates_oos, function(d) tail(pnl.wo.mh.oos[dates_vec_oos == d], 1))
              #daily_pnl_oos <- c(eod_vals_oos[1], diff(eod_vals_oos))
              #sharpe.ratio.oos <- mean(daily_pnl_oos, na.rm = TRUE) / sd(daily_pnl_oos, na.rm = TRUE)

              #if (is.single.run) {
                # Event-level OOS
                #oos_df <- data.frame(
                  #plot_index = seq_along(pnl.wo.mh.oos),
                  #time_ms    = as.numeric(event_oos_ms),
                  #time_iso   = event_oos_posix,
                  #date       = substr(event_oos_posix, 1, 10),
                  #cum_pnl    = pnl.wo.mh.oos
                #)
                #to_export_oos <- file.path(PNL_SAVING_PATH, paste0(TRADER_ID, "_backtest_OOS.csv"))
                #write.table(oos_df, to_export_oos, quote = FALSE, sep = ",", row.names = FALSE, append = FALSE)

                # Daily OOS (matches PDF)
                #oos_daily_df <- data.frame(
                 # date = u_dates_oos,
                 # daily_pnl = as.numeric(daily_pnl_oos),
                 # cum_pnl_day_eod = as.numeric(eod_vals_oos),
                #stringsAsFactors = FALSE
                # )
                #to_export_oos_daily <- file.path(PNL_SAVING_PATH, paste0(TRADER_ID, "_backtest_OOS_daily.csv"))
                #write.table(oos_daily_df, to_export_oos_daily, quote = FALSE, sep = ",", row.names = FALSE, append = FALSE)
              #}

              ############################################################################################################
              # sync timestamp for further validation
              idx.xing                       <- pnl.wo.mh.oos.lst$idx.xing
              idx.crossing.all.raw           <- sort(c(which(idx.xing), idx.oos.eod, idx.oos.eod+1))
              idx.crossing.all.clean         <- c(idx.crossing.all.raw[-length(idx.crossing.all.raw)], nrow(prices.bbo.a.b.oos))

              event_oos_ms                   <- prices.bbo.a.b.oos[idx.crossing.all.clean,1]

              event_oos_posix                <- as.POSIXct(event_oos_ms/1000, origin="1970-01-01", tz="UTC")

              ###########################################################################################################
              ###########  EXACT DAILY PNL (OOS) USING OOS DAY LABELS FROM SLAVE (MATCHES PDF)
              ###########################################################################################################
              dates_vec_oos    <- pnl.wo.mh.oos.lst$daily.date.serie
              u_dates_oos      <- unique(dates_vec_oos)
              eod_vals_oos     <- sapply(u_dates_oos, function(d) tail(pnl.wo.mh.oos[dates_vec_oos == d], 1))
              daily_pnl_oos    <- c(eod_vals_oos[1], diff(eod_vals_oos))
              sharpe.ratio.oos <- mean(daily_pnl_oos, na.rm = TRUE) / sd(daily_pnl_oos, na.rm = TRUE)

              if (is.single.run) {
                # Event-level OOS
                oos_df <- data.frame(
                  plot_index = seq_along(pnl.wo.mh.oos),
                  time_ms    = as.numeric(event_oos_ms),
                  time_iso   = event_oos_posix,
                  date       = substr(event_oos_posix, 1, 10),
                  cum_pnl    = pnl.wo.mh.oos
                )
                to_export_oos <- file.path(PNL_SAVING_PATH, paste0(TRADER_ID, "_backtest_OOS.csv"))
                write.table(oos_df, to_export_oos, quote = FALSE, sep = ",", row.names = FALSE, append = FALSE)

                # Daily OOS (matches PDF)
                oos_daily_df <- data.frame(
                  date = u_dates_oos,
                  daily_pnl = as.numeric(daily_pnl_oos),
                  cum_pnl_day_eod = as.numeric(eod_vals_oos),
                  stringsAsFactors = FALSE
                )
                to_export_oos_daily <- file.path(PNL_SAVING_PATH, paste0(TRADER_ID, "_backtest_OOS_daily.csv"))
                write.table(oos_daily_df, to_export_oos_daily, quote = FALSE, sep = ",", row.names = FALSE, append = FALSE)
              }

              ###########################################################################################################

              ###########################################################################################################
              ###########  Plots
              ###########################################################################################################
              usd.profit.cum <- cbind(pnl.cum.a, pnl.cum.b)

              if(is.plot.results) {
                par(mfrow=c(4,1), cex.main=0.8)
                titl.relative <- paste(relative.signal.angle.range[i],"#",relative.margin.range[j],"#",relative.step.back.range[k],"#",
                                       relative.trading.angle.range[l],"#",relative.order.size.range[m],"#", num.crossing.2.limit.range[n], sep="")
                plot(pnl.wo.mh, type='l', ylim=c(min(pnl.wo.mh), max(pnl.wo.mh)),
                     main=paste("In-Sample run (", head(is.dates.vect,1), "-", tail(is.dates.vect,1), ")", sep=""),
                     xlab=titl.relative)

                titl <- paste(normalized.signal.vector[1],"#",normalized.signal.vector[2],"#",margin,"#",
                              stepback,"#",normalized.trading.vector[1],"#", normalized.trading.vector[2],"#",
                              order.size.range[m],"#",num.crossing.2.limit.range[n], sep="")
                plot(pnl.wo.mh, type='l', ylim=c(min(pnl.wo.mh, usd.profit.cum[,1], usd.profit.cum[,2]),
                                                 max(pnl.wo.mh, usd.profit.cum[,1], usd.profit.cum[,2])),
                     main=titl, cex.main=0.5)
                lines(usd.profit.cum[,1], col=2, lty=2)
                lines(usd.profit.cum[,2], col=3, lty=2)

                plot(safe.position.a.b[,1], col=2, type='l',
                     ylim=c(min(safe.position.a.b[,1], safe.position.a.b[,2]),
                            max(safe.position.a.b[,1], safe.position.a.b[,2])),
                     ylab="inventory", xlab="red -> product 1, green -> product 2",
                     main=paste("max inventories (USD) : (", max(abs(safe.position.a.b[,1])), " - ", max(abs(safe.position.a.b[,2])), ")", sep=""))
                lines(safe.position.a.b[,2], col=3)

                plot(pnl.wo.mh.oos, type='l', ylim=c(min(pnl.wo.mh.oos), max(pnl.wo.mh.oos)),
                     main=paste("OOS-Run(", head(oos.dates.vect,1), "-", tail(oos.dates.vect,1), ")", sep=""))
              }

              ###########################################################################################################
              ###########  Surfaces / logs
              ###########################################################################################################
              if(is.export.pnl.surface) {
                titl <- paste(normalized.signal.vector[1],"#",normalized.signal.vector[2],"#",margin,"#",
                              stepback,"#",normalized.trading.vector[1],"#", normalized.trading.vector[2],"#",
                              order.size.range[m],"#",num.crossing.2.limit.range[n], sep="")
                pnl.util.surface[[titl]] <- zoo(pnl.wo.mh, order.by = as.numeric(event_times_is))
              }

              status.log <- paste(relative.signal.angle.range[i],"#",relative.margin.range[j],"#",relative.step.back.range[k],"#",
                                  relative.trading.angle.range[l],"#",relative.order.size.range[m],"#",num.crossing.2.limit.range[n],
                                  " ### pnl is: ", tail(pnl.wo.mh, 1),
                                  " ### pnl oos: ", tail(pnl.wo.mh.oos, 1),
                                  " ### sharpe.ratio: ", sharpe.ratio,
                                  " ### sharpe.ratio oos: ", sharpe.ratio.oos,
                                  sep="")
              cat(file=stderr(), paste("[",Sys.time(),"] New Optima found : ", status.log, "\n", sep=""))
              num.optima <- num.optima + 1
            } # n
          } # m
        } # l
      } # k
    } # j
  } # i

  ###########################################################################################################
  ###########  Export utility surface file and close pdf
  ###########################################################################################################
  if(!is.null(grid.util.surface)) {
    colnames(grid.util.surface) <- c("absolute.parameters", "relative.signal.angle", "relative.margin", "relative.step.back",
                                     "relative.trading.angle","relative.order.size", "num.crossing.2.limit",
                                     "sharpe.ratio", "pnl")
    to_export <- paste(path.to.results, "/gs_", product.name.std.a, "_vs_", product.name.std.b, "_", export.name.flag, ".csv", sep="")
    write.table(grid.util.surface, to_export, quote = FALSE, sep = ",", row.names = FALSE, append = FALSE)
  }

  if(length(pnl.util.surface)) {
    pnl.util.surface.raw   <- do.call(merge, pnl.util.surface)
    idx_ms <- as.numeric(index(pnl.util.surface.raw))
    time_seconds <- floor(idx_ms/1000)
    time_iso     <- as.POSIXct(time_seconds, origin="1970-01-01", tz="UTC")

    pnl.util.surface.df <- data.frame(
      time_seconds = time_seconds,
      time_iso     = time_iso,
      coredata(pnl.util.surface.raw),
      check.names = FALSE
    )
    to_export <- paste(path.to.results, "/pnl_", product.name.std.a, "_vs_", product.name.std.b, "_", export.name.flag, ".csv", sep="")
    write.table(pnl.util.surface.df, to_export, quote = FALSE, sep = ",", row.names = FALSE, append = FALSE)
  }

  if(export2pdf) dev.off()
} # end z
