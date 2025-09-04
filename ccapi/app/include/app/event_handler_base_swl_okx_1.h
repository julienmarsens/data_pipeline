#ifndef APP_INCLUDE_APP_EVENT_HANDLER_BASE_H_
#define APP_INCLUDE_APP_EVENT_HANDLER_BASE_H_
#ifndef APP_EVENT_HANDLER_BASE_ORDER_STATUS_NEW
#define APP_EVENT_HANDLER_BASE_ORDER_STATUS_NEW "NEW"
#endif
#ifndef APP_EVENT_HANDLER_BASE_ORDER_STATUS_CANCELED
#define APP_EVENT_HANDLER_BASE_ORDER_STATUS_CANCELED "CANCELED"
#endif
#ifndef CCAPI_EM_POSITION_QUANTITY
#define CCAPI_EM_POSITION_QUANTITY "QUANTITY"
#endif
#ifndef APP_EVENT_HANDLER_BASE_ORDER_STATUS_PARTIALLY_FILLED
#define APP_EVENT_HANDLER_BASE_ORDER_STATUS_PARTIALLY_FILLED "PARTIALLY_FILLED"
#endif
#ifndef APP_EVENT_HANDLER_BASE_ORDER_STATUS_FILLED
#define APP_EVENT_HANDLER_BASE_ORDER_STATUS_FILLED "FILLED"
#endif
#ifndef APP_PUBLIC_TRADE_LAST
#define APP_PUBLIC_TRADE_LAST 0
#endif
#ifndef APP_PUBLIC_TRADE_VOLUME
#define APP_PUBLIC_TRADE_VOLUME 1
#endif
#ifndef APP_PUBLIC_TRADE_VOLUME_IN_QUOTE
#define APP_PUBLIC_TRADE_VOLUME_IN_QUOTE 2
#endif
#include <sys/stat.h>

#include <chrono>
#include <cmath>
#include <random>
#include <sstream>

#include "app/common.h"

// #include "app/historical_market_data_event_processor.h"
#include "app/historical_market_data_event_processor_swl.h"
#include "app/order.h"
// #include "boost/optional/optional.hpp"
#include "boost/optional/optional.hpp"
#include "ccapi_cpp/ccapi_util_private.h"

// include mysql db libs
#include <cppconn/driver.h>
#include <cppconn/exception.h>
#include <cppconn/prepared_statement.h>
#include <cppconn/resultset.h>
#include <cppconn/statement.h>

#include "mysql_connection.h"

#ifndef CCAPI_APP_IS_BACKTEST
#include "ccapi_cpp/ccapi_session.h"
#else
#include <future>

#include "ccapi_cpp/ccapi_element.h"
#include "ccapi_cpp/ccapi_event.h"
#include "ccapi_cpp/ccapi_event_handler.h"
#include "ccapi_cpp/ccapi_message.h"
#include "ccapi_cpp/ccapi_request.h"
#include "ccapi_cpp/ccapi_subscription.h"

namespace ccapi {
class Session {
 public:
  virtual void subscribe(std::vector<Subscription>& subscriptionList) {}
  virtual void sendRequest(const Event& event, Session* session, std::vector<Request>& requestList) {}
  virtual void sendRequest(Request& request) {}
  virtual void sendRequestByWebsocket(Request& request) {}

  virtual void stop() {}
};
}  // namespace ccapi
#endif
// #include <filesystem>
namespace ccapi {
class EventHandlerBase : public EventHandler {
 public:
  enum class AppMode {
    MARKET_MAKING,
    SINGLE_ORDER_EXECUTION,
  };
  enum class TradingMode {
    LIVE,
    PAPER,
    BACKTEST,
  };
  enum class AdverseSelectionGuardActionType {
    NONE,
    MAKE,
    TAKE,
  };
  enum class AdverseSelectionGuardInformedTraderSide {
    NONE,
    BUY,
    SELL,
  };
  enum class TradingStrategy {
    TWAP,
    VWAP,
    POV,
    IS,
  };
  virtual ~EventHandlerBase() {}
  virtual void onInit(Session* session) {
    /* Create a connection */
    driver = get_driver_instance();

    // on multiple AWS instances
    // con = driver->connect("tcp://172.31.37.115:3306", "root", "Swlswl123$");

    // on local machines
    // con = driver->connect("tcp://127.0.0.1:3306", "root", "Swlswl123$");

    /* Connect to the MySQL test database */
    // con->setSchema("swl_perf");

    // configure database from config_file in order to adjust quickly if we have to migrate dashboard instance
    con = driver->connect(this->dashboardAddress, this->databaseLogin, this->databasePassword);

    /* Connect to the MySQL test database */
    con->setSchema(this->databaseName);

    if (this->isMysqlexport) {
      // transactions management
      pstmt = con->prepareStatement(
          "INSERT INTO overview(TraderId, BaseBalanceA, BaseBalanceB, QuoteBalanceA, QuoteBalanceB, BestBidPriceA, BestAskPriceA, BestBidPriceB, "
          "BestAskPriceB, TotalBalanceA, TotalBalanceB, TotalBalance) VALUES (?,?,?,?,?,?,?,?,?,?,?,?)");
    }

    // check sql query
    // select * from overview order by TS desc limit 10;

    // state management -> lookup for init state
    /*
    mysql> describe states;
    +------------------+--------------+------+-----+-------------------+-------------------+
    | Field            | Type         | Null | Key | Default           | Extra             |
    +------------------+--------------+------+-----+-------------------+-------------------+
    | TS               | timestamp    | YES  |     | CURRENT_TIMESTAMP | DEFAULT_GENERATED |
    | TraderId         | varchar(255) | YES  |     | NULL              |                   |
    | BaseBalanceA     | float(10,5)  | YES  |     | NULL              |                   |
    | BaseBalanceB     | float(10,5)  | YES  |     | NULL              |                   |
    | QuoteBalanceA    | float(10,5)  | YES  |     | NULL              |                   |
    | QuoteBalanceB    | float(10,5)  | YES  |     | NULL              |                   |
    | TheoreticalPrice | float(10,5)  | YES  |     | NULL              |                   |
    +------------------+--------------+------+-----+-------------------+-------------------+
    */
    pstmt_1 = con->prepareStatement("SELECT * FROM states WHERE TraderId='" + this->trader_id + "';");
    res = pstmt_1->executeQuery();

    bool isStateExist = false;
    while (res->next()) {
      isStateExist = true;
      this->baseBalance_a = res->getDouble("BaseBalanceA");
      this->baseBalance_b = res->getDouble("BaseBalanceB");

      this->quoteBalance_a = res->getDouble("QuoteBalanceA");
      this->quoteBalance_b = res->getDouble("QuoteBalanceB");

      this->theoreticalPrice = res->getDouble("TheoreticalPrice");
    }

    if (!isStateExist) {
      // is state doesn't exist for that trader then create it.
      pstmt_3 = con->prepareStatement(
          "INSERT INTO states(TraderId, BaseBalanceA, BaseBalanceB, QuoteBalanceA, QuoteBalanceB, TheoreticalPrice) VALUES (?,?,?,?,?,?)");

      pstmt_3->setString(1, this->trader_id.c_str());

      pstmt_3->setDouble(2, 0.0);
      pstmt_3->setDouble(3, 0.0);
      pstmt_3->setDouble(4, 0.0);
      pstmt_3->setDouble(5, 0.0);
      pstmt_3->setDouble(6, 0.0);

      pstmt_3->executeUpdate();  // insert into db

      // init inventory and states parameters
      this->baseBalance_a = 0.0;
      this->baseBalance_b = 0.0;

      this->quoteBalance_a = 0.0;
      this->quoteBalance_b = 0.0;

      this->theoreticalPrice = 0.0;

      isStateExist = true;
    }
    if (isStateExist && (this->isReinit || this->isReinitOrder)) {
      // is state exists for that trader then reinit it.
      pstmt_3 = con->prepareStatement(
          "UPDATE states SET BaseBalanceA=?, BaseBalanceB=?, QuoteBalanceA=?, QuoteBalanceB=?, TheoreticalPrice=? WHERE TraderId='" + this->trader_id + "';");

      pstmt_3->setDouble(1, 0.0);
      pstmt_3->setDouble(2, 0.0);
      pstmt_3->setDouble(3, 0.0);
      pstmt_3->setDouble(4, 0.0);
      pstmt_3->setDouble(5, 0.0);

      pstmt_3->executeUpdate();  // update db

      // init inventory and states parameters
      this->baseBalance_a = 0.0;
      this->baseBalance_b = 0.0;

      this->quoteBalance_a = 0.0;
      this->quoteBalance_b = 0.0;

      this->theoreticalPrice = 0.0;

      // export performance to mysql db

      pstmt->setString(1, this->trader_id.c_str());
      pstmt->setDouble(2, this->baseBalance_a);
      pstmt->setDouble(3, this->baseBalance_b);
      pstmt->setDouble(4, this->quoteBalance_a);
      pstmt->setDouble(5, this->quoteBalance_b);
      pstmt->setDouble(6, 0.0);
      pstmt->setDouble(7, 0.0);
      pstmt->setDouble(8, 0.0);
      pstmt->setDouble(9, 0.0);
      pstmt->setDouble(10, 0.0);
      pstmt->setDouble(11, 0.0);
      pstmt->setDouble(12, 0.0);

      pstmt->executeUpdate();  // insert into db

      isStateExist = true;
    }

    // update states prepare statement (at each crossing)
    pstmt_2 = con->prepareStatement("UPDATE states SET BaseBalanceA=?, BaseBalanceB=?, QuoteBalanceA=?, QuoteBalanceB=?, TheoreticalPrice=? WHERE TraderId='" +
                                    this->trader_id + "';");

    // Necessary in backtest and paper trading to set all subscription to true
    if (this->tradingMode == TradingMode::BACKTEST || this->tradingMode == TradingMode::PAPER) {
      this->isAllSubscriptionStarted = true;
    }

    // in case we use the liquidation flag, one has to set the isInit function
    if (this->isLiquitation) {
      this->isReinit = true;
    }

    // if we use liquidation order then use the default init order function BUT don't activate the quoter; i.e. onQuote
    if (this->isLiquidationOrder) {
      APP_LOGGER_INFO("#############################################################");
      APP_LOGGER_INFO("##### Liquidation orders function -> don't start quoter.");
      APP_LOGGER_INFO("#############################################################");

      this->isInitOrder = true;
    }

    // if we use reinit order then use the default init order function and activate the quoter; i.e. onQuote (but the difference would be in db update/init)
    if (this->isReinitOrder) {
      APP_LOGGER_INFO("#############################################################");
      APP_LOGGER_INFO("##### Reinit orders function -> start quoter after reinit parameters.");
      APP_LOGGER_INFO("#############################################################");

      this->isInitOrder = true;
    }

    // Necessary in backtest or paper trading in reinit mode to skip capture function waiting for liquidation...
    /*
    if(this->tradingMode == TradingMode::BACKTEST || this->tradingMode == TradingMode::PAPER) {
      this->isLiquidated_a = true;
      this->isLiquidated_b = true;
    }
    */
  }

  virtual void onClose(Session* session) {
    // at the end of backtest close sql connection...should be done in the main...
    if (this->isMysqlexport) {
      delete pstmt;
    }

    // close state management
    delete res;
    delete pstmt_1;
    delete pstmt_2;
    delete pstmt_3;
    delete con;
  }
  // Terminology
  // These terms will be used throughout the documentation, so it is recommended especially for new users to read to help their understanding of the API.

  // base asset refers to the asset that is the quantity of a symbol. For the symbol BTCUSDT, BTC would be the base asset.
  // quote asset refers to the asset that is the price of a symbol. For the symbol BTCUSDT, USDT would be the quote asset.

  // in order to use visual studio code on mac one has to down version openssl...see.
  // https://stackoverflow.com/questions/74059978/why-is-lldb-generating-exc-bad-instruction-with-user-compiled-library-on-macos

  // main code entry point
  bool processEvent(const Event& event, Session* session) override {
    if (this->skipProcessEvent) {
      return true;
    }

    // rate limit management
    // if reqQuantitySec >= MaxRequestSec or reqQuantityHour >= MaxreqestHour then breach -> stop the onQuote method that triggers request
    // (in addition to rebalancer/market order which correspond to two request (cancel and buy/sell order))

    // reset states and previous period start time
    // beware in backtest now has a different meaning as current machine time...must be retrieved from message time
    // backtest
    const auto& messageList = event.getMessageList();
    const auto& message = messageList.at(0);
    const std::string& messageTimeISO = UtilTime::getISOTimestamp(message.getTime());

    this->nowMessageTp = UtilTime::parse(messageTimeISO);
    //} //else {
    // this->nowTp = tpnow();
    //}

    // init previous crossing micro
    // if(this->previousCrossingMicro==0) {
    //  this->previousCrossingMicro=getUnixTimestampMicro(tpnow());
    //}

    // if(this->isMysqlexport) {
    //   this->previousExportMicro=getUnixTimestampMicro(tpnow());
    // }

    // this->previousSyncMicro=getUnixTimestampMicro(tpnow());

    /*
    mysql> use swl_perf;
    Database changed
    mysql> CREATE TABLE overview (
        ->     TS timestamp default current_timestamp,
        ->     TraderId varchar(255),
        ->     BaseBalanceA FLOAT(10,5),
        ->     BaseBalanceB FLOAT(10,5),
        ->     QuoteBalanceA FLOAT(10,5),
        ->     QuoteBalanceB FLOAT(10,5),
        ->     BestBidPriceA FLOAT(10,5),
        ->     BestAskPriceA FLOAT(10,5),
        ->     BestBidPriceB FLOAT(10,5),
        ->     BestAskPriceB FLOAT(10,5),
        ->     TotalBalanceA FLOAT(10,5),
        ->     TotalBalanceB FLOAT(10,5)
        -> );

    */

    // eventually export perf to mysql db for further visualization by grafana
    // if(this->isMysqlexport) this->processEventExportPerf(event, session, this->nowTp);
    if (this->isMysqlexport) this->processEventExportPerf(event, session, tpnow());

    // this->previousCrossingMicro = getUnixTimestampMicro(tpnow());
    // long previousCrossingMicro{}; = getUnixTimestampMicro(tpnow());

    /*
    // backtest
    const TimePoint& nowTp = UtilTime::parse(messageTimeISO); //UtilTime::now();

    if(!this->isInit) {
      previousShortTimePeriodTp = nowTp;
      previousLongTimePeriodTp = nowTp;
      this->isInit = true;
    }
    */
    // production

    // do we have short time limiter (i.e. BITMEX)
    /*
    if(this->isShortLimiter ) {
      // check for limit breaches
      //this->isShortTimeLimiterLimitBreached = (this->shortTimeIntervalNumberOfRequest) >= (this->shortTimeRateLimit-2)? true:false; // reduce by 2 because we
    should be able to match the other leg in case of trade... this->isShortTimeLimiterLimitBreached = (this->shortTimeIntervalNumberOfRequest) >=
    (this->shortTimeRateLimit)? true:false; // reduce by 2 because we should be able to match the other leg in case of trade...

      APP_LOGGER_DEBUG("####################################################################");
      APP_LOGGER_DEBUG("#### Check limiter condition");
      int nowInt = UtilTime::getUnixTimestamp(nowTp);
      APP_LOGGER_DEBUG("#### Now Unix:" + std::to_string(nowInt));
      APP_LOGGER_DEBUG("#### Previous Unix:" + std::to_string(UtilTime::getUnixTimestamp(this->previousShortTimePeriodTp)));
      APP_LOGGER_DEBUG("#### Previous Unix + delta long time:" + std::to_string(UtilTime::getUnixTimestamp((this->previousShortTimePeriodTp) +
    std::chrono::seconds(this->shortTimeLimiter))));

      if (nowTp >= this->previousShortTimePeriodTp + std::chrono::seconds(this->shortTimeLimiter)) {
        this->isShortTimeLimiterLimitBreached = false;
        this->shortTimeIntervalNumberOfRequest = 0;
        this->previousShortTimePeriodTp = nowTp;
      }
    }

    if(this->isLongLimiter ) {
      // check for limit breaches
      //this->isLongTimeLimiterLimitBreached = (this->longTimeIntervalNumberOfRequest) >= (this->longTimeRateLimit-2)? true:false;
      this->isLongTimeLimiterLimitBreached = (this->longTimeIntervalNumberOfRequest) >= (this->longTimeRateLimit)? true:false;

      if (nowTp >= this->previousLongTimePeriodTp + std::chrono::seconds(this->longTimeLimiter)) {
        this->isLongTimeLimiterLimitBreached = false;
        this->longTimeIntervalNumberOfRequest = 0;
        this->previousLongTimePeriodTp = nowTp;
      }
    }

    // verbose limit breach status
    if(this->isShortLimiter && this->isShortTimeLimiterLimitBreached) {
      const std::string& nowISO = UtilTime::getISOTimestamp(nowTp, "%FT%TZ");
      const std::string& startbucketISO = UtilTime::getISOTimestamp(this->previousShortTimePeriodTp, "%FT%TZ");
      APP_LOGGER_DEBUG("####################################################################");
      APP_LOGGER_DEBUG("#### Short time limit breached");
      APP_LOGGER_DEBUG("#### shortTimeIntervalNumberOfRequest: " + std::to_string(this->shortTimeIntervalNumberOfRequest));
      APP_LOGGER_DEBUG("#### current time: " + nowISO);
      APP_LOGGER_DEBUG("#### start time bucket: " + startbucketISO);
      APP_LOGGER_DEBUG("####################################################################");
    }

    if(this->isLongLimiter && this->isLongTimeLimiterLimitBreached) {
      const std::string& nowISO = UtilTime::getISOTimestamp(nowTp, "%FT%TZ");
      const std::string& startbucketISO = UtilTime::getISOTimestamp(this->previousLongTimePeriodTp, "%FT%TZ");
      APP_LOGGER_DEBUG("####################################################################");
      APP_LOGGER_DEBUG("#### Long time rate limit breached");
      APP_LOGGER_DEBUG("#### longTimeIntervalNumberOfRequest: " + std::to_string(this->longTimeIntervalNumberOfRequest));
      APP_LOGGER_DEBUG("#### current time: " + nowISO);
      APP_LOGGER_DEBUG("#### start time bucket: " + startbucketISO);
      APP_LOGGER_DEBUG("####################################################################");
    }
    */
    APP_LOGGER_DEBUG("******** version : 001");
    APP_LOGGER_DEBUG("Received an event: " + event.toStringPretty());

    if (!this->openBuyOrder_a) {
      this->numOpenOrdersBuy_a = 0;
    } else {
      this->numOpenOrdersBuy_a = 1;
    }
    if (!this->openSellOrder_a) {
      this->numOpenOrdersSell_a = 0;
    } else {
      this->numOpenOrdersSell_a = 1;
    }
    if (!this->openBuyOrder_b) {
      this->numOpenOrdersBuy_b = 0;
    } else {
      this->numOpenOrdersBuy_b = 1;
    }
    if (!this->openSellOrder_b) {
      this->numOpenOrdersSell_b = 0;
    } else {
      this->numOpenOrdersSell_b = 1;
    }

    APP_LOGGER_DEBUG("####################################################################");
    APP_LOGGER_DEBUG("#### num orders Buy A:" + std::to_string(this->numOpenOrdersBuy_a));
    APP_LOGGER_DEBUG("#### num orders Sell A:" + std::to_string(this->numOpenOrdersSell_a));
    APP_LOGGER_DEBUG("#### num orders Buy B:" + std::to_string(this->numOpenOrdersBuy_b));
    APP_LOGGER_DEBUG("#### num orders Sell B:" + std::to_string(this->numOpenOrdersSell_b));
    APP_LOGGER_DEBUG("####################################################################");

    if (this->openBuyOrder_a) {
      APP_LOGGER_DEBUG("Open buy orderA is " + this->openBuyOrder_a->toString() + ".");
    }
    if (this->openSellOrder_a) {
      APP_LOGGER_DEBUG("Open sell orderA is " + this->openSellOrder_a->toString() + ".");
    }

    if (this->openBuyOrder_b) {
      APP_LOGGER_DEBUG("Open buy orderB is " + this->openBuyOrder_b->toString() + ".");
    }
    if (this->openSellOrder_b) {
      APP_LOGGER_DEBUG("Open sell orderB is " + this->openSellOrder_b->toString() + ".");
    }

    // APP_LOGGER_DEBUG(this->baseAsset + " balance is " + Decimal(UtilString::printDoubleScientific(this->baseBalance)).toString() + ", " + this->quoteAsset +
    //                  " balance is " + Decimal(UtilString::printDoubleScientific(this->quoteBalance)).toString() + ".");
    auto eventType = event.getType();
    std::vector<Request> requestList;

    /*
    // stop loss/kill switch management
    if(this->isStopLossTriggered) {

      if (eventType == Event::Type::SUBSCRIPTION_DATA) {
        const auto& messageList = event.getMessageList();
        int index = -1;
        for (int i = 0; i < messageList.size(); ++i) {
          const auto& message = messageList.at(i);

          // retrieve message correlation id and filter out product id
          const auto& correlationIdList = message.getCorrelationIdList();

          auto correlationIdTocken = UtilString::split(correlationIdList.at(0), '#');
          std::string correlationId = correlationIdTocken.at(0);
          std::string productId = correlationIdTocken.at(1);

          if (message.getType() == Message::Type::EXECUTION_MANAGEMENT_EVENTS_PRIVATE_TRADE) {

            for (const auto& element : message.getElementList()) {

              // recieved the confirmation that the market order for liquidating product A in case of stop loss is recieved then cancel all remaining orders
              if(productId=="prodA") {
                const std::string& messageTimeISO = UtilTime::getISOTimestamp(tpnow());
                this->cancelAllOpenOrders(event, session, requestList, tpnow(), messageTimeISO, "prodA");
              }

              // recieved the confirmation that the market order for liquidating product A in case of stop loss is recieved then cancel all remaining orders
              if(productId=="prodB") {
                const std::string& messageTimeISO = UtilTime::getISOTimestamp(tpnow());
                this->cancelAllOpenOrders(event, session, requestList, tpnow(), messageTimeISO, "prodB");
              }

            }
          }
        }
      }
    }
    */

    // resync portfolio or check internal positions versus exchange positions
    // rem. only if stop loss not triggered...
    if (!this->isStopLossTriggered) this->processEventSyncPortfolio(event, session, requestList, tpnow());

    if (eventType == Event::Type::SUBSCRIPTION_DATA) {
      const auto& messageList = event.getMessageList();
      int index = -1;
      for (int i = 0; i < messageList.size(); ++i) {
        const auto& message = messageList.at(i);
        //
        // MARKET_DATA_EVENTS_MARKET_DEPTH will be treated at the end of the section thanks to index id...
        //
        // retrieve message correlation id and filter out product id
        const auto& correlationIdList = message.getCorrelationIdList();

        auto correlationIdTocken = UtilString::split(correlationIdList.at(0), '#');
        std::string correlationId = correlationIdTocken.at(0);
        std::string productId = correlationIdTocken.at(1);

        if (message.getType() == Message::Type::MARKET_DATA_EVENTS_MARKET_DEPTH && message.getRecapType() == Message::RecapType::NONE) {
          index = i;

        } else if (message.getType() == Message::Type::EXECUTION_MANAGEMENT_EVENTS_PRIVATE_TRADE) {
          // for each new trade submitted (private) reset the timer for the market chaser...
          // counter=0;

          for (const auto& element : message.getElementList()) {
            double lastExecutedPrice = std::stod(element.getValue(CCAPI_EM_ORDER_LAST_EXECUTED_PRICE));
            double lastExecutedSize = std::stod(element.getValue(CCAPI_EM_ORDER_LAST_EXECUTED_SIZE));
            const auto& feeQuantityStr = element.getValue(CCAPI_EM_ORDER_FEE_QUANTITY);
            double feeQuantity = feeQuantityStr.empty() ? 0 : std::stod(feeQuantityStr);
            std::string feeAsset = element.getValue(CCAPI_EM_ORDER_FEE_ASSET);
            bool isMaker = element.getValue(CCAPI_IS_MAKER) == "1";

            std::string side = UtilString::toUpper(element.getValue(CCAPI_EM_ORDER_SIDE));

            // compute NUMBER OF CONTRACTS from trade confirmation, Only if we are dealing with derivative products
            // reality check in R
            // if dealing with derivative compute the number of contract
            // for some exchange the trade size for derivative in in USD unit (multiple of contract size), i.e. Deribit, for some others, i.e. OKEX its directly
            // in contracts...BEWARE if(productId=="prodA" && this->instrument_type_a == "INVERSE") {
            double nbContract = 0.0;
            if (productId == "prodA") {
              // for linear product -> lastExecutedSize == quote, orderQuantityIncrement_a == quote
              if (!this->isContractDenominated_a) {
                nbContract = std::round(lastExecutedSize / std::stod(this->orderQuantityIncrement_a));
              } else {
                nbContract = std::round(lastExecutedSize);
              }  // std::stod(AppUtil::roundInput(lastExecutedSize / std::stod(this->orderQuantityIncrement_a), "1", false));

              if (side == CCAPI_EM_ORDER_SIDE_BUY) {
                this->position_qty_a += nbContract;
              } else {
                this->position_qty_a -= nbContract;
              }
            }

            if (productId == "prodB") {
              if (!this->isContractDenominated_b) {
                nbContract = std::round(lastExecutedSize / std::stod(this->orderQuantityIncrement_b));
              } else {
                nbContract = std::round(lastExecutedSize);
              }  // std::stod(AppUtil::roundInput(lastExecutedSize / std::stod(this->orderQuantityIncrement_a), "1", false));

              if (side == CCAPI_EM_ORDER_SIDE_BUY) {
                this->position_qty_b += nbContract;
              } else {
                this->position_qty_b -= nbContract;
              }
            }

            //
            // LIVE
            //
            if (this->tradingMode == TradingMode::LIVE) {
              // keep track of balance only after liquidation
              if (!(this->isReinit || this->isTarget || this->isInitOrder || this->isReinitOrder) || (this->isLiquidated_a && this->isLiquidated_b)) {
                // if(!this->isReinit || this->isTarget || (this->isLiquidated_a && this->isLiquidated_b)) {

                if (productId == "prodA") {
                  // std::string side = element.getValue(CCAPI_EM_ORDER_SIDE);
                  if (side == CCAPI_EM_ORDER_SIDE_BUY) {
                    // BUY
                    if (this->instrument_type_a != "INVERSE") {
                      if (!this->isContractDenominated_a) {
                        this->baseBalance_a += lastExecutedSize;
                        this->quoteBalance_a -= lastExecutedPrice * lastExecutedSize;
                      } else {
                        this->baseBalance_a += lastExecutedSize * std::stod(this->orderQuantityIncrement_a);
                        this->quoteBalance_a -= lastExecutedPrice * lastExecutedSize * std::stod(this->orderQuantityIncrement_a);
                      }
                    } else {
                      if (!this->isContractDenominated_a) {
                        this->baseBalance_a += lastExecutedSize / lastExecutedPrice;
                        this->quoteBalance_a -= lastExecutedSize;
                      } else {
                        this->baseBalance_a += (lastExecutedSize * std::stod(this->orderQuantityIncrement_a)) / lastExecutedPrice;
                        this->quoteBalance_a -= lastExecutedSize * std::stod(this->orderQuantityIncrement_a);
                      }
                    }
                  } else {
                    // SELL
                    if (this->instrument_type_a != "INVERSE") {
                      if (!this->isContractDenominated_a) {
                        this->baseBalance_a -= lastExecutedSize;
                        this->quoteBalance_a += lastExecutedPrice * lastExecutedSize;
                      } else {
                        this->baseBalance_a -= lastExecutedSize * std::stod(this->orderQuantityIncrement_a);
                        this->quoteBalance_a += lastExecutedPrice * lastExecutedSize * std::stod(this->orderQuantityIncrement_a);
                      }
                    } else {
                      if (!this->isContractDenominated_a) {
                        this->baseBalance_a -= lastExecutedSize / lastExecutedPrice;
                        this->quoteBalance_a += lastExecutedSize;
                      } else {
                        this->baseBalance_a -= (lastExecutedSize * std::stod(this->orderQuantityIncrement_a)) / lastExecutedPrice;
                        this->quoteBalance_a += lastExecutedSize * std::stod(this->orderQuantityIncrement_a);
                      }
                    }
                  }
                } else {
                  // PROD B
                  if (side == CCAPI_EM_ORDER_SIDE_BUY) {
                    // BUY
                    if (this->instrument_type_b != "INVERSE") {
                      if (!this->isContractDenominated_b) {
                        this->baseBalance_b += lastExecutedSize;
                        this->quoteBalance_b -= lastExecutedPrice * lastExecutedSize;
                      } else {
                        this->baseBalance_b += lastExecutedSize * std::stod(this->orderQuantityIncrement_b);
                        this->quoteBalance_b -= lastExecutedPrice * lastExecutedSize * std::stod(this->orderQuantityIncrement_b);
                      }
                    } else {
                      if (!this->isContractDenominated_b) {
                        this->baseBalance_b += lastExecutedSize / lastExecutedPrice;
                        this->quoteBalance_b -= lastExecutedSize;
                      } else {
                        this->baseBalance_b += (lastExecutedSize * std::stod(this->orderQuantityIncrement_b)) / lastExecutedPrice;
                        this->quoteBalance_b -= lastExecutedSize * std::stod(this->orderQuantityIncrement_b);
                      }
                    }
                  } else {
                    // SELL
                    if (this->instrument_type_b != "INVERSE") {
                      if (!this->isContractDenominated_b) {
                        this->baseBalance_b -= lastExecutedSize;
                        this->quoteBalance_b += lastExecutedPrice * lastExecutedSize;
                      } else {
                        this->baseBalance_b -= lastExecutedSize * std::stod(this->orderQuantityIncrement_b);
                        this->quoteBalance_b += lastExecutedPrice * lastExecutedSize * std::stod(this->orderQuantityIncrement_b);
                      }
                    } else {
                      if (!this->isContractDenominated_b) {
                        this->baseBalance_b -= lastExecutedSize / lastExecutedPrice;
                        this->quoteBalance_b += lastExecutedSize;
                      } else {
                        this->baseBalance_b -= (lastExecutedSize * std::stod(this->orderQuantityIncrement_b)) / lastExecutedPrice;
                        this->quoteBalance_b += lastExecutedSize * std::stod(this->orderQuantityIncrement_b);
                      }
                    }
                  }
                }
                this->updateAccountBalancesByFee(feeAsset, feeQuantity, side, isMaker, productId);
              }

              /*
              // legacy implementation for SPOT
              //std::string side = element.getValue(CCAPI_EM_ORDER_SIDE);
              if (side == CCAPI_EM_ORDER_SIDE_BUY) {
                // seggregate between leg a and b
                if(productId=="prodA") {
                  this->baseBalance_a += lastExecutedSize;
                  this->quoteBalance_a -= lastExecutedPrice * lastExecutedSize;
                } else {
                  this->baseBalance_b += lastExecutedSize;
                  this->quoteBalance_b -= lastExecutedPrice * lastExecutedSize;
                }
              } else {
                if(productId=="prodA") {
                  this->baseBalance_a -= lastExecutedSize;
                  this->quoteBalance_a += lastExecutedPrice * lastExecutedSize;
                  } else {
                 this->baseBalance_b -= lastExecutedSize;
                 this->quoteBalance_b += lastExecutedPrice * lastExecutedSize;

                }
              }
              */
            }  // end LIVE

            // volume traded and fees retrieved from mkt information
            if (productId == "prodA") {
              if (this->instrument_type_a != "INVERSE") {
                // SPOT/LINEAR
                if (!this->isContractDenominated_a) {
                  this->privateTradeVolumeInBaseSum_a += lastExecutedSize;
                  this->privateTradeVolumeInQuoteSum_a += lastExecutedSize * lastExecutedPrice;
                } else {
                  this->privateTradeVolumeInBaseSum_a += lastExecutedSize * std::stod(this->orderQuantityIncrement_b);
                  this->privateTradeVolumeInQuoteSum_a += lastExecutedSize * lastExecutedPrice * std::stod(this->orderQuantityIncrement_b);
                }
              } else {
                // INVERSE
                if (!this->isContractDenominated_a) {
                  this->privateTradeVolumeInBaseSum_a += lastExecutedSize / lastExecutedPrice;
                  this->privateTradeVolumeInQuoteSum_a += lastExecutedSize;
                } else {
                  this->privateTradeVolumeInBaseSum_a += (lastExecutedSize * std::stod(this->orderQuantityIncrement_a)) / lastExecutedPrice;
                  this->privateTradeVolumeInQuoteSum_a += lastExecutedSize * std::stod(this->orderQuantityIncrement_a);
                }
              }

              if (UtilString::toLower(feeAsset) == UtilString::toLower(this->baseAsset_a)) {
                this->privateTradeFeeInBaseSum_a += feeQuantity;
                this->privateTradeFeeInQuoteSum_a += feeQuantity * lastExecutedPrice;

              } else if (UtilString::toLower(feeAsset) == UtilString::toLower(this->quoteAsset_a)) {
                this->privateTradeFeeInBaseSum_a += feeQuantity / lastExecutedPrice;
                this->privateTradeFeeInQuoteSum_a += feeQuantity;
              }

            } else {
              // prodB
              if (this->instrument_type_b != "INVERSE") {
                // SPOT/LINEAR
                if (!this->isContractDenominated_b) {
                  this->privateTradeVolumeInBaseSum_b += lastExecutedSize;
                  this->privateTradeVolumeInQuoteSum_b += lastExecutedSize * lastExecutedPrice;
                } else {
                  this->privateTradeVolumeInBaseSum_b += lastExecutedSize * std::stod(this->orderQuantityIncrement_b);
                  this->privateTradeVolumeInQuoteSum_b += lastExecutedSize * lastExecutedPrice * std::stod(this->orderQuantityIncrement_b);
                }
              } else {
                // INVERSE
                if (!this->isContractDenominated_b) {
                  this->privateTradeVolumeInBaseSum_b += lastExecutedSize / lastExecutedPrice;
                  this->privateTradeVolumeInQuoteSum_b += lastExecutedSize;
                } else {
                  this->privateTradeVolumeInBaseSum_b += (lastExecutedSize * std::stod(this->orderQuantityIncrement_b)) / lastExecutedPrice;
                  this->privateTradeVolumeInQuoteSum_b += lastExecutedSize * std::stod(this->orderQuantityIncrement_b);
                }
              }

              if (UtilString::toLower(feeAsset) == UtilString::toLower(this->baseAsset_b)) {
                this->privateTradeFeeInBaseSum_b += feeQuantity;
                this->privateTradeFeeInQuoteSum_b += feeQuantity * lastExecutedPrice;

              } else if (UtilString::toLower(feeAsset) == UtilString::toLower(this->quoteAsset_b)) {
                this->privateTradeFeeInBaseSum_b += feeQuantity / lastExecutedPrice;
                this->privateTradeFeeInQuoteSum_b += feeQuantity;
              }
            }
          }

          if (!this->privateDataOnlySaveFinalSummary && this->privateTradeCsvWriter && isCSVexport) {
            std::vector<std::vector<std::string>> rows;
            const std::string& messageTimeISO = UtilTime::getISOTimestamp(message.getTime());
            for (const auto& element : message.getElementList()) {
              std::vector<std::string> row = {
                  messageTimeISO,
                  productId,
                  element.getValue(CCAPI_TRADE_ID),
                  element.getValue(CCAPI_EM_ORDER_LAST_EXECUTED_PRICE),
                  element.getValue(CCAPI_EM_ORDER_LAST_EXECUTED_SIZE),
                  element.getValue(CCAPI_EM_ORDER_SIDE),
                  element.getValue(CCAPI_IS_MAKER),
                  element.getValue(CCAPI_EM_ORDER_ID),
                  element.getValue(CCAPI_EM_CLIENT_ORDER_ID),
                  element.getValue(CCAPI_EM_ORDER_FEE_QUANTITY),
                  element.getValue(CCAPI_EM_ORDER_FEE_ASSET),
                  std::to_string(this->position_qty_a),
                  std::to_string(this->position_qty_b),
              };

              APP_LOGGER_INFO("#####################################################################################");
              APP_LOGGER_INFO("#### Log within CSV Private trade details");
              APP_LOGGER_INFO("#### product id: " + productId);
              APP_LOGGER_INFO("#### side: " + element.getValue(CCAPI_EM_ORDER_SIDE));
              APP_LOGGER_INFO("#### quantity: " + element.getValue(CCAPI_EM_ORDER_LAST_EXECUTED_SIZE));
              APP_LOGGER_INFO("#### price: " + element.getValue(CCAPI_EM_ORDER_LAST_EXECUTED_PRICE));
              // APP_LOGGER_INFO("position: "+ productId=="prodA"?std::to_string(this->position_qty_a):std::to_string(this->position_qty_b));
              APP_LOGGER_INFO("#####################################################################################");

              rows.emplace_back(std::move(row));
            }
            this->privateTradeCsvWriter->writeRows(rows);
            this->privateTradeCsvWriter->flush();
          }

          // keep track of absolute/relative position based on true trade execution
          // this correspond to USD inventory for each legs
          // if(productId=="prodA") {
          //  this->real_position_a = 0;
          //} else {
          //}

          this->postProcessMessageMarketDataEventPrivateTrade(event, session, requestList, message);

        } else if (message.getType() == Message::Type::EXECUTION_MANAGEMENT_EVENTS_ORDER_UPDATE) {
          for (const auto& element : message.getElementList()) {
            auto quantity = element.getValue(CCAPI_EM_ORDER_QUANTITY);
            auto cumulativeFilledQuantity = element.getValue(CCAPI_EM_ORDER_CUMULATIVE_FILLED_QUANTITY);
            auto remainingQuantity = element.getValue(CCAPI_EM_ORDER_REMAINING_QUANTITY);
            std::string side = element.getValue(CCAPI_EM_ORDER_SIDE);

            bool filled = false;
            if (!quantity.empty() && !cumulativeFilledQuantity.empty()) {
              filled = Decimal(quantity).toString() == Decimal(cumulativeFilledQuantity).toString();
            } else if (!remainingQuantity.empty()) {
              filled = Decimal(remainingQuantity).toString() == "0";
            }
            if (filled) {
              APP_LOGGER_DEBUG("#####################################################################################");
              if (side == CCAPI_EM_ORDER_SIDE_BUY) {
                if (productId == "prodA") {
                  APP_LOGGER_DEBUG("#### BUY ORDER A FILLED!");
                  this->openBuyOrder_a = boost::none;
                  this->numOpenOrdersBuy_a = 0;
                } else {
                  APP_LOGGER_DEBUG("#### BUY ORDER B FILLED!");
                  this->openBuyOrder_b = boost::none;
                  this->numOpenOrdersBuy_b = 0;
                }
              } else {
                if (productId == "prodA") {
                  APP_LOGGER_DEBUG("#### SELL ORDER A FILLED!");
                  this->openSellOrder_a = boost::none;
                  this->numOpenOrdersSell_a = 0;
                } else {
                  APP_LOGGER_DEBUG("#### SELL ORDER B FILLED!");
                  this->openSellOrder_b = boost::none;
                  this->numOpenOrdersSell_b = 0;
                }
              }
              APP_LOGGER_DEBUG("#####################################################################################");
            }

            if (productId == "prodA" && this->numOpenOrdersBuy_a == 0) {
              APP_LOGGER_DEBUG("No open buy orders for leg a.");
            }
            if (productId == "prodA" && this->numOpenOrdersSell_a == 0) {
              APP_LOGGER_DEBUG("No open sell orders for leg a.");
            }
            if (productId == "prodB" && this->numOpenOrdersBuy_b == 0) {
              APP_LOGGER_DEBUG("No open buy orders for leg b.");
            }
            if (productId == "prodB" && this->numOpenOrdersSell_b == 0) {
              APP_LOGGER_DEBUG("No open sell orders for leg b.");
            }
            // SWL LOGIC TODO
            // if (this->immediatelyPlaceNewOrders) {
            //  const auto& messageTimeReceived = message.getTimeReceived();
            //  const auto& messageTimeReceivedISO = UtilTime::getISOTimestamp(messageTimeReceived);
            //  this->orderRefreshLastTime = messageTimeReceived;
            //  this->cancelOpenOrdersLastTime = messageTimeReceived;
            //}

            if (!this->privateDataOnlySaveFinalSummary && this->orderUpdateCsvWriter && isCSVexportAll) {
              std::vector<std::vector<std::string>> rows;
              const std::string& messageTimeISO = UtilTime::getISOTimestamp(message.getTime());
              // for (const auto& element : message.getElementList()) {
              std::vector<std::string> row = {
                  messageTimeISO,
                  productId,
                  element.getValue(CCAPI_EM_ORDER_ID),
                  element.getValue(CCAPI_EM_CLIENT_ORDER_ID),
                  element.getValue(CCAPI_EM_ORDER_SIDE),
                  element.getValue(CCAPI_EM_ORDER_LIMIT_PRICE),
                  element.getValue(CCAPI_EM_ORDER_QUANTITY),
                  element.getValue(CCAPI_EM_ORDER_REMAINING_QUANTITY),
                  element.getValue(CCAPI_EM_ORDER_CUMULATIVE_FILLED_QUANTITY),
                  element.getValue(CCAPI_EM_ORDER_CUMULATIVE_FILLED_PRICE_TIMES_QUANTITY),
                  element.getValue(CCAPI_EM_ORDER_STATUS),
              };
              rows.emplace_back(std::move(row));
              //}
              this->orderUpdateCsvWriter->writeRows(rows);
              this->orderUpdateCsvWriter->flush();
            }
          }

        } else if (message.getType() == Message::Type::MARKET_DATA_EVENTS_TRADE || message.getType() == Message::Type::MARKET_DATA_EVENTS_AGG_TRADE) {
          const auto& messageTime = message.getTime();
          if (message.getRecapType() == Message::RecapType::NONE) {
            //
            // PAPER OR BACKTEST
            //
            // leg a
            if ((this->tradingMode == TradingMode::PAPER || this->tradingMode == TradingMode::BACKTEST) && productId == "prodA") {
              for (const auto& element : message.getElementList()) {
                // filter out the DUST...
                // const auto& takerQuantity = Decimal(element.getValue(CCAPI_LAST_SIZE));
                // if(takerQuantity < Decimal(this->orderQuantityIncrement_a)) continue;

                bool isBuyerMaker = element.getValue(CCAPI_IS_BUYER_MAKER) == "1";
                const auto& takerPrice = Decimal(element.getValue(CCAPI_LAST_PRICE));
                Order order;
                if (isBuyerMaker && this->openBuyOrder_a) {
                  order = this->openBuyOrder_a.get();
                } else if (!isBuyerMaker && this->openSellOrder_a) {
                  order = this->openSellOrder_a.get();
                }

                if ((isBuyerMaker && this->openBuyOrder_a && takerPrice <= this->openBuyOrder_a.get().limitPrice) ||
                    (!isBuyerMaker && this->openSellOrder_a && takerPrice >= this->openSellOrder_a.get().limitPrice)) {
                  const auto& takerQuantity = Decimal(element.getValue(CCAPI_LAST_SIZE));
                  Decimal lastFilledQuantity;
                  // JFA -> DIFF TO REALITY
                  // if (false){//takerQuantity < order.remainingQuantity) {
                  if (takerQuantity < order.remainingQuantity) {
                    lastFilledQuantity = takerQuantity;
                    order.cumulativeFilledQuantity = order.cumulativeFilledQuantity.add(lastFilledQuantity);
                    order.remainingQuantity = order.remainingQuantity.subtract(lastFilledQuantity);
                    order.status = APP_EVENT_HANDLER_BASE_ORDER_STATUS_PARTIALLY_FILLED;
                    if (isBuyerMaker) {
                      this->openBuyOrder_a = order;
                    } else {
                      this->openSellOrder_a = order;
                    }
                  } else {
                    lastFilledQuantity = order.remainingQuantity;
                    order.cumulativeFilledQuantity = order.quantity;
                    order.remainingQuantity = Decimal("0");
                    order.status = APP_EVENT_HANDLER_BASE_ORDER_STATUS_FILLED;
                    if (isBuyerMaker) {
                      this->openBuyOrder_a = boost::none;
                      this->numOpenOrdersBuy_a = 0;
                    } else {
                      this->openSellOrder_a = boost::none;
                      this->numOpenOrdersSell_a = 0;
                    }
                  }

                  if (isBuyerMaker) {
                    if (this->instrument_type_a != "INVERSE") {
                      if (!this->isContractDenominated_a) {
                        this->baseBalance_a += lastFilledQuantity.toDouble();
                        this->quoteBalance_a -= order.limitPrice.toDouble() * lastFilledQuantity.toDouble();
                      } else {
                        this->baseBalance_a += lastFilledQuantity.toDouble() * std::stod(this->orderQuantityIncrement_a);
                        this->quoteBalance_a -= order.limitPrice.toDouble() * lastFilledQuantity.toDouble() * std::stod(this->orderQuantityIncrement_a);
                      }
                    } else {
                      if (!this->isContractDenominated_a) {
                        this->baseBalance_a += lastFilledQuantity.toDouble() / order.limitPrice.toDouble();
                        this->quoteBalance_a -= lastFilledQuantity.toDouble();
                      } else {
                        this->baseBalance_a += (lastFilledQuantity.toDouble() * std::stod(this->orderQuantityIncrement_a)) / order.limitPrice.toDouble();
                        this->quoteBalance_a -= lastFilledQuantity.toDouble() * std::stod(this->orderQuantityIncrement_a);
                      }
                    }
                  } else {
                    if (this->instrument_type_a != "INVERSE") {
                      if (!this->isContractDenominated_a) {
                        this->baseBalance_a -= lastFilledQuantity.toDouble();
                        this->quoteBalance_a += order.limitPrice.toDouble() * lastFilledQuantity.toDouble();
                      } else {
                        this->baseBalance_a -= lastFilledQuantity.toDouble() * std::stod(this->orderQuantityIncrement_a);
                        this->quoteBalance_a += order.limitPrice.toDouble() * lastFilledQuantity.toDouble() * std::stod(this->orderQuantityIncrement_a);
                      }
                    } else {
                      if (!this->isContractDenominated_a) {
                        this->baseBalance_a -= lastFilledQuantity.toDouble() / order.limitPrice.toDouble();
                        this->quoteBalance_a += lastFilledQuantity.toDouble();
                      } else {
                        this->baseBalance_a -= (lastFilledQuantity.toDouble() * std::stod(this->orderQuantityIncrement_a)) / order.limitPrice.toDouble();
                        this->quoteBalance_a += lastFilledQuantity.toDouble() * std::stod(this->orderQuantityIncrement_a);
                      }
                    }
                  }

                  // legacy
                  //  this->baseBalance_a += lastFilledQuantity.toDouble();
                  //  this->quoteBalance_a -= order.limitPrice.toDouble() * lastFilledQuantity.toDouble();
                  //} else {
                  //  this->baseBalance_a -= lastFilledQuantity.toDouble();
                  //  this->quoteBalance_a += order.limitPrice.toDouble() * lastFilledQuantity.toDouble();
                  //}

                  // double feeQuantity = 0;
                  // if ((isBuyerMaker && UtilString::toLower(this->makerBuyerFeeAsset_b) == UtilString::toLower(this->baseAsset_b)) ||
                  //     (!isBuyerMaker && UtilString::toLower(this->makerSellerFeeAsset_b) == UtilString::toLower(this->baseAsset_b))) {
                  //   feeQuantity = lastFilledQuantity.toDouble() * this->makerFee_b;
                  //   this->baseBalance_b -= feeQuantity;
                  // } else if ((isBuyerMaker && UtilString::toLower(this->makerBuyerFeeAsset_b) == UtilString::toLower(this->quoteAsset_b)) ||
                  //            (!isBuyerMaker && UtilString::toLower(this->makerSellerFeeAsset_b) == UtilString::toLower(this->quoteAsset_b))) {
                  //     feeQuantity = order.limitPrice.toDouble() * lastFilledQuantity.toDouble() * this->makerFee_b;
                  //   this->quoteBalance_b -= feeQuantity;
                  // }

                  double feeQuantity = 0;
                  if ((isBuyerMaker && UtilString::toLower(this->makerBuyerFeeAsset_a) == UtilString::toLower(this->baseAsset_a)) ||
                      (!isBuyerMaker && UtilString::toLower(this->makerSellerFeeAsset_a) == UtilString::toLower(this->baseAsset_a))) {
                    if (instrument_type_a != "INVERSE") {
                      if (!this->isContractDenominated_a) {
                        feeQuantity = lastFilledQuantity.toDouble() * this->makerFee_a;
                      } else {
                        feeQuantity = lastFilledQuantity.toDouble() * std::stod(this->orderQuantityIncrement_a) * this->makerFee_a;
                      }
                      // this->quoteBalance_a -= feeQuantity;
                      // this->baseBalance_a -= feeQuantity;
                    } else {
                      if (!this->isContractDenominated_a) {
                        feeQuantity = lastFilledQuantity.toDouble() / order.limitPrice.toDouble() * this->makerFee_a;
                      } else {
                        feeQuantity =
                            (lastFilledQuantity.toDouble() * std::stod(this->orderQuantityIncrement_a)) / order.limitPrice.toDouble() * this->makerFee_a;
                      }
                      // this->baseBalance_a -= feeQuantity;
                    }
                    // if inverse but for linear discounted to quote balance
                    // JF_DBG
                    this->baseBalance_a -= feeQuantity;
                  } else if ((isBuyerMaker && UtilString::toLower(this->makerBuyerFeeAsset_a) == UtilString::toLower(this->quoteAsset_a)) ||
                             (!isBuyerMaker && UtilString::toLower(this->makerSellerFeeAsset_a) == UtilString::toLower(this->quoteAsset_a))) {
                    if (instrument_type_a != "INVERSE") {
                      if (!this->isContractDenominated_a) {
                        feeQuantity = order.limitPrice.toDouble() * lastFilledQuantity.toDouble() * this->makerFee_a;
                      } else {
                        feeQuantity =
                            order.limitPrice.toDouble() * lastFilledQuantity.toDouble() * std::stod(this->orderQuantityIncrement_a) * this->makerFee_a;
                      }
                      // this->quoteBalance_a -= feeQuantity;
                    } else {
                      if (!this->isContractDenominated_a) {
                        feeQuantity = lastFilledQuantity.toDouble() * this->makerFee_a;
                      } else {
                        feeQuantity = lastFilledQuantity.toDouble() * std::stod(this->orderQuantityIncrement_a) * this->makerFee_a;
                      }
                      // this->quoteBalance_a -= feeQuantity;
                    }
                    // JF_DBG
                    this->quoteBalance_a -= feeQuantity;
                  }

                  Event virtualEvent;
                  virtualEvent.setType(Event::Type::SUBSCRIPTION_DATA);
                  Message messagePrivateTrade;
                  messagePrivateTrade.setType(Message::Type::EXECUTION_MANAGEMENT_EVENTS_PRIVATE_TRADE);
                  messagePrivateTrade.setTime(messageTime);
                  messagePrivateTrade.setTimeReceived(messageTime);
                  messagePrivateTrade.setCorrelationIdList({std::string(CCAPI_EM_PRIVATE_TRADE) + "#prodA"});
                  // messagePrivateTrade.setCorrelationIdList({PRIVATE_SUBSCRIPTION_DATA_CORRELATION_ID});
                  Element elementPrivateTrade;
                  elementPrivateTrade.insert(CCAPI_TRADE_ID, std::to_string(++this->virtualTradeId_b));
                  elementPrivateTrade.insert(CCAPI_EM_ORDER_LAST_EXECUTED_PRICE, order.limitPrice.toString());
                  elementPrivateTrade.insert(CCAPI_EM_ORDER_LAST_EXECUTED_SIZE, lastFilledQuantity.toString());
                  elementPrivateTrade.insert(CCAPI_EM_ORDER_SIDE, order.side);
                  elementPrivateTrade.insert(CCAPI_IS_MAKER, "1");
                  elementPrivateTrade.insert(CCAPI_EM_ORDER_ID, order.orderId);
                  elementPrivateTrade.insert(CCAPI_EM_CLIENT_ORDER_ID, order.clientOrderId);
                  elementPrivateTrade.insert(CCAPI_EM_ORDER_FEE_QUANTITY, Decimal(UtilString::printDoubleScientific(feeQuantity)).toString());
                  elementPrivateTrade.insert(CCAPI_EM_ORDER_FEE_ASSET, isBuyerMaker ? this->makerBuyerFeeAsset_a : this->makerSellerFeeAsset_a);
                  std::vector<Element> elementListPrivateTrade;
                  elementListPrivateTrade.emplace_back(std::move(elementPrivateTrade));
                  messagePrivateTrade.setElementList(elementListPrivateTrade);
                  Message messageOrderUpdate;
                  messageOrderUpdate.setType(Message::Type::EXECUTION_MANAGEMENT_EVENTS_ORDER_UPDATE);
                  messageOrderUpdate.setTime(messageTime);
                  messageOrderUpdate.setTimeReceived(messageTime);
                  messageOrderUpdate.setCorrelationIdList({std::string(CCAPI_EM_ORDER_UPDATE) + "#prodA"});
                  // messageOrderUpdate.setCorrelationIdList({PRIVATE_SUBSCRIPTION_DATA_CORRELATION_ID});
                  Element elementOrderUpdate;
                  // SWL rem missing : CCAPI_EM_ORDER_CUMULATIVE_FILLED_PRICE_TIMES_QUANTITY
                  elementOrderUpdate.insert(CCAPI_EM_ORDER_ID, order.orderId);
                  elementOrderUpdate.insert(CCAPI_EM_CLIENT_ORDER_ID, order.clientOrderId);
                  elementOrderUpdate.insert(CCAPI_EM_ORDER_SIDE, order.side);
                  elementOrderUpdate.insert(CCAPI_EM_ORDER_LIMIT_PRICE, order.limitPrice.toString());
                  elementOrderUpdate.insert(CCAPI_EM_ORDER_QUANTITY, order.quantity.toString());
                  elementOrderUpdate.insert(CCAPI_EM_ORDER_CUMULATIVE_FILLED_QUANTITY, order.cumulativeFilledQuantity.toString());
                  elementOrderUpdate.insert(CCAPI_EM_ORDER_REMAINING_QUANTITY, order.remainingQuantity.toString());
                  elementOrderUpdate.insert(CCAPI_EM_ORDER_STATUS, order.status);
                  std::vector<Element> elementListOrderUpdate;
                  elementListOrderUpdate.emplace_back(std::move(elementOrderUpdate));
                  messageOrderUpdate.setElementList(elementListOrderUpdate);
                  std::vector<Message> messageList;
                  messageList.emplace_back(std::move(messagePrivateTrade));
                  messageList.emplace_back(std::move(messageOrderUpdate));
                  virtualEvent.setMessageList(messageList);
                  // APP_LOGGER_DEBUG("Generated a virtual event for leg a: " + virtualEvent.toStringPretty());
                  this->processEvent(virtualEvent, session);
                }
              }
            }  // END BACKTEST OR PAPER MARKET_DATA_EVENTS_TRADE or MARKET_DATA_EVENTS_AGG_TRADE -> leg a

            // leg b
            if ((this->tradingMode == TradingMode::PAPER || this->tradingMode == TradingMode::BACKTEST) && productId == "prodB") {
              for (const auto& element : message.getElementList()) {
                // filter out the DUST...
                // const auto& takerQuantity = Decimal(element.getValue(CCAPI_LAST_SIZE));
                // if(takerQuantity < Decimal(this->orderQuantityIncrement_b)) continue;

                bool isBuyerMaker = element.getValue(CCAPI_IS_BUYER_MAKER) == "1";
                const auto& takerPrice = Decimal(element.getValue(CCAPI_LAST_PRICE));
                Order order;
                if (isBuyerMaker && this->openBuyOrder_b) {
                  order = this->openBuyOrder_b.get();
                } else if (!isBuyerMaker && this->openSellOrder_b) {
                  order = this->openSellOrder_b.get();
                }

                if ((isBuyerMaker && this->openBuyOrder_b && takerPrice <= this->openBuyOrder_b.get().limitPrice) ||
                    (!isBuyerMaker && this->openSellOrder_b && takerPrice >= this->openSellOrder_b.get().limitPrice)) {
                  const auto& takerQuantity = Decimal(element.getValue(CCAPI_LAST_SIZE));
                  Decimal lastFilledQuantity;

                  // JFA -> DIFF TO REALITY
                  // if (false){//takerQuantity < order.remainingQuantity) {
                  if (takerQuantity < order.remainingQuantity) {
                    lastFilledQuantity = takerQuantity;
                    order.cumulativeFilledQuantity = order.cumulativeFilledQuantity.add(lastFilledQuantity);
                    order.remainingQuantity = order.remainingQuantity.subtract(lastFilledQuantity);
                    order.status = APP_EVENT_HANDLER_BASE_ORDER_STATUS_PARTIALLY_FILLED;
                    if (isBuyerMaker) {
                      this->openBuyOrder_b = order;
                    } else {
                      this->openSellOrder_b = order;
                    }
                  } else {
                    lastFilledQuantity = order.remainingQuantity;
                    order.cumulativeFilledQuantity = order.quantity;
                    order.remainingQuantity = Decimal("0");
                    order.status = APP_EVENT_HANDLER_BASE_ORDER_STATUS_FILLED;
                    if (isBuyerMaker) {
                      this->openBuyOrder_b = boost::none;
                      this->numOpenOrdersBuy_b = 0;
                    } else {
                      this->openSellOrder_b = boost::none;
                      this->numOpenOrdersSell_b = 0;
                    }
                  }

                  if (isBuyerMaker) {
                    if (this->instrument_type_b != "INVERSE") {
                      if (!this->isContractDenominated_b) {
                        this->baseBalance_b += lastFilledQuantity.toDouble();
                        this->quoteBalance_b -= order.limitPrice.toDouble() * lastFilledQuantity.toDouble();
                      } else {
                        this->baseBalance_b += lastFilledQuantity.toDouble() * std::stod(this->orderQuantityIncrement_b);
                        this->quoteBalance_b -= order.limitPrice.toDouble() * lastFilledQuantity.toDouble() * std::stod(this->orderQuantityIncrement_b);
                      }
                    } else {
                      if (!this->isContractDenominated_b) {
                        this->baseBalance_b += lastFilledQuantity.toDouble() / order.limitPrice.toDouble();
                        this->quoteBalance_b -= lastFilledQuantity.toDouble();
                      } else {
                        this->baseBalance_b += (lastFilledQuantity.toDouble() * std::stod(this->orderQuantityIncrement_b)) / order.limitPrice.toDouble();
                        this->quoteBalance_b -= lastFilledQuantity.toDouble() * std::stod(this->orderQuantityIncrement_b);
                      }
                    }
                  } else {
                    if (this->instrument_type_b != "INVERSE") {
                      if (!this->isContractDenominated_b) {
                        this->baseBalance_b -= lastFilledQuantity.toDouble();
                        this->quoteBalance_b += order.limitPrice.toDouble() * lastFilledQuantity.toDouble();
                      } else {
                        this->baseBalance_b -= lastFilledQuantity.toDouble() * std::stod(this->orderQuantityIncrement_b);
                        this->quoteBalance_b += order.limitPrice.toDouble() * lastFilledQuantity.toDouble() * std::stod(this->orderQuantityIncrement_b);
                      }
                    } else {
                      if (!this->isContractDenominated_b) {
                        this->baseBalance_b -= lastFilledQuantity.toDouble() / order.limitPrice.toDouble();
                        this->quoteBalance_b += lastFilledQuantity.toDouble();
                      } else {
                        this->baseBalance_b -= (lastFilledQuantity.toDouble() * std::stod(this->orderQuantityIncrement_b)) / order.limitPrice.toDouble();
                        this->quoteBalance_b += lastFilledQuantity.toDouble() * std::stod(this->orderQuantityIncrement_b);
                      }
                    }
                  }

                  // legacy
                  //  this->baseBalance_a += lastFilledQuantity.toDouble();
                  //  this->quoteBalance_a -= order.limitPrice.toDouble() * lastFilledQuantity.toDouble();
                  //} else {
                  //  this->baseBalance_a -= lastFilledQuantity.toDouble();
                  //  this->quoteBalance_a += order.limitPrice.toDouble() * lastFilledQuantity.toDouble();
                  //}

                  // double feeQuantity = 0;
                  // if ((isBuyerMaker && UtilString::toLower(this->makerBuyerFeeAsset_b) == UtilString::toLower(this->baseAsset_b)) ||
                  //     (!isBuyerMaker && UtilString::toLower(this->makerSellerFeeAsset_b) == UtilString::toLower(this->baseAsset_b))) {
                  //   feeQuantity = lastFilledQuantity.toDouble() * this->makerFee_b;
                  //   this->baseBalance_b -= feeQuantity;
                  // } else if ((isBuyerMaker && UtilString::toLower(this->makerBuyerFeeAsset_b) == UtilString::toLower(this->quoteAsset_b)) ||
                  //            (!isBuyerMaker && UtilString::toLower(this->makerSellerFeeAsset_b) == UtilString::toLower(this->quoteAsset_b))) {
                  //     feeQuantity = order.limitPrice.toDouble() * lastFilledQuantity.toDouble() * this->makerFee_b;
                  //   this->quoteBalance_b -= feeQuantity;
                  // }

                  double feeQuantity = 0;
                  if ((isBuyerMaker && UtilString::toLower(this->makerBuyerFeeAsset_b) == UtilString::toLower(this->baseAsset_b)) ||
                      (!isBuyerMaker && UtilString::toLower(this->makerSellerFeeAsset_b) == UtilString::toLower(this->baseAsset_b))) {
                    if (instrument_type_b != "INVERSE") {
                      if (!this->isContractDenominated_b) {
                        feeQuantity = lastFilledQuantity.toDouble() * this->makerFee_b;
                      } else {
                        feeQuantity = lastFilledQuantity.toDouble() * std::stod(this->orderQuantityIncrement_b) * this->makerFee_b;
                      }
                    } else {
                      if (!this->isContractDenominated_b) {
                        feeQuantity = lastFilledQuantity.toDouble() / order.limitPrice.toDouble() * this->makerFee_b;
                      } else {
                        feeQuantity =
                            (lastFilledQuantity.toDouble() * std::stod(this->orderQuantityIncrement_b)) / order.limitPrice.toDouble() * this->makerFee_b;
                      }
                    }
                    // JF_DBG
                    this->baseBalance_b -= feeQuantity;
                  } else if ((isBuyerMaker && UtilString::toLower(this->makerBuyerFeeAsset_b) == UtilString::toLower(this->quoteAsset_b)) ||
                             (!isBuyerMaker && UtilString::toLower(this->makerSellerFeeAsset_b) == UtilString::toLower(this->quoteAsset_b))) {
                    if (instrument_type_b != "INVERSE") {
                      if (!this->isContractDenominated_b) {
                        feeQuantity = order.limitPrice.toDouble() * lastFilledQuantity.toDouble() * this->makerFee_b;
                      } else {
                        feeQuantity =
                            order.limitPrice.toDouble() * lastFilledQuantity.toDouble() * std::stod(this->orderQuantityIncrement_b) * this->makerFee_b;
                      }
                    } else {
                      if (!this->isContractDenominated_b) {
                        feeQuantity = lastFilledQuantity.toDouble() * this->makerFee_b;
                      } else {
                        feeQuantity = lastFilledQuantity.toDouble() * std::stod(this->orderQuantityIncrement_b) * this->makerFee_b;
                      }
                    }
                    // JF_DBG
                    this->quoteBalance_b -= feeQuantity;
                  }

                  Event virtualEvent;
                  virtualEvent.setType(Event::Type::SUBSCRIPTION_DATA);
                  Message messagePrivateTrade;
                  messagePrivateTrade.setType(Message::Type::EXECUTION_MANAGEMENT_EVENTS_PRIVATE_TRADE);
                  messagePrivateTrade.setTime(messageTime);
                  messagePrivateTrade.setTimeReceived(messageTime);
                  messagePrivateTrade.setCorrelationIdList({std::string(CCAPI_EM_PRIVATE_TRADE) + "#prodB"});
                  // messagePrivateTrade.setCorrelationIdList({PRIVATE_SUBSCRIPTION_DATA_CORRELATION_ID});
                  Element elementPrivateTrade;
                  elementPrivateTrade.insert(CCAPI_TRADE_ID, std::to_string(++this->virtualTradeId_b));
                  elementPrivateTrade.insert(CCAPI_EM_ORDER_LAST_EXECUTED_PRICE, order.limitPrice.toString());
                  elementPrivateTrade.insert(CCAPI_EM_ORDER_LAST_EXECUTED_SIZE, lastFilledQuantity.toString());
                  elementPrivateTrade.insert(CCAPI_EM_ORDER_SIDE, order.side);
                  elementPrivateTrade.insert(CCAPI_IS_MAKER, "1");
                  elementPrivateTrade.insert(CCAPI_EM_ORDER_ID, order.orderId);
                  elementPrivateTrade.insert(CCAPI_EM_CLIENT_ORDER_ID, order.clientOrderId);
                  elementPrivateTrade.insert(CCAPI_EM_ORDER_FEE_QUANTITY, Decimal(UtilString::printDoubleScientific(feeQuantity)).toString());
                  elementPrivateTrade.insert(CCAPI_EM_ORDER_FEE_ASSET, isBuyerMaker ? this->makerBuyerFeeAsset_b : this->makerSellerFeeAsset_b);
                  std::vector<Element> elementListPrivateTrade;
                  elementListPrivateTrade.emplace_back(std::move(elementPrivateTrade));
                  messagePrivateTrade.setElementList(elementListPrivateTrade);
                  Message messageOrderUpdate;
                  messageOrderUpdate.setType(Message::Type::EXECUTION_MANAGEMENT_EVENTS_ORDER_UPDATE);
                  messageOrderUpdate.setTime(messageTime);
                  messageOrderUpdate.setTimeReceived(messageTime);
                  messageOrderUpdate.setCorrelationIdList({std::string(CCAPI_EM_ORDER_UPDATE) + "#prodB"});
                  // messageOrderUpdate.setCorrelationIdList({PRIVATE_SUBSCRIPTION_DATA_CORRELATION_ID});
                  Element elementOrderUpdate;
                  // SWL rem missing : CCAPI_EM_ORDER_CUMULATIVE_FILLED_PRICE_TIMES_QUANTITY
                  elementOrderUpdate.insert(CCAPI_EM_ORDER_ID, order.orderId);
                  elementOrderUpdate.insert(CCAPI_EM_CLIENT_ORDER_ID, order.clientOrderId);
                  elementOrderUpdate.insert(CCAPI_EM_ORDER_SIDE, order.side);
                  elementOrderUpdate.insert(CCAPI_EM_ORDER_LIMIT_PRICE, order.limitPrice.toString());
                  elementOrderUpdate.insert(CCAPI_EM_ORDER_QUANTITY, order.quantity.toString());
                  elementOrderUpdate.insert(CCAPI_EM_ORDER_CUMULATIVE_FILLED_QUANTITY, order.cumulativeFilledQuantity.toString());
                  elementOrderUpdate.insert(CCAPI_EM_ORDER_REMAINING_QUANTITY, order.remainingQuantity.toString());
                  elementOrderUpdate.insert(CCAPI_EM_ORDER_STATUS, order.status);
                  std::vector<Element> elementListOrderUpdate;
                  elementListOrderUpdate.emplace_back(std::move(elementOrderUpdate));
                  messageOrderUpdate.setElementList(elementListOrderUpdate);
                  std::vector<Message> messageList;
                  messageList.emplace_back(std::move(messagePrivateTrade));
                  messageList.emplace_back(std::move(messageOrderUpdate));
                  virtualEvent.setMessageList(messageList);
                  // APP_LOGGER_DEBUG("Generated a virtual event for leg a: " + virtualEvent.toStringPretty());
                  this->processEvent(virtualEvent, session);
                }
              }
            }  // END BACKTEST OR PAPER MARKET_DATA_EVENTS_TRADE or MARKET_DATA_EVENTS_AGG_TRADE -> leg b

            // postProcessMessageMarketDataEventTrade(const Event& event, Session* session, std::vector<Request>& requestList, const Message& message, const
            // TimePoint& messageTime) this->postProcessMessageMarketDataEventTrade(event, session, requestList, message, messageTime);
          }
        }
      }  // END LOOP THROUGH SUBSCRIPTION DATA MESSAGE BUT NEXT LINE CARRY ON Event::Type::SUBSCRIPTION_DATA -> MARKET_DATA_EVENTS_MARKET_DEPTH INDEX SEE :
         // MARKET_DATA_EVENTS_MARKET_DEPTH
      if (index != -1) {
        const auto& message = messageList.at(index);
        const auto& messageTime = message.getTime();
        const std::string& messageTimeISO = UtilTime::getISOTimestamp(messageTime);

        const auto& messageTimeReceived = message.getTimeReceived();
        const std::string& messageTimeReceivedISO = UtilTime::getISOTimestamp(messageTimeReceived);

        // retrieve message correlation id and filter out product id
        const auto& correlationIdList = message.getCorrelationIdList();

        auto correlationIdTocken = UtilString::split(correlationIdList.at(0), '#');
        std::string correlationId = correlationIdTocken.at(0);
        std::string productId = correlationIdTocken.at(1);

        // legacy implementation to setup a start time -> currently not used
        if (messageTime < this->startTimeTp) {
          return true;
        }

        productId == "prodA" ? (this->snapshotBid_a.clear()) : (this->snapshotBid_b.clear());
        productId == "prodA" ? (this->snapshotAsk_a.clear()) : (this->snapshotAsk_b.clear());

        // legacy implementation
        // this->snapshotBid.clear();
        // this->snapshotAsk.clear();
        for (const auto& element : message.getElementList()) {
          const auto& elementNameValueMap = element.getNameValueMap();
          {
            auto it = elementNameValueMap.find(CCAPI_BEST_BID_N_PRICE);
            if (it != elementNameValueMap.end()) {
              auto price = it->second;
              if (price != CCAPI_BEST_BID_N_PRICE_EMPTY) {
                if (productId == "prodA") {
                  this->snapshotBid_a[Decimal(price)] = elementNameValueMap.at(CCAPI_BEST_BID_N_SIZE);
                } else {
                  this->snapshotBid_b[Decimal(price)] = elementNameValueMap.at(CCAPI_BEST_BID_N_SIZE);
                }
                // legacy implementation
                // this->snapshotBid[Decimal(price)] = elementNameValueMap.at(CCAPI_BEST_BID_N_SIZE);
              }
            }
          }
          {
            auto it = elementNameValueMap.find(CCAPI_BEST_ASK_N_PRICE);
            if (it != elementNameValueMap.end()) {
              auto price = it->second;
              if (price != CCAPI_BEST_ASK_N_PRICE_EMPTY) {
                if (productId == "prodA") {
                  this->snapshotAsk_a[Decimal(price)] = elementNameValueMap.at(CCAPI_BEST_ASK_N_SIZE);
                } else {
                  this->snapshotAsk_b[Decimal(price)] = elementNameValueMap.at(CCAPI_BEST_ASK_N_SIZE);
                }
                // legacy implementation
                // this->snapshotAsk[Decimal(price)] = elementNameValueMap.at(CCAPI_BEST_ASK_N_SIZE);
              }
            }
          }
          if (productId == "prodA") {
            if (this->snapshotBid_a.empty()) {
              this->bestBidPrice_a = "";
              this->bestBidSize_a = "";
            } else {
              auto it = this->snapshotBid_a.rbegin();
              this->bestBidPrice_a = it->first.toString();
              this->bestBidSize_a = it->second;
            }
          } else {
            if (this->snapshotBid_b.empty()) {
              this->bestBidPrice_b = "";
              this->bestBidSize_b = "";
            } else {
              auto it = this->snapshotBid_b.rbegin();
              this->bestBidPrice_b = it->first.toString();
              this->bestBidSize_b = it->second;
            }
          }
          if (productId == "prodA") {
            if (this->snapshotAsk_a.empty()) {
              this->bestAskPrice_a = "";
              this->bestAskSize_a = "";
            } else {
              auto it = this->snapshotAsk_a.begin();
              this->bestAskPrice_a = it->first.toString();
              this->bestAskSize_a = it->second;
            }
          } else {
            if (this->snapshotAsk_b.empty()) {
              this->bestAskPrice_b = "";
              this->bestAskSize_b = "";
            } else {
              auto it = this->snapshotAsk_b.begin();
              this->bestAskPrice_b = it->first.toString();
              this->bestAskSize_b = it->second;
            }
          }

          // Compute mid-prices
          if (!this->bestBidPrice_a.empty() && !this->bestAskPrice_a.empty() && !this->bestBidPrice_b.empty() && !this->bestAskPrice_b.empty()) {
            this->midPriceA = (std::stod(this->bestBidPrice_a) + std::stod(this->bestAskPrice_a)) / 2;
            this->midPriceB = (std::stod(this->bestBidPrice_b) + std::stod(this->bestAskPrice_b)) / 2;
          } else {
            this->midPriceA = 0.0;
            this->midPriceB = 0.0;
          }

          // SWL: in case one wants to compute the mid-price -> legacy
          // if (!this->bestBidPrice.empty() && !this->bestAskPrice.empty()) {
          //  if (this->useWeightedMidPrice) {
          //    this->midPrice = (std::stod(this->bestBidPrice) * std::stod(this->bestAskSize) + std::stod(this->bestAskPrice) * std::stod(this->bestBidSize)) /
          //                     (std::stod(this->bestBidSize) + std::stod(this->bestAskSize));
          //  } else {
          //    this->midPrice = (std::stod(this->bestBidPrice) + std::stod(this->bestAskPrice)) / 2;
          //  }
          //} else {
          //  this->midPrice = 0;
          //}

          //}
        }  // loop through all elements of the message (i.e. mkt depth)

        //
        // TRADING MODE PAPER OR BACKTEST
        //
        // IF OWN QUOTE CROSSED A MKT QUOTE (I.E. CROSSED THE SPREAD) -> SEND A VIRTUAL EVENT
        // SIMULATE MAKER ORDER
        if (this->tradingMode == TradingMode::PAPER || this->tradingMode == TradingMode::BACKTEST) {
          bool buySideCrossed_a =
              !this->bestAskPrice_a.empty() && this->openBuyOrder_a && Decimal(this->bestAskPrice_a) <= this->openBuyOrder_a.get().limitPrice;
          bool buySideCrossed_b =
              !this->bestAskPrice_b.empty() && this->openBuyOrder_b && Decimal(this->bestAskPrice_b) <= this->openBuyOrder_b.get().limitPrice;

          bool sellSideCrossed_a =
              !this->bestBidPrice_a.empty() && this->openSellOrder_a && Decimal(this->bestBidPrice_a) >= this->openSellOrder_a.get().limitPrice;
          bool sellSideCrossed_b =
              !this->bestBidPrice_b.empty() && this->openSellOrder_b && Decimal(this->bestBidPrice_b) >= this->openSellOrder_b.get().limitPrice;

          // leg a
          if ((buySideCrossed_a || sellSideCrossed_a) && this->marketImpfactFactor_a > 0) {
            APP_LOGGER_DEBUG("#####################################################################################");
            APP_LOGGER_DEBUG("### Crossed the spread for a");
            const Order& order = buySideCrossed_a ? this->openBuyOrder_a.get() : this->openSellOrder_a.get();

            // decide if we want to take into account mkt impact
            // std::string filledAmount_std = AppUtil::roundInput(order.remainingQuantity.toDouble() * this->marketImpfactFactor_a,
            // this->orderQuantityIncrement_a, false);
            std::string filledAmount_std{};
            if (!this->isContractDenominated_a) {
              filledAmount_std = AppUtil::roundInput(order.remainingQuantity.toDouble(), this->orderQuantityIncrement_a, false);  // JFA -> DIFF TO REALITY
            } else {
              filledAmount_std = AppUtil::roundInput(order.remainingQuantity.toDouble(), this->orderPriceIncrement_a, false);  // JFA -> DIFF TO REALITY
              // filledAmount_std = AppUtil::roundInput(order.remainingQuantity.toDouble(), "1.0", false);
            }

            APP_LOGGER_DEBUG("### Crossed the spread for a -> filled quantity: " + filledAmount_std);

            if (abs(std::stod(filledAmount_std)) > 0) {
              Event virtualEvent;
              virtualEvent.setType(Event::Type::SUBSCRIPTION_DATA);
              Message message;
              message.setType(exchange.rfind("binance", 0) == 0 ? Message::Type::MARKET_DATA_EVENTS_AGG_TRADE : Message::Type::MARKET_DATA_EVENTS_TRADE);
              message.setRecapType(Message::RecapType::NONE);
              message.setTime(messageTime);
              message.setTimeReceived(messageTime);

              message.setCorrelationIdList({std::string(PUBLIC_SUBSCRIPTION_DATA_TRADE_CORRELATION_ID) + "#prodA"});
              // legacy implementation of the correlation id
              // message.setCorrelationIdList({PUBLIC_SUBSCRIPTION_DATA_TRADE_CORRELATION_ID});
              std::vector<Element> elementList;
              Element element;

              element.insert(CCAPI_LAST_PRICE, order.limitPrice.toString());

              // JFA -> DIFF TO REALITY
              element.insert(CCAPI_LAST_SIZE, Decimal(UtilString::printDoubleScientific(std::stod(filledAmount_std))).toString());

              // element.insert(CCAPI_LAST_SIZE,
              //               Decimal(UtilString::printDoubleScientific(order.remainingQuantity.toDouble() * this->marketImpfactFactor_a)).toString());
              element.insert(CCAPI_IS_BUYER_MAKER, buySideCrossed_a ? "1" : "0");
              elementList.emplace_back(std::move(element));
              message.setElementList(elementList);
              virtualEvent.addMessage(message);
              // APP_LOGGER_DEBUG("Generated a virtual event for leg a: " + virtualEvent.toStringPretty());
              this->processEvent(virtualEvent, session);
            }
          }  // end leg a

          // leg b
          if ((buySideCrossed_b || sellSideCrossed_b) && this->marketImpfactFactor_b > 0) {
            APP_LOGGER_DEBUG("#####################################################################################");
            APP_LOGGER_DEBUG("### Crossed the spread for b");
            const Order& order = buySideCrossed_b ? this->openBuyOrder_b.get() : this->openSellOrder_b.get();

            // JFA -> DIFF TO REALITY
            // std::string filledAmount_std = AppUtil::roundInput(order.remainingQuantity.toDouble() * this->marketImpfactFactor_b,
            // this->orderQuantityIncrement_b, false);
            std::string filledAmount_std{};
            if (!this->isContractDenominated_b) {
              filledAmount_std = AppUtil::roundInput(order.remainingQuantity.toDouble(), this->orderQuantityIncrement_b, false);  // JFA -> DIFF TO REALITY
            } else {
              filledAmount_std = AppUtil::roundInput(order.remainingQuantity.toDouble(), this->orderPriceIncrement_b, false);  // JFA -> DIFF TO REALITY
              // filledAmount_std = AppUtil::roundInput(order.remainingQuantity.toDouble(), "1.0", false); //JFA -> DIFF TO REALITY
            }

            APP_LOGGER_DEBUG("### Crossed the spread for b -> filled quantity: " + filledAmount_std);

            if (abs(std::stod(filledAmount_std)) > 0) {
              Event virtualEvent;
              virtualEvent.setType(Event::Type::SUBSCRIPTION_DATA);
              Message message;
              message.setType(exchange.rfind("binance", 0) == 0 ? Message::Type::MARKET_DATA_EVENTS_AGG_TRADE : Message::Type::MARKET_DATA_EVENTS_TRADE);
              message.setRecapType(Message::RecapType::NONE);
              message.setTime(messageTime);
              message.setTimeReceived(messageTime);

              message.setCorrelationIdList({std::string(PUBLIC_SUBSCRIPTION_DATA_TRADE_CORRELATION_ID) + "#prodB"});
              // legacy implementation of the correlation id
              // message.setCorrelationIdList({PUBLIC_SUBSCRIPTION_DATA_TRADE_CORRELATION_ID});
              std::vector<Element> elementList;
              Element element;

              element.insert(CCAPI_LAST_PRICE, order.limitPrice.toString());

              // JFA -> DIFF TO REALITY
              element.insert(CCAPI_LAST_SIZE, Decimal(UtilString::printDoubleScientific(std::stod(filledAmount_std))).toString());

              // element.insert(CCAPI_LAST_SIZE,
              //               Decimal(UtilString::printDoubleScientific(order.remainingQuantity.toDouble() * this->marketImpfactFactor_b)).toString());
              element.insert(CCAPI_IS_BUYER_MAKER, buySideCrossed_b ? "1" : "0");
              elementList.emplace_back(std::move(element));
              message.setElementList(elementList);
              virtualEvent.addMessage(message);
              // APP_LOGGER_DEBUG("Generated a virtual event for leg a: " + virtualEvent.toStringPretty());
              this->processEvent(virtualEvent, session);
            }
          }  // end leg b

          /*
          // leg b
          if ((buySideCrossed_b || sellSideCrossed_b) && this->marketImpfactFactor_b > 0) {
            Event virtualEvent;
            virtualEvent.setType(Event::Type::SUBSCRIPTION_DATA);
            Message message;
            message.setType(exchange.rfind("binance", 0) == 0 ? Message::Type::MARKET_DATA_EVENTS_AGG_TRADE : Message::Type::MARKET_DATA_EVENTS_TRADE);
            message.setRecapType(Message::RecapType::NONE);
            message.setTime(messageTime);
            message.setTimeReceived(messageTime);

            message.setCorrelationIdList({std::string(PUBLIC_SUBSCRIPTION_DATA_TRADE_CORRELATION_ID)+"#prodB"});
            // legacy implementation of the correlation id
            //message.setCorrelationIdList({PUBLIC_SUBSCRIPTION_DATA_TRADE_CORRELATION_ID});
            std::vector<Element> elementList;
            Element element;
            const Order& order = buySideCrossed_b ? this->openBuyOrder_b.get() : this->openSellOrder_b.get();
            element.insert(CCAPI_LAST_PRICE, order.limitPrice.toString());
            element.insert(CCAPI_LAST_SIZE,
                           Decimal(UtilString::printDoubleScientific(order.remainingQuantity.toDouble() * this->marketImpfactFactor_b)).toString());
            element.insert(CCAPI_IS_BUYER_MAKER, buySideCrossed_b ? "1" : "0");
            elementList.emplace_back(std::move(element));
            message.setElementList(elementList);
            virtualEvent.addMessage(message);
            //APP_LOGGER_DEBUG("Generated a virtual event for leg b: " + virtualEvent.toStringPretty());
            this->processEvent(virtualEvent, session);
          } // end leg b
          */
        }  // END BACKTEST OR PAPER

        // WRITE RESULTS TO FILE
        // SWL remark: we don't seggregate time between message as of now

        // legacy
        // const std::string& messageTimeISO = UtilTime::getISOTimestamp(messageTime);

        const std::string& messageTimeISODate = messageTimeISO.substr(0, 10);
        // PERFORM ONLY FOR THE FIRST SUBSCRIBE MESSAGE OR NEW DAY (SEE LINE ABOVE)

        if (isCSVexport) {
          if (this->previousMessageTimeISODate.empty() || messageTimeISODate != previousMessageTimeISODate) {
            // prevent reopening file multiple times the same day
            this->previousMessageTimeISODate = messageTimeISODate;

            std::string prefix;
            if (!this->privateDataFilePrefix.empty()) {
              prefix = this->privateDataFilePrefix;
            }
            std::string suffix;
            if (!this->privateDataFileSuffix.empty()) {
              suffix = this->privateDataFileSuffix;
            }

            std::string privateTradeCsvFilename(prefix + this->exchange_a + "__" + this->exchange_b + "__" + UtilString::toLower(this->baseAsset_a) + "-" +
                                                UtilString::toLower(this->quoteAsset_a) + "__" + UtilString::toLower(this->baseAsset_b) + "-" +
                                                UtilString::toLower(this->quoteAsset_b) + "__" + messageTimeISODate + "__private-trade" + suffix + "__" +
                                                this->trader_id + ".csv"),
                orderUpdateCsvFilename(prefix + this->exchange_a + "__" + this->exchange_b + "__" + UtilString::toLower(this->baseAsset_a) + "-" +
                                       UtilString::toLower(this->quoteAsset_a) + "__" + UtilString::toLower(this->baseAsset_b) + "-" +
                                       UtilString::toLower(this->quoteAsset_b) + "__" + messageTimeISODate + "__order-update" + suffix + "__" +
                                       this->trader_id + ".csv"),
                accountBalanceCsvFilename(prefix + this->exchange_a + "__" + this->exchange_b + "__" + UtilString::toLower(this->baseAsset_a) + "-" +
                                          UtilString::toLower(this->quoteAsset_a) + "__" + UtilString::toLower(this->baseAsset_b) + "-" +
                                          UtilString::toLower(this->quoteAsset_b) + "__" + messageTimeISODate + "__account-balance" + suffix + "__" +
                                          this->trader_id + ".csv"),
                positionCsvFilename(prefix + this->exchange_a + "__" + this->exchange_b + "__" + UtilString::toLower(this->baseAsset_a) + "-" +
                                    UtilString::toLower(this->quoteAsset_a) + "__" + UtilString::toLower(this->baseAsset_b) + "-" +
                                    UtilString::toLower(this->quoteAsset_b) + "__" + messageTimeISODate + "__position" + suffix + "__" + this->trader_id +
                                    ".csv");

            // legacy implementation of the file naming convention
            // std::string privateTradeCsvFilename(prefix + this->exchange + "__" + UtilString::toLower(this->baseAsset) + "-" +
            //                                    UtilString::toLower(this->quoteAsset) + "__" + messageTimeISODate + "__private-trade" + suffix + ".csv"),
            //    orderUpdateCsvFilename(prefix + this->exchange + "__" + UtilString::toLower(this->baseAsset) + "-" + UtilString::toLower(this->quoteAsset) +
            //                           "__" + messageTimeISODate + "__order-update" + suffix + ".csv"),
            //    accountBalanceCsvFilename(prefix + this->exchange + "__" + UtilString::toLower(this->baseAsset) + "-" + UtilString::toLower(this->quoteAsset)
            //    +
            //                              "__" + messageTimeISODate + "__account-balance" + suffix + ".csv");
            if (!this->privateDataDirectory.empty()) {
              // std::filesystem::create_directory(std::filesystem::path(this->privateDataDirectory.c_str()));
              privateTradeCsvFilename = this->privateDataDirectory + "/" + privateTradeCsvFilename;
              orderUpdateCsvFilename = this->privateDataDirectory + "/" + orderUpdateCsvFilename;
              accountBalanceCsvFilename = this->privateDataDirectory + "/" + accountBalanceCsvFilename;
              positionCsvFilename = this->privateDataDirectory + "/" + positionCsvFilename;
            }
            CsvWriter* privateTradeCsvWriter = nullptr;
            CsvWriter* orderUpdateCsvWriter = nullptr;
            CsvWriter* accountBalanceCsvWriter = nullptr;
            CsvWriter* positionCsvWriter = nullptr;
            if (!privateDataOnlySaveFinalSummary) {
              privateTradeCsvWriter = new CsvWriter();
              {
                struct stat buffer;
                if (stat(privateTradeCsvFilename.c_str(), &buffer) != 0) {
                  privateTradeCsvWriter->open(privateTradeCsvFilename, std::ios_base::app);
                  privateTradeCsvWriter->writeRow({
                      "TIME",
                      "PRODUCT_ID",
                      "TRADE_ID",
                      "LAST_EXECUTED_PRICE",
                      "LAST_EXECUTED_SIZE",
                      "SIDE",
                      "IS_MAKER",
                      "ORDER_ID",
                      "CLIENT_ORDER_ID",
                      "FEE_QUANTITY",
                      "FEE_ASSET",
                      "POSITION_QTY_A",
                      "POSITION_QTY_B",
                  });
                  privateTradeCsvWriter->flush();
                } else {
                  privateTradeCsvWriter->open(privateTradeCsvFilename, std::ios_base::app);
                }
              }
              orderUpdateCsvWriter = new CsvWriter();
              {
                struct stat buffer;
                if (stat(orderUpdateCsvFilename.c_str(), &buffer) != 0) {
                  orderUpdateCsvWriter->open(orderUpdateCsvFilename, std::ios_base::app);
                  orderUpdateCsvWriter->writeRow({
                      "TIME",
                      "PRODUCT_ID",
                      "ORDER_ID",
                      "CLIENT_ORDER_ID",
                      "SIDE",
                      "LIMIT_PRICE",
                      "QUANTITY",
                      "REMAINING_QUANTITY",
                      "CUMULATIVE_FILLED_QUANTITY",
                      "CUMULATIVE_FILLED_PRICE_TIMES_QUANTITY",
                      "STATUS",
                  });
                  orderUpdateCsvWriter->flush();
                } else {
                  orderUpdateCsvWriter->open(orderUpdateCsvFilename, std::ios_base::app);
                }
              }
            }
            if (!this->privateDataOnlySaveFinalSummary) {
              accountBalanceCsvWriter = new CsvWriter();
              {
                struct stat buffer;
                if (stat(accountBalanceCsvFilename.c_str(), &buffer) != 0) {
                  accountBalanceCsvWriter->open(accountBalanceCsvFilename, std::ios_base::app);
                  accountBalanceCsvWriter->writeRow({
                      "TIME",
                      "PRODUCT_ID",
                      "BASE_AVAILABLE_BALANCE",
                      "QUOTE_AVAILABLE_BALANCE",
                      "BEST_BID_PRICE",
                      "BEST_ASK_PRICE",
                  });
                  accountBalanceCsvWriter->flush();
                } else {
                  accountBalanceCsvWriter->open(accountBalanceCsvFilename, std::ios_base::app);
                }
              }
            }
            if (!this->privateDataOnlySaveFinalSummary) {
              positionCsvWriter = new CsvWriter();
              {
                struct stat buffer;
                if (stat(positionCsvFilename.c_str(), &buffer) != 0) {
                  positionCsvWriter->open(positionCsvFilename, std::ios_base::app);
                  positionCsvWriter->writeRow({
                      "TIME",
                      "PRODUCT_ID",
                      "POSITION",
                      "BEST_BID_PRICE",
                      "BEST_ASK_PRICE",
                  });
                  positionCsvWriter->flush();
                } else {
                  positionCsvWriter->open(accountBalanceCsvFilename, std::ios_base::app);
                }
              }
            }
            if (this->privateTradeCsvWriter) {
              delete this->privateTradeCsvWriter;
            }
            this->privateTradeCsvWriter = privateTradeCsvWriter;

            if (this->orderUpdateCsvWriter) {
              delete this->orderUpdateCsvWriter;
            }
            this->orderUpdateCsvWriter = orderUpdateCsvWriter;

            if (this->accountBalanceCsvWriter) {
              delete this->accountBalanceCsvWriter;
            }
            this->accountBalanceCsvWriter = accountBalanceCsvWriter;

            if (this->positionCsvWriter) {
              delete this->positionCsvWriter;
            }
            this->positionCsvWriter = positionCsvWriter;
          }  // END INITIALIZE FILE
        }

        // IF NEW MESSAGE THE SAME DAY -> legacy logic
        /*
        this->previousMessageTimeISODate = messageTimeISODate;
        if ((this->orderRefreshIntervalOffsetSeconds == -1 &&
             std::chrono::duration_cast<std::chrono::seconds>(messageTime - this->orderRefreshLastTime).count() >= this->orderRefreshIntervalSeconds) ||
            (this->orderRefreshIntervalOffsetSeconds >= 0 &&
             std::chrono::duration_cast<std::chrono::seconds>(messageTime.time_since_epoch()).count() % this->orderRefreshIntervalSeconds ==
                 this->orderRefreshIntervalOffsetSeconds)) {

          // in case one want to refresh orders at recurrent interval
          APP_LOGGER_INFO("---------------------------------------------------------------> cancelling all orders");
          this->cancelOpenOrders(event, session, requestList, messageTime, messageTimeISO, false, "prodA");
          this->cancelOpenOrders(event, session, requestList, messageTime, messageTimeISO, false, "prodB");
        } else if (std::chrono::duration_cast<std::chrono::seconds>(messageTime - this->cancelOpenOrdersLastTime).count() >=
                       this->accountBalanceRefreshWaitSeconds &&
                   this->getAccountBalancesLastTime < this->cancelOpenOrdersLastTime &&
                   this->cancelOpenOrdersLastTime + std::chrono::seconds(this->accountBalanceRefreshWaitSeconds) >= this->orderRefreshLastTime) {

          // in case one want to fetch account balance at recurrent interval
          APP_LOGGER_INFO("---------------------------------------------------------------> get account balance for the two legs");
          this->getAccountBalances(event, session, requestList, messageTime, messageTimeISO, "prodA");
          this->getAccountBalances(event, session, requestList, messageTime, messageTimeISO, "prodB");
        }
        */

        // introduce multi-account flag action execution logic - here as we don't need to retrieve irrelevent position information in the case of multiple
        // accounts in case we are running a backtest we should ensure that we recieved a list one bid-ask quote for the two products...

        // in live no need to have mkt quotes as we send market orders
        bool isAllQuotesAvailable = this->tradingMode == TradingMode::BACKTEST ? (!this->bestBidPrice_a.empty() && !this->bestAskPrice_a.empty() &&
                                                                                  !this->bestBidPrice_b.empty() && !this->bestAskPrice_b.empty())
                                                                               : true;

        // INITORDER OR LIQUIDATION ORDER OR REINITORDER
        if (this->isInitOrder && this->isAllSubscriptionStarted && isAllQuotesAvailable) {
          if (!this->isLiquidationOrderSent_a) {
            APP_LOGGER_INFO("#############################################################");
            APP_LOGGER_INFO("##### Send initial order for leg a.");
            APP_LOGGER_INFO("#############################################################");

            double amountTaker_a = this->initOrderSize_a;

            APP_LOGGER_INFO("##### Initial Order size in USD for leg a: " + std::to_string(amountTaker_a));
            APP_LOGGER_INFO("#############################################################");

            if (amountTaker_a == 0.0) {
              // don't liquidate several times
              this->isLiquidationOrderSent_a = true;
              this->isLiquidated_a = true;  // consistency the private trade quote/base calculation
            }
            // Rem as we send market orders we don't need to send the level -> set to 0.0
            if (amountTaker_a != 0.0 && !this->isLiquidationOrderSent_a) {
              // don't liquidate several times
              this->isLiquidationOrderSent_a = true;

              // set liquidation qty
              this->remainingLiquidationQty_a = amountTaker_a;

              /*
              if (!this->isContractDenominated_a) {
                // quoteAmount_std = AppUtil::roundInput(quoteAmount, this->orderQuantityIncrement_a, false);
                this->remainingLiquidationQty_a =
                    std::to_string(round(amountTaker_a / std::stod(this->orderQuantityIncrement_a)) * std::stod(this->orderQuantityIncrement_a));
              } else {
                // quoteAmount_std = std::to_string(std::stod(AppUtil::roundInput(quoteAmount, this->orderQuantityIncrement_a,
                // false))/std::stod(this->orderQuantityIncrement_a));
                this->remainingLiquidationQty_a = std::to_string(round(amountTaker_a / std::stod(this->orderQuantityIncrement_a)));
              }
              */

              double aSellLevelTakerA = 0.0;
              double aBuyLevelTakerA = 0.0;
              // in production at that point we possibly didn't recieve bid-ask quote -> not a pb as we send a mkt order
              if (this->tradingMode == TradingMode::BACKTEST) {
                // send a sell mkt order -> taker
                aSellLevelTakerA = std::stod(this->bestBidPrice_a) -
                                   this->offset * abs(std::stod(this->bestBidPrice_a) -
                                                      std::stod(this->bestAskPrice_a));  // sell into the order book == mkt order : BEWARE OF THE DUST...

                // send a buy mkt order
                aBuyLevelTakerA = std::stod(this->bestAskPrice_a) +
                                  this->offset * abs(std::stod(this->bestBidPrice_a) -
                                                     std::stod(this->bestAskPrice_a));  // buy into the order book == mkt order : BEWARE OF THE DUST...
              }

              // double quoteLevel, double quoteAmount, int leg, int side)
              // swl quoter, i.e. leg==0 -> leg a, leg==1 -> leg b ## side==0 -> buy, side==1 -> sell
              APP_LOGGER_INFO("##### Send an initial order size A: " + std::to_string(amountTaker_a));
              this->placeQuote(event, session, requestList, tpnow(), amountTaker_a < 0 ? aSellLevelTakerA : aBuyLevelTakerA, abs(amountTaker_a), 0,
                               amountTaker_a > 0 ? 0 : 1, false);
            }
          }

          if (!this->isLiquidationOrderSent_b) {
            APP_LOGGER_INFO("#############################################################");
            APP_LOGGER_INFO("##### Send initial order for leg b.");
            APP_LOGGER_INFO("#############################################################");

            double amountTaker_b = this->initOrderSize_b;

            APP_LOGGER_INFO("##### Initial Order size in USD for leg b: " + std::to_string(amountTaker_b));
            APP_LOGGER_INFO("#############################################################");

            if (amountTaker_b == 0.0) {
              // don't liquidate several times
              this->isLiquidationOrderSent_b = true;
              this->isLiquidated_b = true;  // consistency the private trade quote/base calculation
            }
            // Rem as we send market orders we don't need to send the level -> set to 0.0
            if (amountTaker_b != 0.0 && !this->isLiquidationOrderSent_b) {
              // don't liquidate several times
              this->isLiquidationOrderSent_b = true;

              // set liquidation qty
              this->remainingLiquidationQty_b = amountTaker_b;

              /*
              if (!this->isContractDenominated_b) {
                // quoteAmount_std = AppUtil::roundInput(quoteAmount, this->orderQuantityIncrement_b, false);
                this->remainingLiquidationQty_b =
                    std::to_string(round(amountTaker_b / std::stod(this->orderQuantityIncrement_b)) * std::stod(this->orderQuantityIncrement_b));
              } else {
                // quoteAmount_std = std::to_string(std::stod(AppUtil::roundInput(quoteAmount, this->orderQuantityIncrement_b,
                // false))/std::stod(this->orderQuantityIncrement_b));
                this->remainingLiquidationQty_b = std::to_string(round(amountTaker_b / std::stod(this->orderQuantityIncrement_b)));
              }
              */

              double aSellLevelTakerB = 0.0;
              double aBuyLevelTakerB = 0.0;
              // in production at that point we possibly didn't recieve bid-ask quote -> not a pb as we send a mkt order
              if (this->tradingMode == TradingMode::BACKTEST) {
                // send a sell mkt order -> taker
                aSellLevelTakerB = std::stod(this->bestBidPrice_b) -
                                   this->offset * abs(std::stod(this->bestBidPrice_b) -
                                                      std::stod(this->bestAskPrice_b));  // sell into the order book == mkt order : BEWARE OF THE DUST...

                // send a buy mkt order
                aBuyLevelTakerB = std::stod(this->bestAskPrice_b) +
                                  this->offset * abs(std::stod(this->bestBidPrice_b) -
                                                     std::stod(this->bestAskPrice_b));  // buy into the order book == mkt order : BEWARE OF THE DUST...
              }

              // double quoteLevel, double quoteAmount, int leg, int side)
              // swl quoter, i.e. leg==0 -> leg a, leg==1 -> leg b ## side==0 -> buy, side==1 -> sell
              APP_LOGGER_INFO("##### Send an initial order size B: " + std::to_string(amountTaker_b));
              this->placeQuote(event, session, requestList, tpnow(), amountTaker_b < 0 ? aSellLevelTakerB : aBuyLevelTakerB, abs(amountTaker_b), 1,
                               amountTaker_b > 0 ? 0 : 1, false);
            }
          }
        }

        // QUOTER ENGINE - START - HERE

        // quoter engine is called under a few...conditions
        if (!this->isLiquidationOrder && !this->isLiquitation && !this->isStopLossTriggered &&
            ((this->isAllSubscriptionStarted &&
              (!(this->isReinit || this->isTarget || this->isInitOrder || this->isReinitOrder) || (this->isLiquidated_b && this->isLiquidated_b))))) {
          // if(!this->isStopLossTriggered && ((this->isAllSubscriptionStarted && (!(this->isReinit || this->isTarget) || (this->isLiquidated_a &&
          // this->isLiquidated_b))) || this->tradingMode == TradingMode::BACKTEST || this->tradingMode == TradingMode::PAPER)) {
          this->postProcessMessageMarketDataEventMarketDepth(event, session, requestList, message, messageTime);
        }
      }  // END SUBSCRIPTION MKT DEPTH

      //
      // RESPONSE MESSAGE TO REQUEST
      //

      /*[2023-12-20T04:56:19.000912458Z] DEBUG: Received an event: Event [
        type = REQUEST_STATUS,
        messageList = [
          Message [
            type = REQUEST_FAILURE,
            recapType = UNKNOWN,
            time = 1970-01-01T00:00:00.000000000Z,
            timeReceived = 2023-12-20T04:56:19.000831907Z,
            elementList = [
              Element [
                nameValueMap = {
                  ERROR_MESSAGE = connect: The socket was closed due to a timeout, category: boost.beast
                }
              ]
            ],
            correlationIdList = [ CANCEL_ALL_ORDERS#prodB ],
            secondaryCorrelationIdMap = {}
          ]
        ]
      ]
          } else if (eventType == Event::Type::REQUEST_STATUS) {
            // STOP TRADER IF STATUS==FAILURE...
            APP_LOGGER_DEBUG("###################################################");
            APP_LOGGER_DEBUG("Event::Type::REQUEST_STATUS recieved");

            const auto& firstMessage = event.getMessageList().at(0);
            const auto& correlationIdList = firstMessage.getCorrelationIdList();

            // retrieve message correlation id and filter out product id
            //std::string correlationId = correlationIdList.at(0);
            //std::string productId = correlationIdList.at(1);
            auto correlationIdTocken = UtilString::split(correlationIdList.at(0), '#');
            std::string correlationId = correlationIdTocken.at(0);
            std::string productId = correlationIdTocken.at(1);

            const auto& messageTimeReceived = firstMessage.getTimeReceived();
            const auto& messageTimeReceivedISO = UtilTime::getISOTimestamp(messageTimeReceived);
            if (firstMessage.getType() == Message::Type::REQUEST_FAILURE) {
              APP_LOGGER_ERROR(event.toStringPretty() + ".");
            }
      */
    } else if (eventType == Event::Type::RESPONSE) {
      APP_LOGGER_DEBUG("###################################################");
      APP_LOGGER_DEBUG("Event::Type::RESPONSE recieved");

      const auto& firstMessage = event.getMessageList().at(0);
      const auto& correlationIdList = firstMessage.getCorrelationIdList();

      // retrieve message correlation id and filter out product id
      // std::string correlationId = correlationIdList.at(0);
      // std::string productId = correlationIdList.at(1);
      auto correlationIdTocken = UtilString::split(correlationIdList.at(0), '#');
      std::string correlationId = correlationIdTocken.at(0);
      std::string productId = correlationIdTocken.at(1);

      const auto& messageTimeReceived = firstMessage.getTimeReceived();
      const auto& messageTimeReceivedISO = UtilTime::getISOTimestamp(messageTimeReceived);
      if (firstMessage.getType() == Message::Type::RESPONSE_ERROR) {
        if (correlationId != "CANCEL_ALL_ORDERS") {
          APP_LOGGER_ERROR(event.toStringPretty() + ".");
        } else {
          APP_LOGGER_DEBUG("CANCEL_ALL_ORDERS SENT WITHOUT ORDERS ON THE MKT FOR LEG: " + productId);
        }
      }
      if (firstMessage.getType() == Message::Type::CREATE_ORDER) {
        APP_LOGGER_DEBUG("###################################################");
        APP_LOGGER_DEBUG("Event::Type::RESPONSE recieved -> CREATE_ORDER");

        // legacy implementation: retrieve message type from message type instead of correlation id
        // if (std::find(correlationIdList.begin(), correlationIdList.end(), std::string("CREATE_ORDER_") + CCAPI_EM_ORDER_SIDE_BUY) != correlationIdList.end()
        // ||
        //    std::find(correlationIdList.begin(), correlationIdList.end(), std::string("CREATE_ORDER_") + CCAPI_EM_ORDER_SIDE_SELL) != correlationIdList.end()
        //    || (std::find(correlationIdList.begin(), correlationIdList.end(), PRIVATE_SUBSCRIPTION_DATA_CORRELATION_ID) != correlationIdList.end() &&
        //     firstMessage.getType() == Message::Type::CREATE_ORDER)) {

        const auto& element = firstMessage.getElementList().at(0);
        Order order;
        order.orderId = element.getValue(CCAPI_EM_ORDER_ID);
        order.clientOrderId = element.getValue(CCAPI_EM_CLIENT_ORDER_ID);

        if (this->exchange_a != "okx" || this->exchange_b != "okx") {
          order.side = element.getValue(CCAPI_EM_ORDER_SIDE);
          order.limitPrice = Decimal(element.getValue(CCAPI_EM_ORDER_LIMIT_PRICE));
          order.quantity = Decimal(element.getValue(CCAPI_EM_ORDER_QUANTITY));
          order.cumulativeFilledQuantity = Decimal("0");
          order.remainingQuantity = order.quantity;
          order.status = element.getValue(CCAPI_EM_ORDER_STATUS);
          bool isBuy = element.getValue(CCAPI_EM_ORDER_SIDE) == CCAPI_EM_ORDER_SIDE_BUY;

          /*
          Received an event: Event [
            type = RESPONSE,
            messageList = [
              Message [
                type = CREATE_ORDER,
                recapType = UNKNOWN,
                time = 1970-01-01T00:00:00.000000000Z,
                timeReceived = 2023-09-14T00:05:24.350835000Z,
                elementList = [
                  Element [
                    nameValueMap = {
                      CUMULATIVE_FILLED_PRICE_TIMES_QUANTITY = 524240.200000,
                      CUMULATIVE_FILLED_QUANTITY = 20.0,
                      INSTRUMENT = BTC-PERPETUAL,
                      LIMIT_PRICE = 26217.0,
                      ORDER_ID = 59236697904,
                      QUANTITY = 20.0,
                      SIDE = BUY,
                      STATUS = filled
                    }
                  ]
                ],
                correlationIdList = [ CREATE_ORDER_BUY#prodA ],
                secondaryCorrelationIdMap = {}
              ]
            ]
          ]
          */

          if (productId == "prodA") {
            APP_LOGGER_DEBUG("#### Release lock for leg a");
            this->is_lock_cancelled_a = false;

            if (isBuy && UtilString::toUpper(order.status) != APP_EVENT_HANDLER_BASE_ORDER_STATUS_FILLED) {
              this->openBuyOrder_a = order;
              // this->numOpenOrdersBuy_a += 1;
            } else {
              this->openSellOrder_a = order;
              // this->numOpenOrdersSell_a += 1;
            }
          } else {
            APP_LOGGER_DEBUG("#### Release lock for leg b");
            this->is_lock_cancelled_b = false;

            if (isBuy && UtilString::toUpper(order.status) != APP_EVENT_HANDLER_BASE_ORDER_STATUS_FILLED) {
              this->openBuyOrder_b = order;
              // this->numOpenOrdersBuy_b += 1;
            } else {
              this->openSellOrder_b = order;
              // this->numOpenOrdersSell_b += 1;
            }
          }
        } else {
          if (productId == "prodA") {
            APP_LOGGER_DEBUG("#### Release lock for leg a");
            this->is_lock_cancelled_a = false;
          } else {
            APP_LOGGER_DEBUG("#### Release lock for leg b");
            this->is_lock_cancelled_b = false;
          }
        }

      } else if (firstMessage.getType() == Message::Type::CANCEL_ORDER) {
        APP_LOGGER_DEBUG("###################################################");
        APP_LOGGER_DEBUG("Event::Type::RESPONSE recieved -> CANCEL_ORDER");

        // legacy implementation: retrieve message type from message type instead of correlation id
        // else if (std::find(correlationIdList.begin(), correlationIdList.end(), this->cancelBuyOrderRequestCorrelationId) != correlationIdList.end() ||
        //           std::find(correlationIdList.begin(), correlationIdList.end(), this->cancelSellOrderRequestCorrelationId) != correlationIdList.end() ||
        //           (std::find(correlationIdList.begin(), correlationIdList.end(), PRIVATE_SUBSCRIPTION_DATA_CORRELATION_ID) != correlationIdList.end() &&
        //            firstMessage.getType() == Message::Type::CANCEL_ORDER)) {
        const auto& element = firstMessage.getElementList().at(0);
        bool isBuy = element.getValue(CCAPI_EM_ORDER_SIDE) == CCAPI_EM_ORDER_SIDE_BUY;

        // cancel id
        std::string orderId = element.getValue(CCAPI_EM_ORDER_ID);

        // BEWARE OF ASYNCHRONOUS MESSAGING...A CREATE ORDER RESPONSE MAY ARRIVE BEFORE THE CANCEL OF THE PREVIOUS ORDER...THEREFORE DON'T NULL
        // ORDER STATUS IF openBuyOrder_a (sell/b) id != from cancel order id see above

        std::string internalOrderId{};
        if (productId == "prodA") {
          if (isBuy) {
            if (this->openBuyOrder_a && this->openBuyOrder_a.get().orderId == orderId) {
              APP_LOGGER_DEBUG("#### CANCEL BUY ORDER A WITH ID: " + this->openBuyOrder_a.get().orderId);
              this->openBuyOrder_a = boost::none;
            } else {
              internalOrderId = this->openBuyOrder_a ? this->openBuyOrder_a.get().orderId : "None";
              APP_LOGGER_DEBUG("#### DON'T INTERNALLY (SHOULD HAVE OCCURED ON THE EXCHANGE THOUGHT) CANCEL BUY ORDER A WITH EXCHANGE ORDER ID: " + orderId +
                               " FOR NEW ORDER ID:" + internalOrderId);
            }
            // this->numOpenOrdersBuy_a = 0;
          } else {
            if (this->openSellOrder_a && this->openSellOrder_a.get().orderId == orderId) {
              APP_LOGGER_DEBUG("#### CANCEL SELL ORDER A WITH ID: " + this->openSellOrder_a.get().orderId);
              this->openSellOrder_a = boost::none;
            } else {
              internalOrderId = this->openSellOrder_a ? this->openSellOrder_a.get().orderId : "None";
              APP_LOGGER_DEBUG("#### DON'T INTERNALLY (SHOULD HAVE OCCURED ON THE EXCHANGE THOUGHT) CANCEL SELL ORDER A WITH EXCHANGE ORDER ID: " + orderId +
                               " FOR NEW ORDER ID:" + internalOrderId);
            }
            // this->numOpenOrdersSell_a = 0;
          }
        } else {
          if (isBuy) {
            if (this->openBuyOrder_b && this->openBuyOrder_b.get().orderId == orderId) {
              APP_LOGGER_DEBUG("#### CANCEL BUY ORDER B WITH ID: " + this->openBuyOrder_b.get().orderId);
              this->openBuyOrder_b = boost::none;
            } else {
              internalOrderId = this->openBuyOrder_b ? this->openBuyOrder_b.get().orderId : "None";
              APP_LOGGER_DEBUG("#### DON'T INTERNALLY (SHOULD HAVE OCCURED ON THE EXCHANGE THOUGHT) CANCEL BUY ORDER B WITH EXCHANGE ORDER ID: " + orderId +
                               " FOR NEW ORDER ID:" + internalOrderId);
            }
            // this->numOpenOrdersBuy_b = 0;
          } else {
            if (this->openSellOrder_b && this->openSellOrder_b.get().orderId == orderId) {
              APP_LOGGER_DEBUG("#### CANCEL SELL ORDER B WITH ID: " + this->openSellOrder_b.get().orderId);
              this->openSellOrder_b = boost::none;
            } else {
              internalOrderId = this->openSellOrder_b ? this->openSellOrder_b.get().orderId : "None";
              APP_LOGGER_DEBUG("#### DON'T INTERNALLY (SHOULD HAVE OCCURED ON THE EXCHANGE THOUGHT) CANCEL SELL ORDER B WITH EXCHANGE ORDER ID: " + orderId +
                               " FOR NEW ORDER ID:" + internalOrderId);
            }
            // this->numOpenOrdersSell_b = 0;
          }
        }

        /*
        // legacy quoting logic...
          if (!this->openBuyOrder && !this->openSellOrder) {
            if (this->accountBalanceRefreshWaitSeconds == 0) {
              this->getAccountBalances(event, session, requestList, messageTimeReceived, messageTimeReceivedISO);
            }
          }
        } else if (std::find(correlationIdList.begin(), correlationIdList.end(), this->cancelOpenOrdersRequestCorrelationId) != correlationIdList.end()) {
          if (this->accountBalanceRefreshWaitSeconds == 0) {
            this->getAccountBalances(event, session, requestList, messageTimeReceived, messageTimeReceivedISO);
          }
        } else if (std::find(correlationIdList.begin(), correlationIdList.end(), this->getAccountBalancesRequestCorrelationId) != correlationIdList.end()) {
          if (this->tradingMode == TradingMode::LIVE) {
            for (const auto& element : firstMessage.getElementList()) {
              this->extractBalanceInfo(element);
            }
          }
          const auto& baseBalanceDecimalNotation = Decimal(UtilString::printDoubleScientific(this->baseBalance)).toString();
          const auto& quoteBalanceDecimalNotation = Decimal(UtilString::printDoubleScientific(this->quoteBalance)).toString();
          if (!this->privateDataOnlySaveFinalSummary && this->accountBalanceCsvWriter &&
              (baseBalanceDecimalNotation != "0" || quoteBalanceDecimalNotation != "0")) {
            this->accountBalanceCsvWriter->writeRow({
                messageTimeReceivedISO,
                baseBalanceDecimalNotation,
                quoteBalanceDecimalNotation,
                this->bestBidPrice,
                this->bestAskPrice,
            });
            this->accountBalanceCsvWriter->flush();
          }
          if (this->numOpenOrders == 0) {
            size_t oldRequestListSize = requestList.size();
            this->placeOrders(event, session, requestList, messageTimeReceived);
            this->numOpenOrders = requestList.size() - oldRequestListSize;
          }
          */
        //} else if (firstMessage.getType() == Message::Type::GET_INSTRUMENT) {

        /*
          type = RESPONSE,
          messageList = [
            Message [
              type = CANCEL_OPEN_ORDERS,
              recapType = UNKNOWN,
              time = 1970-01-01T00:00:00.000000000Z,
              timeReceived = 2023-09-14T00:05:24.376741000Z,
              elementList = [

              ],
              correlationIdList = [ CANCEL_ALL_ORDERS#prodA ],
              secondaryCorrelationIdMap = {}
            ]
          ]
        ]*/

      } else if (firstMessage.getType() == Message::Type::CANCEL_OPEN_ORDERS ||
                 (firstMessage.getType() == Message::Type::RESPONSE_ERROR && correlationId == "CANCEL_ALL_ORDERS")) {
        APP_LOGGER_DEBUG("###################################################");
        APP_LOGGER_DEBUG("Event::Type::RESPONSE recieved -> CANCEL_OPEN_ORDERS");

        // APP_LOGGER_DEBUG("CANCEL_OPEN_ORDERS message: " + firstMessage.toStringPretty());

        // const auto& element = firstMessage.getElementList().at(0);

        if (productId == "prodA") {
          if (!this->isStopLossTriggered) {
            APP_LOGGER_DEBUG("#### CANCEL_OPEN_ORDERS FOR LEG RESPONSE RECIEVED THEN SUBMIT NEW QUOTES FOR A");

            // APP_LOGGER_DEBUG("#### Release lock for leg a");
            // this->is_lock_cancelled_a = false;

            // clean up internal order state
            // this->openBuyOrder_a = boost::none;
            // this->openSellOrder_a = boost::none;

            // swl quoter, i.e. leg==0 -> leg a, leg==1 -> leg b ## side==0 -> buy, side==1 -> sell, is post only
            if (!this->upperLimitReached_a) {
              APP_LOGGER_DEBUG("# Place quote A, side==buy");
              APP_LOGGER_DEBUG("# Quote A, buy level: " + std::to_string(this->aBuyLevelA));
              APP_LOGGER_DEBUG("# Quote A, buy amount: " + std::to_string(this->orderAmount_a));
              this->placeQuote(event, session, requestList, messageTimeReceived, this->aBuyLevelA, this->orderAmount_a, 0, 0,
                               true);  // place ONE QUOTE, therefore one more open order
            } else {
              APP_LOGGER_DEBUG("# Risk limit reached, don't place quote A, side==buy");
            }
            // swl quoter, i.e. leg==0 -> leg a, leg==1 -> leg b ## side==0 -> buy, side==1 -> sell
            if (!this->lowerLimitReached_a) {
              APP_LOGGER_DEBUG("# Place quote A, side==sell");
              APP_LOGGER_DEBUG("# Quote A, sell level: " + std::to_string(this->aSellLevelA));
              APP_LOGGER_DEBUG("# Quote A, sell amount: " + std::to_string(this->orderAmount_a));
              this->placeQuote(event, session, requestList, messageTimeReceived, this->aSellLevelA, this->orderAmount_a, 0, 1,
                               true);  // place ONE QUOTE, therefore one more open order
            } else {
              APP_LOGGER_DEBUG("# Risk limit reached, don't place quote A, side==sell");
            }
            APP_LOGGER_DEBUG("##########################################################");
          } else {
            APP_LOGGER_DEBUG("#### CANCEL_OPEN_ORDERS RESPONSE RECIEVED FOR A -> stop loss triggered, hence don't resubmit new quotes!");
          }
        } else {
          if (!this->isStopLossTriggered) {
            APP_LOGGER_DEBUG("#### CANCEL_OPEN_ORDERS RESPONSE RECIEVED THEN SUBMIT NEW QUOTES FOR B");

            // APP_LOGGER_DEBUG("#### Release lock for leg b");
            // this->is_lock_cancelled_b = false;

            // clean up internal order state
            // this->openBuyOrder_b = boost::none;
            // this->openSellOrder_b = boost::none;

            // swl quoter, i.e. leg==0 -> leg a, leg==1 -> leg b ## side==0 -> buy, side==1 -> sell
            if (!this->upperLimitReached_b) {
              APP_LOGGER_DEBUG("# Place quote B, side==buy");
              APP_LOGGER_DEBUG("# Quote B, buy level: " + std::to_string(this->aBuyLevelB));
              APP_LOGGER_DEBUG("# Quote B, buy amount: " + std::to_string(this->orderAmount_b));
              this->placeQuote(event, session, requestList, messageTimeReceived, this->aBuyLevelB, this->orderAmount_b, 1, 0,
                               true);  // place ONE QUOTE, therefore one more open order
            } else {
              APP_LOGGER_DEBUG("# Risk limit reached, don't place quote B, side==buy");
            }
            // swl quoter, i.e. leg==0 -> leg a, leg==1 -> leg b ## side==0 -> buy, side==1 -> sell
            if (!this->lowerLimitReached_b) {
              APP_LOGGER_DEBUG("# Place quote B, side==sell");
              APP_LOGGER_DEBUG("# Quote B, sell level: " + std::to_string(this->aSellLevelB));
              APP_LOGGER_DEBUG("# Quote B, sell amount: " + std::to_string(this->orderAmount_b));
              this->placeQuote(event, session, requestList, messageTimeReceived, this->aSellLevelB, this->orderAmount_b, 1, 1,
                               true);  // place ONE QUOTE, therefore one more open order
            } else {
              APP_LOGGER_DEBUG("# Risk limit reached, don't place quote B, side==sell");
            }
            APP_LOGGER_DEBUG("##########################################################");
          } else {
            APP_LOGGER_DEBUG("#### CANCEL_OPEN_ORDERS RESPONSE RECIEVED FOR B -> stop loss triggered, hence don't resubmit new quotes!");
          }
        }
      } else if (firstMessage.getType() == Message::Type::GET_ACCOUNT_BALANCES || firstMessage.getType() == Message::Type::GET_ACCOUNTS) {
        // SWL addition to comply with generic get_account_balance request type
        const auto& messageTimeReceived = firstMessage.getTimeReceived();
        const auto& messageTimeReceivedISO = UtilTime::getISOTimestamp(messageTimeReceived);

        // swl two product instantiation
        const auto& elementFirst = firstMessage.getElementList().at(0);
        const auto& correlationIdList = firstMessage.getCorrelationIdList();

        // retrieve message correlation id and filter out product id
        auto correlationIdTocken = UtilString::split(correlationIdList.at(0), '#');
        std::string correlationId = correlationIdTocken.at(0);
        std::string productId = correlationIdTocken.at(1);

        /*
        // live account balance may be misaligned with current bot as the init balance are set to 0 versus balance in exchange accounts...
        if (this->tradingMode == TradingMode::LIVE) {
          for (const auto& element : firstMessage.getElementList()) {
            this->extractBalanceInfo2L(element, productId=="prodA"?0:1);
          }
        }
        */

        // verbose exchange balance at recurrent time
        if (this->exchange_a != "okx" || this->exchange_b != "okx") {
          APP_LOGGER_INFO("###################################################");
          APP_LOGGER_INFO("Event::Type::RESPONSE recieved -> GET_ACCOUNT_BALANCES or GET_ACCOUNTS");
          APP_LOGGER_INFO("# exchange account balance: ");
          for (const auto& element : firstMessage.getElementList()) {
            const auto& asset = element.getValue(CCAPI_EM_ASSET);
            double balance = std::stod(element.getValue(CCAPI_EM_QUANTITY_AVAILABLE_FOR_TRADING));
            if (productId == "prodA") {
              if (UtilString::toLower(asset) == UtilString::toLower(this->baseAsset_a)) {
                APP_LOGGER_INFO("# Base balance Product A : " + std::to_string(balance));
                // this->baseBalance_a = std::stod(element.getValue(CCAPI_EM_QUANTITY_AVAILABLE_FOR_TRADING));
              } else if (UtilString::toLower(asset) == UtilString::toLower(this->quoteAsset_a)) {
                APP_LOGGER_INFO("# Quote balance Product A : " + std::to_string(balance));
                // this->quoteBalance_a = std::stod(element.getValue(CCAPI_EM_QUANTITY_AVAILABLE_FOR_TRADING));
              } else if (UtilString::toLower(asset) == "usdt") {
                // pm account
                APP_LOGGER_INFO("# Balance in usdt (pm account) for Product A : " + std::to_string(balance));
              }
            } else {
              if (UtilString::toLower(asset) == UtilString::toLower(this->baseAsset_b)) {
                APP_LOGGER_INFO("# Base balance Product B : " + std::to_string(balance));
                // this->baseBalance_b = std::stod(element.getValue(CCAPI_EM_QUANTITY_AVAILABLE_FOR_TRADING));
              } else if (UtilString::toLower(asset) == UtilString::toLower(this->quoteAsset_b)) {
                APP_LOGGER_INFO("# Quote balance Product B : " + std::to_string(balance));
                // this->quoteBalance_b = std::stod(element.getValue(CCAPI_EM_QUANTITY_AVAILABLE_FOR_TRADING));
              } else if (UtilString::toLower(asset) == "usdt") {
                // pm account
                APP_LOGGER_INFO("# Balance in usdt (pm account) for Product B : " + std::to_string(balance));
              }
            }
          }
          APP_LOGGER_INFO("###################################################");
        } else {
          APP_LOGGER_INFO("###################################################");
          APP_LOGGER_INFO("Event::Type::RESPONSE recieved -> GET_ACCOUNT_BALANCES or GET_ACCOUNTS");
          APP_LOGGER_INFO("# exchange account balance: ");
          for (const auto& element : firstMessage.getElementList()) {
            const auto& asset = element.getValue(CCAPI_EM_ASSET);
            double balance = std::stod(element.getValue(CCAPI_EM_QUANTITY_AVAILABLE_FOR_TRADING));
            if (productId == "prodA") {
              APP_LOGGER_INFO("# Balance Product A : " + std::to_string(balance) + "[" + asset + "]");
            } else {
              APP_LOGGER_INFO("# Balance Product B : " + std::to_string(balance) + "[" + asset + "]");
            }
          }
          APP_LOGGER_INFO("###################################################");
        }

        const auto& baseBalanceDecimalNotation =
            Decimal(UtilString::printDoubleScientific(productId == "prodA" ? this->baseBalance_a : this->baseBalance_b)).toString();
        const auto& quoteBalanceDecimalNotation =
            Decimal(UtilString::printDoubleScientific(productId == "prodA" ? this->quoteBalance_a : this->quoteBalance_b)).toString();

        // TIME,PRODUCT_ID,BASE_AVAILABLE_BALANCE,QUOTE_AVAILABLE_BALANCE,BEST_BID_PRICE,BEST_ASK_PRICE
        if (!this->privateDataOnlySaveFinalSummary && this->accountBalanceCsvWriter &&
            (baseBalanceDecimalNotation != "0" || quoteBalanceDecimalNotation != "0") && isCSVexport && isCSVexportAll) {
          this->accountBalanceCsvWriter->writeRow({
              messageTimeReceivedISO,
              productId,
              baseBalanceDecimalNotation,
              quoteBalanceDecimalNotation,
              productId == "prodA" ? this->bestBidPrice_a : this->bestBidPrice_b,
              productId == "prodA" ? this->bestAskPrice_a : this->bestAskPrice_b,
          });
          this->accountBalanceCsvWriter->flush();
        }

        /*
        if(productId=="prodA") {
          this->isAccountBalanceUpdatedA = true;
        } else {
          this->isAccountBalanceUpdatedB = true;
        }
        */

      } else if (firstMessage.getType() == Message::Type::GET_ACCOUNT_POSITIONS) {
        APP_LOGGER_INFO("###################################################");
        APP_LOGGER_INFO("Event::Type::RESPONSE recieved -> GET_ACCOUNT_POSITIONS");

        APP_LOGGER_INFO("CHECK INTERNAL VERSUS EXCHANGE POSITION/BALANCE!");

        // SWL addition to comply with generic get_account_position request type
        const auto& messageTimeReceived = firstMessage.getTimeReceived();
        const auto& messageTimeReceivedISO = UtilTime::getISOTimestamp(messageTimeReceived);

        // retrieve message correlation id and filter out product id
        const auto& correlationIdList = firstMessage.getCorrelationIdList();

        Message message = firstMessage;  // one cannot modify a const...

        // in case there are not position AND we are dealing with portfolio margin...response is empty
        if (firstMessage.getElementList().empty()) {
          auto correlationIdTocken = UtilString::split(correlationIdList.at(0), '#');
          std::string correlationId = correlationIdTocken.at(0);
          std::string productId = correlationIdTocken.at(1);

          std::vector<Element> elementList;
          Element element;

          element.insert(CCAPI_INSTRUMENT, productId == "prodA" ? this->instrument_a : this->instrument_b);
          element.insert(CCAPI_EM_POSITION_QUANTITY, Decimal(UtilString::printDoubleScientific(0.0)).toString());
          element.insert(CCAPI_EM_POSITION_SIDE, CCAPI_EM_POSITION_SIDE_LONG);

          elementList.emplace_back(std::move(element));
          message.setElementList(elementList);
        }

        // for (const auto& element : firstMessage.getElementList()) {
        for (const auto& element : message.getElementList()) {
          const auto& instrument_name = element.getValue(CCAPI_INSTRUMENT);

          // filter out products that don't belong to either instrument_a or instrument_b
          if (instrument_name == this->instrument_a || instrument_name == this->instrument_b) {
            std::string productId = instrument_name == this->instrument_a ? "prodA" : "prodB";

            // position check
            std::string side = element.getValue(CCAPI_EM_POSITION_SIDE);
            double qty = abs(std::stod(element.getValue(CCAPI_EM_POSITION_QUANTITY)));  // * (side==CCAPI_EM_POSITION_SIDE_LONG?1:-1);

            // element.insert(CCAPI_EM_POSITION_SIDE,
            // std::string(x["positionSide"].GetString())=="LONG"?CCAPI_EM_POSITION_SIDE_LONG:CCAPI_EM_POSITION_SIDE_SHORT);

            if (side != CCAPI_EM_POSITION_SIDE_LONG) {
              qty *= -1;
            }

            if (productId == "prodA") {
              APP_LOGGER_INFO("#################################################################################################################");
              APP_LOGGER_INFO("##### Compare internal vs exchange balance for product: prodA");
              APP_LOGGER_INFO("##### Local Balances ");

              APP_LOGGER_INFO("##### quoteBalance_a: " + std::to_string(this->quoteBalance_a));
              APP_LOGGER_INFO("##### baseBalance_a: " + std::to_string(this->baseBalance_a));
              // APP_LOGGER_DEBUG("##### position_qty_a: "+ std::to_string(this->position_qty_a));

              APP_LOGGER_INFO("#############################################################");
              APP_LOGGER_INFO("##### ProdA");

              // BEWARE: TODO: SEGGREGATE BETWEEN LINEAR AND INVERSE SWAP
              if (!isContractDenominated_a) {
                this->exchangePositionUSD_a = -qty;
              } else {
                this->exchangePositionUSD_a = -qty * std::stod(this->orderQuantityIncrement_a);
              }

              // this->remainingLiquidationQty_a = qty; // in case liquidation order is filled in multiple instances...one has to keep track before assuming the
              // position is liquidated

              APP_LOGGER_INFO("##### Exchange quote balance_a in USD: " + std::to_string(this->exchangePositionUSD_a));

              APP_LOGGER_INFO("#############################################################");

              // update account balance update state
              this->isAccountBalanceUpdated_a = true;
            } else {
              APP_LOGGER_INFO("#############################################################");
              APP_LOGGER_INFO("##### Compare internal vs exchange balance for product: prodB");
              APP_LOGGER_INFO("##### Local Balances ");

              APP_LOGGER_INFO("##### quoteBalance_b: " + std::to_string(this->quoteBalance_b));
              APP_LOGGER_INFO("##### baseBalance_b: " + std::to_string(this->baseBalance_b));
              // APP_LOGGER_DEBUG("##### position_qty_b: "+ std::to_string(this->position_qty_b));

              APP_LOGGER_INFO("#############################################################");
              APP_LOGGER_INFO("##### ProdB");

              if (!isContractDenominated_b) {
                this->exchangePositionUSD_b = -qty;
              } else {
                this->exchangePositionUSD_b = -qty * std::stod(this->orderQuantityIncrement_b);
              }

              // this->remainingLiquidationQty_b = qty;

              APP_LOGGER_INFO("##### Exchange quote balance_b in USD: " + std::to_string(this->exchangePositionUSD_b));
              APP_LOGGER_INFO("#############################################################");

              // update account balance update state
              this->isAccountBalanceUpdated_b = true;
            }
            // end position check

            // if we reinit the trader then liquidate position
            if (this->isReinit && this->isAllSubscriptionStarted) {
              if (productId == "prodA" && !this->isLiquidationOrderSent_a) {
                APP_LOGGER_INFO("#############################################################");
                APP_LOGGER_INFO("##### Liquidation process for leg A");
                APP_LOGGER_INFO("#############################################################");

                // send a taker/market order for leg a
                double amountTaker_a = abs(this->exchangePositionUSD_a);

                if (amountTaker_a == 0.0) {
                  // don't liquidate several times
                  this->isLiquidationOrderSent_a = true;
                  this->isLiquidated_a = true;  // consistency the private trade quote/base calculation
                }

                // Rem as we send market orders we don't need to send the level -> set to 0.0
                if (amountTaker_a != 0.0 && !this->isLiquidationOrderSent_a) {
                  // don't liquidate several times
                  this->isLiquidationOrderSent_a = true;

                  // for backtest -> market order simulated through an in the book limit orders; for production we don't need mkt price
                  double aSellLevelTakerA = 0.0;
                  double aBuyLevelTakerA = 0.0;
                  if (this->tradingMode == TradingMode::BACKTEST || this->tradingMode == TradingMode::PAPER) {
                    // send a sell mkt order -> taker
                    aSellLevelTakerA = std::stod(this->bestBidPrice_a) -
                                       this->offset * abs(std::stod(this->bestBidPrice_a) -
                                                          std::stod(this->bestAskPrice_a));  // sell into the order book == mkt order : BEWARE OF THE DUST...

                    // send a buy mkt order
                    aBuyLevelTakerA = std::stod(this->bestAskPrice_a) +
                                      this->offset * abs(std::stod(this->bestBidPrice_a) -
                                                         std::stod(this->bestAskPrice_a));  // buy into the order book == mkt order : BEWARE OF THE DUST...
                  }
                  // double quoteLevel, double quoteAmount, int leg, int side)
                  // swl quoter, i.e. leg==0 -> leg a, leg==1 -> leg b ## side==0 -> buy, side==1 -> sell
                  APP_LOGGER_INFO("##### Liquidate order amount A:" + std::to_string(this->exchangePositionUSD_a));
                  // this->placeQuote(event, session, requestList, tpnow(), this->exchangePositionUSD_a>0?aSellLevelTakerA:aBuyLevelTakerA, amountTaker_a,
                  // productId=="prodA"?0:1, this->exchangePositionUSD_a>0?1:0, false);
                  this->placeQuote(event, session, requestList, tpnow(), this->exchangePositionUSD_a > 0 ? aSellLevelTakerA : aBuyLevelTakerA, amountTaker_a,
                                   productId == "prodA" ? 0 : 1, this->exchangePositionUSD_a > 0 ? 0 : 1, false);
                  // this->placeQuote(event, session, requestList, tpnow(), 0.0, amountTaker_a, productId=="prodA"?0:1, this->exchangePositionUSD_a>0?0:1,
                  // false);
                }
              }

              if (productId == "prodB" && !this->isLiquidationOrderSent_b) {
                APP_LOGGER_INFO("#############################################################");
                APP_LOGGER_INFO("##### Liquidation process for leg B");
                APP_LOGGER_INFO("#############################################################");

                // send a taker/market order for leg b
                double amountTaker_b = abs(this->exchangePositionUSD_b);

                if (amountTaker_b == 0.0) {
                  // don't liquidate several times
                  this->isLiquidationOrderSent_b = true;
                  this->isLiquidated_b = true;  // consistency the private trade quote/base calculation
                }

                // Rem as we send market orders we don't need to send the level in live but not in backtest..
                if (amountTaker_b != 0.0 && !this->isLiquidationOrderSent_b) {
                  // don't liquidate several times
                  this->isLiquidationOrderSent_b = true;

                  // for backtest -> market order simulated through an in the book limit orders; for production we don't need mkt price
                  double aSellLevelTakerB = 0.0;
                  double aBuyLevelTakerB = 0.0;
                  if (this->tradingMode == TradingMode::BACKTEST || this->tradingMode == TradingMode::PAPER) {
                    // send a sell mkt order -> taker
                    aSellLevelTakerB = std::stod(this->bestBidPrice_b) -
                                       this->offset * abs(std::stod(this->bestBidPrice_b) -
                                                          std::stod(this->bestAskPrice_b));  // sell into the order book == mkt order : BEWARE OF THE DUST...

                    // send a buy mkt order
                    aBuyLevelTakerB = std::stod(this->bestAskPrice_b) +
                                      this->offset * abs(std::stod(this->bestBidPrice_b) -
                                                         std::stod(this->bestAskPrice_b));  // buy into the order book == mkt order : BEWARE OF THE DUST...
                  }
                  // double quoteLevel, double quoteAmount, int leg, int side)
                  // swl quoter, i.e. leg==0 -> leg a, leg==1 -> leg b ## side==0 -> buy, side==1 -> sell
                  APP_LOGGER_INFO("##### Liquidate order amount B:" + std::to_string(this->exchangePositionUSD_b));
                  this->placeQuote(event, session, requestList, tpnow(), this->exchangePositionUSD_b > 0 ? aSellLevelTakerB : aBuyLevelTakerB, amountTaker_b,
                                   productId == "prodA" ? 0 : 1, this->exchangePositionUSD_b > 0 ? 0 : 1, false);
                  // this->placeQuote(event, session, requestList, tpnow(), 0.0, amountTaker_a, productId=="prodA"?0:1, this->exchangePositionUSD_a>0?0:1,
                  // false);
                }
              }
            }  // END LIQUIDATION

            // reset positions to target positions
            if (this->isTarget && this->isAllSubscriptionStarted) {
              if (productId == "prodA" && !this->isLiquidationOrderSent_a) {
                APP_LOGGER_INFO("#############################################################");
                APP_LOGGER_INFO("##### Reset exchange positions to target position process for leg a.");
                APP_LOGGER_INFO("#############################################################");

                // send a taker/market order for leg a
                this->exchangePositionUSD_a =
                    -this->exchangePositionUSD_a;  // BEWARE: original position meaning within reinit is quote value...therefore the inverse
                double amountTaker_a = this->initTargetPosition_a - this->exchangePositionUSD_a;

                APP_LOGGER_INFO("##### Current exchange position in USD for leg a: " + std::to_string(this->exchangePositionUSD_a));
                APP_LOGGER_INFO("##### Input target position in USD for leg a: " + std::to_string(this->initTargetPosition_a));
                APP_LOGGER_INFO("##### Order size in USD for leg a: " + std::to_string(amountTaker_a));
                APP_LOGGER_INFO("#############################################################");

                if (amountTaker_a == 0.0) {
                  // don't liquidate several times
                  this->isLiquidationOrderSent_a = true;
                  this->isLiquidated_a = true;  // consistency the private trade quote/base calculation
                }
                // Rem as we send market orders we don't need to send the level -> set to 0.0
                if (amountTaker_a != 0.0 && !this->isLiquidationOrderSent_a) {
                  // don't liquidate several times
                  this->isLiquidationOrderSent_a = true;

                  // set liquidation qty
                  this->remainingLiquidationQty_a = amountTaker_a;

                  double aSellLevelTakerA = 0.0;
                  double aBuyLevelTakerA = 0.0;
                  // in production at that point we possibly didn't recieve bid-ask quote -> not a pb as we send a mkt order
                  if (this->tradingMode == TradingMode::BACKTEST) {
                    // send a sell mkt order -> taker
                    aSellLevelTakerA = std::stod(this->bestBidPrice_a) -
                                       this->offset * abs(std::stod(this->bestBidPrice_a) -
                                                          std::stod(this->bestAskPrice_a));  // sell into the order book == mkt order : BEWARE OF THE DUST...

                    // send a buy mkt order
                    aBuyLevelTakerA = std::stod(this->bestAskPrice_a) +
                                      this->offset * abs(std::stod(this->bestBidPrice_a) -
                                                         std::stod(this->bestAskPrice_a));  // buy into the order book == mkt order : BEWARE OF THE DUST...
                  }

                  // double quoteLevel, double quoteAmount, int leg, int side)
                  // swl quoter, i.e. leg==0 -> leg a, leg==1 -> leg b ## side==0 -> buy, side==1 -> sell
                  APP_LOGGER_INFO("##### reset position to given init position with order size A: " + std::to_string(amountTaker_a));
                  this->placeQuote(event, session, requestList, tpnow(), amountTaker_a < 0 ? aSellLevelTakerA : aBuyLevelTakerA, abs(amountTaker_a),
                                   productId == "prodA" ? 0 : 1, amountTaker_a > 0 ? 0 : 1, false);
                }
              }

              if (productId == "prodB" && !this->isLiquidationOrderSent_b) {
                APP_LOGGER_INFO("#############################################################");
                APP_LOGGER_INFO("##### Reset exchange positions to target position process for leg b.");
                APP_LOGGER_INFO("#############################################################");

                // send a taker/market order for leg b
                this->exchangePositionUSD_b =
                    -this->exchangePositionUSD_b;  // BEWARE: original position meaning within reinit is quote value...therefore the inverse
                double amountTaker_b = this->initTargetPosition_b - this->exchangePositionUSD_b;

                APP_LOGGER_INFO("##### Current exchange position in USD for leg b: " + std::to_string(this->exchangePositionUSD_b));
                APP_LOGGER_INFO("##### Input target position in USD for leg b: " + std::to_string(this->initTargetPosition_b));
                APP_LOGGER_INFO("##### Order size in USD for leg b: " + std::to_string(amountTaker_b));
                APP_LOGGER_INFO("#############################################################");

                if (amountTaker_b == 0.0) {
                  // don't liquidate several times
                  this->isLiquidationOrderSent_b = true;
                  this->isLiquidated_b = true;  // consistency the private trade quote/base calculation
                }
                // Rem as we send market orders we don't need to send the level -> set to 0.0
                if (amountTaker_b != 0.0 && !this->isLiquidationOrderSent_b) {
                  // don't liquidate several times
                  this->isLiquidationOrderSent_b = true;

                  // set liquidation qty
                  this->remainingLiquidationQty_b = amountTaker_b;

                  double aSellLevelTakerB = 0.0;
                  double aBuyLevelTakerB = 0.0;
                  // in production at that point we possibly didn't recieve bid-ask quote -> not a pb as we send a mkt order
                  if (this->tradingMode == TradingMode::BACKTEST) {
                    // send a sell mkt order -> taker
                    aSellLevelTakerB = std::stod(this->bestBidPrice_b) -
                                       this->offset * abs(std::stod(this->bestBidPrice_b) -
                                                          std::stod(this->bestAskPrice_b));  // sell into the order book == mkt order : BEWARE OF THE DUST...

                    // send a buy mkt order
                    aBuyLevelTakerB = std::stod(this->bestAskPrice_b) +
                                      this->offset * abs(std::stod(this->bestBidPrice_b) -
                                                         std::stod(this->bestAskPrice_b));  // buy into the order book == mkt order : BEWARE OF THE DUST...
                  }

                  // double quoteLevel, double quoteAmount, int leg, int side)
                  // swl quoter, i.e. leg==0 -> leg a, leg==1 -> leg b ## side==0 -> buy, side==1 -> sell
                  APP_LOGGER_INFO("##### reset position to given init position with order size B: " + std::to_string(amountTaker_b));
                  this->placeQuote(event, session, requestList, tpnow(), amountTaker_b < 0 ? aSellLevelTakerB : aBuyLevelTakerB, abs(amountTaker_b),
                                   productId == "prodA" ? 0 : 1, amountTaker_b > 0 ? 0 : 1, false);
                }
              }
            }  // END POSITION RESET

            // rebalance mechanism
            // cannot do rebalance mechanism when aggregating multiple times the same instument within one subaccount
            if (!this->isInitOrder && !this->isStopLossTriggered && !this->isLiquidationOrder && !this->isReinitOrder &&
                (this->isAllSubscriptionStarted && (!(this->isReinit || this->isTarget) || (this->isLiquidated_a && this->isLiquidated_b)))) {  //} ||
              //(!this->isStopLossTriggered && (this->tradingMode == TradingMode::BACKTEST || this->tradingMode == TradingMode::PAPER))) {
              // if(!this->isStopLossTriggered && (this->isAllSubscriptionStarted && (!(this->isReinit || this->isTarget) || (this->isLiquidated_a &&
              // this->isLiquidated_b))) ||
              //(!this->isStopLossTriggered && (this->tradingMode == TradingMode::BACKTEST || this->tradingMode == TradingMode::PAPER))) {

              // check that both acount-balance/position where updated from the exchanges
              if (this->isAccountBalanceUpdated_a && this->isAccountBalanceUpdated_b) {
                // reset semaphore for account balance
                this->isAccountBalanceUpdated_a = false;
                this->isAccountBalanceUpdated_b = false;

                // check exchange versus local qty
                // this->exchangePositionUSD_a vs this->quoteBalance_a (same for leg b)

                // check if there is/are imbalance with exchange OR if divert position due to to exec of dust order of size < min order size of the other leg...
                // double maxInventory_a = abs(this->typicalOrderSize * this->nc2l * this->normalizedTradingVector_0);
                // double maxInventory_b = abs(this->typicalOrderSize * this->nc2l * this->normalizedTradingVector_1);

                // if there is miss-alignment with either of the leg -> most probaly lost connection in private &/ public channel
                // stop quoting/trading

                // dbg
                // this->quoteBalance_a = this->quoteBalance_a+20;
                // this->quoteBalance_b = this->quoteBalance_b-100;
                // end dbg

                if (this->exchangePositionUSD_a != this->quoteBalance_a) {
                  APP_LOGGER_INFO("#############################################################");
                  APP_LOGGER_INFO("##### WARNING: Mis-alignment in LEG_A between exchange and internal position.");
                  APP_LOGGER_INFO("##### quoteBalance_a: " + std::to_string(this->quoteBalance_a));
                  APP_LOGGER_INFO("##### exchangePositionUSD_a: " + std::to_string(this->exchangePositionUSD_a));
                  // APP_LOGGER_DEBUG("##### STOPPING TRADER!!!");
                  APP_LOGGER_INFO("##### CHECK FOR CONNECTIVITY/INTERNAL ERROR AND EITHER LIQUIDATE POSITIONS OR RESTART TRADER WITH REINIT FLAG.");
                  APP_LOGGER_INFO("#############################################################");

                  // set mismatch flag to prevent rebalancing if private channel is lost...
                  this->isMismatch = true;

                  // this->skipProcessEvent=true;
                }

                if (this->exchangePositionUSD_b != this->quoteBalance_b) {
                  APP_LOGGER_INFO("#############################################################");
                  APP_LOGGER_INFO("##### WARNING: Mis-alignment in LEG_B between exchange and internal position.");
                  APP_LOGGER_INFO("##### quoteBalance_b: " + std::to_string(this->quoteBalance_b));
                  APP_LOGGER_INFO("##### exchangePositionUSD_b: " + std::to_string(this->exchangePositionUSD_b));
                  // APP_LOGGER_DEBUG("##### STOPPING TRADER!!!");
                  APP_LOGGER_INFO("##### CHECK FOR CONNECTIVITY/INTERNAL ERROR AND EITHER LIQUIDATE POSITIONS OR RESTART TRADER WITH REINIT FLAG.");
                  APP_LOGGER_INFO("#############################################################");

                  // set mismatch flag to prevent rebalancing if private channel is lost...
                  this->isMismatch = true;

                  // this->skipProcessEvent=true;
                }

                // Periodically rebalance positions to comply with the trading vector proportionnality
                // BEWARE: if a maker order is filled with (i.e. dust) partial small orders smaller than the min order size of the other
                //         leg then an imbalance occurs. There are no way to anticipate the amount of maker order that will be filled and
                //         the size of each fills (possibly multiple small order...)
                // check for imbalance between legs, see trading vector
                // Rem: As an example a large order the leg with a smaller orderquantity increment is executed as maker through partial fill with partial order
                // size smaller than the other leg order quantity increment then no partial fill is executed on the latter leg...leading to misaligment occurs.
                // misalignment on leg b because orderQuantityIncrement_a < orderQuantityIncrement_b
                // if(false) {
                if (!this->bestBidPrice_a.empty() && !this->bestAskPrice_a.empty() && !this->bestBidPrice_b.empty() && !this->bestAskPrice_b.empty()) {
                  // set quote level for backtest & paper
                  double aSellLevelTakerA =
                      std::stod(this->bestBidPrice_a) -
                      this->offset * abs(std::stod(this->bestBidPrice_a) -
                                         std::stod(this->bestAskPrice_a));  // sell into the order book == mkt order : BEWARE OF THE DUST...

                  // send a buy mkt order
                  double aBuyLevelTakerA = std::stod(this->bestAskPrice_a) +
                                           this->offset * abs(std::stod(this->bestBidPrice_a) -
                                                              std::stod(this->bestAskPrice_a));  // buy into the order book == mkt order : BEWARE OF THE DUST...

                  // send a sell mkt order
                  double aSellLevelTakerB =
                      std::stod(this->bestBidPrice_b) -
                      this->offset * abs(std::stod(this->bestBidPrice_b) -
                                         std::stod(this->bestAskPrice_b));  // sell into the order book == mkt order : BEWARE OF THE DUST...

                  // send a buy mkt order
                  double aBuyLevelTakerB = std::stod(this->bestAskPrice_b) +
                                           this->offset * abs(std::stod(this->bestBidPrice_b) -
                                                              std::stod(this->bestAskPrice_b));  // buy into the order book == mkt order : BEWARE OF THE DUST...
                  double quoteLevelSync{};

                  if (std::stod(this->orderQuantityIncrement_a) < std::stod(this->orderQuantityIncrement_b)) {
                    // deal with leg b as the min order size b is larger than the one from leg a
                    if (abs(abs(this->quoteBalance_b) - abs(this->quoteBalance_a * this->normalizedTradingVector_1 / this->normalizedTradingVector_0)) >=
                        std::stod(this->orderQuantityIncrement_b)) {
                      // rebalance only if internal position aligned with exchange...in case private trade channel connection is lost
                      if (!this->isMismatch) {
                        // amounts expressed in USD
                        double targetPosition_b = -this->quoteBalance_a * abs(this->normalizedTradingVector_1 / this->normalizedTradingVector_0);
                        double orderSize_b = -(targetPosition_b - this->quoteBalance_b);

                        APP_LOGGER_INFO("#############################################################");
                        APP_LOGGER_INFO("##### Mismatch if position between leg a and b (most probably due to min order size increment).");
                        APP_LOGGER_INFO("##### Rebalance product b (min order size b > min order size a) with a market order.");
                        APP_LOGGER_INFO("##### quoteBalance_a: " + std::to_string(this->quoteBalance_a));
                        APP_LOGGER_INFO("##### quoteBalance_b: " + std::to_string(this->quoteBalance_b));
                        APP_LOGGER_INFO("##### targetPosition_b (taking into account trading vector scaling): " + std::to_string(targetPosition_b));
                        APP_LOGGER_INFO("##### orderSize_b: " + std::to_string(orderSize_b));
                        APP_LOGGER_INFO("#############################################################");

                        // send a market order (no need for quote level) to align position on leg b
                        // set quote level for backtest & paper.
                        quoteLevelSync = orderSize_b > 0 ? aBuyLevelTakerB : aSellLevelTakerB;
                        this->placeQuote(event, session, requestList, tpnow(), quoteLevelSync, abs(orderSize_b), 1, orderSize_b > 0 ? 0 : 1, false);
                      }
                    }

                  } else {
                    // deal with leg a as the min order size a is larger than the one from leg b
                    if (abs(abs(this->quoteBalance_a) - abs(this->quoteBalance_b * this->normalizedTradingVector_0 / this->normalizedTradingVector_1)) >=
                        std::stod(this->orderQuantityIncrement_a)) {
                      // rebalance only if internal position aligned with exchange...in case private trade channel connection is lost
                      if (!this->isMismatch) {
                        // amounts expressed in USD
                        double targetPosition_a = -this->quoteBalance_b * abs(this->normalizedTradingVector_0 / this->normalizedTradingVector_1);
                        double orderSize_a = -(targetPosition_a - this->quoteBalance_a);

                        APP_LOGGER_INFO("#############################################################");
                        APP_LOGGER_INFO("##### Mismatch if position between leg a and b (most probably due to min order size increment).");
                        APP_LOGGER_INFO("##### Rebalance product a (min order size a > min order size b) with a market order.");
                        APP_LOGGER_INFO("##### quoteBalance_a: " + std::to_string(this->quoteBalance_a));
                        APP_LOGGER_INFO("##### quoteBalance_b: " + std::to_string(this->quoteBalance_b));
                        APP_LOGGER_INFO("##### targetPosition_a (taking into account trading vector scaling): " + std::to_string(targetPosition_a));
                        APP_LOGGER_INFO("##### orderSize_a: " + std::to_string(orderSize_a));
                        APP_LOGGER_INFO("#############################################################");

                        // send a market order (no need for quote level) to align position on leg b
                        quoteLevelSync = orderSize_a > 0 ? aBuyLevelTakerA : aSellLevelTakerA;
                        this->placeQuote(event, session, requestList, tpnow(), quoteLevelSync, abs(orderSize_a), 0, orderSize_a > 0 ? 0 : 1, false);
                      }
                    }
                  }
                }  // end if quote available
                //}
              }
            }  // END REBALANCE

            /*
            [2024-01-10T18:54:32.085256747Z] DEBUG:
            ################################################################################################################# [2024-01-10T18:54:32.085259650Z]
            DEBUG: ##### Rebalance product: prodA [2024-01-10T18:54:32.085262526Z] DEBUG: ##### Local Balances [2024-01-10T18:54:32.085266525Z] DEBUG: #####
            quoteBalance_a: 820.000000 [2024-01-10T18:54:32.085270025Z] DEBUG: ##### baseBalance_a: -0.018013 [2024-01-10T18:54:32.085273449Z] DEBUG: #####
            position_qty_a: -82.000000 [2024-01-10T18:54:32.085276406Z] DEBUG: #############################################################
            [2024-01-10T18:54:32.085279235Z] DEBUG: ##### ProdA
            [2024-01-10T18:54:32.085282452Z] DEBUG: ##### Exchange quote balance: 820.000000
            [2024-01-10T18:54:32.085285400Z] DEBUG: #############################################################
            */
            /*
            [2024-01-10T18:44:32.989726700Z] DEBUG: ###################################################
            [2024-01-10T18:44:32.989729573Z] DEBUG: Event::Type::RESPONSE recieved
            [2024-01-10T18:44:32.989736276Z] DEBUG: ###################################################
            [2024-01-10T18:44:32.989739175Z] DEBUG: Event::Type::RESPONSE recieved -> GET_ACCOUNT_POSITIONS
            [2024-01-10T18:44:32.989741972Z] DEBUG: DISABLED CURRENTLY!
            [2024-01-10T18:44:32.989749099Z] DEBUG:
            ################################################################################################################# [2024-01-10T18:44:32.989751998Z]
            DEBUG: ##### Rebalance product: prodB [2024-01-10T18:44:32.989754823Z] DEBUG: ##### Local Balances [2024-01-10T18:44:32.989758856Z] DEBUG: #####
            quoteBalance_b: -520.000000 [2024-01-10T18:44:32.989762111Z] DEBUG: ##### baseBalance_b: 7.822827 [2024-01-10T18:44:32.989765393Z] DEBUG: #####
            position_qty_b: 52.000000 [2024-01-10T18:44:32.989768326Z] DEBUG: #############################################################
            [2024-01-10T18:44:32.989771116Z] DEBUG: ##### ProdB
            [2024-01-10T18:44:32.989774402Z] DEBUG: ##### Exchange quote balance: -520.000000
            [2024-01-10T18:44:32.989777323Z] DEBUG: #############################################################
            [2024-01-10T18:44:32.989780793Z] DEBUG: #### processing request list -> size: 0

            */
            // disable auto update
            /**
            this->extractPositionInfo(element, productId);

            const auto& positionDecimalNotation =
            Decimal(UtilString::printDoubleScientific(productId=="prodA"?this->position_qty_a:this->position_qty_b)).toString();

            // keep track of positions (as get position call not available for all exchange API
            //TIME,PRODUCT_ID,POSITION,BEST_BID_PRICE,BEST_ASK_PRICE
            if (!this->privateDataOnlySaveFinalSummary && this->positionCsvWriter && positionDecimalNotation != "0" && isCSVexport) {
              this->positionCsvWriter->writeRow({
                  messageTimeReceivedISO,
                  productId,
                  positionDecimalNotation,
                  productId=="prodA"?this->bestBidPrice_a:this->bestBidPrice_b,
                  productId=="prodA"?this->bestAskPrice_a:this->bestAskPrice_b,
              });
              this->positionCsvWriter->flush();
            }
            */
          }
        }
        // end position management

      } else if (firstMessage.getType() == Message::Type::GET_INSTRUMENT) {
        //} else if (std::find(correlationIdList.begin(), correlationIdList.end(), "GET_INSTRUMENT") != correlationIdList.end()) {

        // swl two product instantiation
        const auto& elementFirst = firstMessage.getElementList().at(0);
        const auto& correlationIdList = firstMessage.getCorrelationIdList();

        // retrieve message correlation id and filter out product id
        auto correlationIdTocken = UtilString::split(correlationIdList.at(0), '#');
        std::string correlationId = correlationIdTocken.at(0);
        std::string productId = correlationIdTocken.at(1);

        // the first message correspond to product a
        // if (std::find(correlationIdList.begin(), correlationIdList.end(), "prodA") != correlationIdList.end()) {
        if (productId == "prodA") {
          extractInstrumentInfo_2p(elementFirst, true);
          isDoneExtractInstrumentInfo += 1;
        }

        // if (std::find(correlationIdList.begin(), correlationIdList.end(), "prodB") != correlationIdList.end()) {
        if (productId == "prodB") {
          extractInstrumentInfo_2p(elementFirst, false);
          isDoneExtractInstrumentInfo += 1;
        }

        if (isDoneExtractInstrumentInfo != 2) return true;

        if (this->tradingMode == TradingMode::BACKTEST) {
          // run historical backtest event handler
          HistoricalMarketDataEventProcessor historicalMarketDataEventProcessor(
              std::bind(&EventHandlerBase::processEvent, this, std::placeholders::_1, nullptr));
          historicalMarketDataEventProcessor.instrument_a = this->instrument_a;
          historicalMarketDataEventProcessor.instrument_b = this->instrument_b;
          historicalMarketDataEventProcessor.exchange_a = this->exchange_a;
          historicalMarketDataEventProcessor.exchange_b = this->exchange_b;
          historicalMarketDataEventProcessor.baseAsset_a = UtilString::toLower(this->baseAsset_a);
          historicalMarketDataEventProcessor.baseAsset_b = UtilString::toLower(this->baseAsset_b);
          historicalMarketDataEventProcessor.quoteAsset_a = UtilString::toLower(this->quoteAsset_a);
          historicalMarketDataEventProcessor.quoteAsset_b = UtilString::toLower(this->quoteAsset_b);

          historicalMarketDataEventProcessor.historicalMarketDataStartDateTp = this->historicalMarketDataStartDateTp;
          historicalMarketDataEventProcessor.historicalMarketDataEndDateTp = this->historicalMarketDataEndDateTp;
          historicalMarketDataEventProcessor.historicalMarketDataDirectory = this->historicalMarketDataDirectory;
          historicalMarketDataEventProcessor.historicalMarketDataFilePrefix = this->historicalMarketDataFilePrefix;
          historicalMarketDataEventProcessor.historicalMarketDataFileSuffix = this->historicalMarketDataFileSuffix;
          historicalMarketDataEventProcessor.startTimeTp = this->startTimeTp;
          historicalMarketDataEventProcessor.totalDurationSeconds = this->totalDurationSeconds;
          historicalMarketDataEventProcessor.processEvent();

          // CREATE RESULT FILE SUMMARY ONLY WHEN PERFORMING / AT THE END OF A BACKTEST
          std::string prefix;
          if (!this->privateDataFilePrefix.empty()) {
            prefix = this->privateDataFilePrefix;
          }
          std::string suffix;
          if (!this->privateDataFileSuffix.empty()) {
            suffix = this->privateDataFileSuffix;
          }

          std::string privateDataSummaryCsvFilename(
              prefix + this->exchange_a + "__" + this->exchange_b + "__" + UtilString::toLower(this->baseAsset_a) + "-" +
              UtilString::toLower(this->quoteAsset_a) + "__" + UtilString::toLower(this->baseAsset_b) + "-" + UtilString::toLower(this->quoteAsset_b) + "__" +
              UtilTime::getISOTimestamp(this->historicalMarketDataStartDateTp).substr(0, 10) + "__" +
              UtilTime::getISOTimestamp(this->historicalMarketDataEndDateTp).substr(0, 10) + "__summary" + suffix + "__" + this->trader_id + ".csv");

          // legacy implementation
          // std::string privateDataSummaryCsvFilename(
          //    prefix + this->exchange + "__" + UtilString::toLower(this->baseAsset) + "-" + UtilString::toLower(this->quoteAsset) + "__" +
          //    UtilTime::getISOTimestamp(this->historicalMarketDataStartDateTp).substr(0, 10) + "__" +
          //    UtilTime::getISOTimestamp(this->historicalMarketDataEndDateTp).substr(0, 10) + "__summary" + suffix + ".csv");

          if (isCSVexport) {
            if (!this->privateDataDirectory.empty()) {
              privateDataSummaryCsvFilename = this->privateDataDirectory + "/" + privateDataSummaryCsvFilename;
            }
            CsvWriter* privateDataFinalSummaryCsvWriter = new CsvWriter();
            {
              struct stat buffer;
              if (stat(privateDataSummaryCsvFilename.c_str(), &buffer) != 0) {
                privateDataFinalSummaryCsvWriter->open(privateDataSummaryCsvFilename, std::ios_base::app);
                privateDataFinalSummaryCsvWriter->writeRow({
                    "BASE_AVAILABLE_BALANCE_A",
                    "QUOTE_AVAILABLE_BALANCE_A",
                    "BEST_BID_PRICE_A",
                    "BEST_ASK_PRICE_A",
                    "TRADE_VOLUME_IN_BASE_SUM_A",
                    "TRADE_VOLUME_IN_QUOTE_SUM_A",
                    "TRADE_FEE_IN_BASE_SUM_A",
                    "TRADE_FEE_IN_QUOTE_SUM_A",
                    "BASE_AVAILABLE_BALANCE_B",
                    "QUOTE_AVAILABLE_BALANCE_B",
                    "BEST_BID_PRICE_B",
                    "BEST_ASK_PRICE_B",
                    "TRADE_VOLUME_IN_BASE_SUM_B",
                    "TRADE_VOLUME_IN_QUOTE_SUM_B",
                    "TRADE_FEE_IN_BASE_SUM_B",
                    "TRADE_FEE_IN_QUOTE_SUM_B",
                });
                privateDataFinalSummaryCsvWriter->flush();
              } else {
                privateDataFinalSummaryCsvWriter->open(privateDataSummaryCsvFilename, std::ios_base::app);
              }
            }

            privateDataFinalSummaryCsvWriter->writeRow({
                Decimal(UtilString::printDoubleScientific(this->baseBalance_a)).toString(),
                Decimal(UtilString::printDoubleScientific(this->quoteBalance_a)).toString(),
                this->bestBidPrice_a,
                this->bestAskPrice_a,
                // legacy
                // this->bestBidPrice_a,
                // this->bestAskPrice_a,
                Decimal(UtilString::printDoubleScientific(this->privateTradeVolumeInBaseSum_a)).toString(),
                Decimal(UtilString::printDoubleScientific(this->privateTradeVolumeInQuoteSum_a)).toString(),
                Decimal(UtilString::printDoubleScientific(this->privateTradeFeeInBaseSum_a)).toString(),
                Decimal(UtilString::printDoubleScientific(this->privateTradeFeeInQuoteSum_a)).toString(),
                Decimal(UtilString::printDoubleScientific(this->baseBalance_b)).toString(),
                Decimal(UtilString::printDoubleScientific(this->quoteBalance_b)).toString(),
                this->bestBidPrice_b,
                this->bestAskPrice_b,
                Decimal(UtilString::printDoubleScientific(this->privateTradeVolumeInBaseSum_b)).toString(),
                Decimal(UtilString::printDoubleScientific(this->privateTradeVolumeInQuoteSum_b)).toString(),
                Decimal(UtilString::printDoubleScientific(this->privateTradeFeeInBaseSum_b)).toString(),
                Decimal(UtilString::printDoubleScientific(this->privateTradeFeeInQuoteSum_b)).toString(),
            });
            privateDataFinalSummaryCsvWriter->flush();
            delete privateDataFinalSummaryCsvWriter;
          }
          try {
            this->promisePtr->set_value();
          } catch (const std::future_error& e) {
            APP_LOGGER_DEBUG(e.what());
          }

          // IF ONE SEND A GET INSTRUMENT AND IS NOT IN A BACKTEST THEN SUBSCRIBE...NOT THE CLEANEST
        } else {
          std::vector<Subscription> subscriptionList;
          this->createSubscriptionList(subscriptionList);
          session->subscribe(subscriptionList);
        }
      }
      // END RESPONSE

      // Check session status and subscription status PRIOR to starting trading logic and processes...
    } else if (eventType == Event::Type::SESSION_STATUS) {
      for (const auto& message : event.getMessageList()) {
        if (message.getType() == Message::Type::SESSION_CONNECTION_UP) {
          for (const auto& correlationId : message.getCorrelationIdList()) {
            if (correlationId == "PRIVATE_TRADE#prodA") {
              // if (correlationId == PRIVATE_SUBSCRIPTION_DATA_CORRELATION_ID) {
              const auto& messageTimeReceived = message.getTimeReceived();
              const auto& messageTimeReceivedISO = UtilTime::getISOTimestamp(messageTimeReceived);
              // this->cancelOpenOrders(event, session, requestList, messageTimeReceived, messageTimeReceivedISO, true, "prodA", "buy");
              // this->cancelOpenOrders(event, session, requestList, messageTimeReceived, messageTimeReceivedISO, true, "prodA", "sell");
            }
            if (correlationId == "PRIVATE_TRADE#prodB") {
              const auto& messageTimeReceived = message.getTimeReceived();
              const auto& messageTimeReceivedISO = UtilTime::getISOTimestamp(messageTimeReceived);
              // this->cancelOpenOrders(event, session, requestList, messageTimeReceived, messageTimeReceivedISO, true, "prodB", "buy");
              // this->cancelOpenOrders(event, session, requestList, messageTimeReceived, messageTimeReceivedISO, true, "prodB", "sell");
            }
          }
        }
      }
    } else if (eventType == Event::Type::SUBSCRIPTION_STATUS) {
      for (const auto& message : event.getMessageList()) {
        if (message.getType() == Message::Type::SUBSCRIPTION_STARTED) {
          // check if all subscriptions started & unlock onQuote method (see. isAllSubscriptionStarted)
          for (const auto& correlationId : message.getCorrelationIdList()) {
            // APP_LOGGER_DEBUG("####JFA - correlation ID subscription status: " + correlationId);

            if (correlationId == "PRIVATE_TRADE#prodA") {
              this->is_subscription_started_private_trade_a = true;
            }
            if (correlationId == "PRIVATE_TRADE#prodB") {
              this->is_subscription_started_private_trade_b = true;
            }
            if (correlationId == "TRADE#prodA") {
              this->is_subscription_started_trade_a = true;
            }
            if (correlationId == "TRADE#prodB") {
              this->is_subscription_started_trade_b = true;
            }
            if (correlationId == "MARKET_DEPTH#prodA") {
              this->is_subscription_started_mkt_depth_a = true;
            }
            if (correlationId == "MARKET_DEPTH#prodB") {
              this->is_subscription_started_mkt_depth_b = true;
            }
            if (correlationId == "ORDER_UPDATE#prodA") {
              this->is_subscription_started_order_update_a = true;
            }
            if (correlationId == "ORDER_UPDATE#prodB") {
              this->is_subscription_started_order_update_b = true;
            }

            if (this->is_subscription_started_private_trade_a && this->is_subscription_started_private_trade_b && this->is_subscription_started_trade_a &&
                this->is_subscription_started_trade_b && this->is_subscription_started_order_update_a && this->is_subscription_started_order_update_b &&
                this->is_subscription_started_mkt_depth_a && this->is_subscription_started_mkt_depth_b) {
              APP_LOGGER_INFO("################################################################################################################");
              APP_LOGGER_INFO("#### ALL SUBSCRIPTION STARTED -> READY FOR TRADING -> UNLOCK ONQUOTE METHOD & GET_ACCOUNT_POSITIONS AND BALANCE.");
              APP_LOGGER_INFO("################################################################################################################");

              this->isAllSubscriptionStarted = true;

              // then get exchange positions (eventually reinit)
              const auto& messageTimeReceived = message.getTimeReceived();
              this->getAccountPositions(event, session, requestList, messageTimeReceived, "prodA");
              this->getAccountPositions(event, session, requestList, messageTimeReceived, "prodB");

              const auto& messageTimeReceivedISO = UtilTime::getISOTimestamp(messageTimeReceived);
              this->getAccountBalances(event, session, requestList, messageTimeReceived, messageTimeReceivedISO, "prodA");
              this->getAccountBalances(event, session, requestList, messageTimeReceived, messageTimeReceivedISO, "prodB");
            }
          }

          for (const auto& element : message.getElementList()) {
            APP_LOGGER_DEBUG(element.getValue(CCAPI_INFO_MESSAGE));
          }
        } else if (message.getType() == Message::Type::SUBSCRIPTION_FAILURE) {
          for (const auto& element : message.getElementList()) {
            APP_LOGGER_ERROR(element.getValue(CCAPI_ERROR_MESSAGE));
          }
        }
      }
    }
    // legacy
    // this->processEventFurther(event, session, requestList);

    APP_LOGGER_DEBUG("#### processing request list -> size: " + std::to_string(requestList.size()));
    if (!requestList.empty()) {
      // legacy
      /*
      APP_LOGGER_DEBUG("#####################################################################################################");
      APP_LOGGER_DEBUG("##### keep track of number of request for rate limit check");
      APP_LOGGER_DEBUG("##### shortTimeIntervalNumberOfRequest: " + std::to_string(this->shortTimeIntervalNumberOfRequest));
      APP_LOGGER_DEBUG("##### longTimeIntervalNumberOfRequest: " + std::to_string(this->longTimeIntervalNumberOfRequest));

      // keep track of number of request for rate limit check
      this->shortTimeIntervalNumberOfRequest += requestList.size();
      this->longTimeIntervalNumberOfRequest += requestList.size();
      */

      APP_LOGGER_DEBUG("#### request list not empty, request list content: ");
      for (int i = 0; i < requestList.size(); i++) {
        APP_LOGGER_DEBUG("request[" + std::to_string(i) + "]: " + requestList[i].toString());
      }

      if (this->tradingMode == TradingMode::PAPER || this->tradingMode == TradingMode::BACKTEST) {
        APP_LOGGER_DEBUG("#####################################################################################################");
        APP_LOGGER_DEBUG("#### Call fakeServerResponse for request list for paper and backtest only");
        fakeServerResponse(session, requestList);
      }

      if (this->tradingMode == TradingMode::LIVE) {
        //
        // LIVE - EITHER WITH OR WITHOUR WEBSOCKET -> seggregate by product a,b
        //
        for (auto& request : requestList) {
          std::string corrId = request.getCorrelationId();

          // retrieve message correlation id and filter out product id
          auto correlationIdTocken = UtilString::split(corrId, '#');
          std::string correlationId = correlationIdTocken.at(0);
          std::string productId = correlationIdTocken.at(1);

          auto operation = request.getOperation();

          // BEWARE IF EXCHANGE USE WEBSOCKET FOR EXECUTION -> MAKE SURE THAT CANCEL_ALL_ORDERS OPERATION IS SET IN THE IF CONDITION
          if (operation == Request::Operation::CREATE_ORDER || operation == Request::Operation::CANCEL_ORDER) {
            if (productId == "prodA" && this->useWebsocketToExecuteOrder_a) {
              session->sendRequestByWebsocket(request);
            }

            if (productId == "prodB" && this->useWebsocketToExecuteOrder_b) {
              session->sendRequestByWebsocket(request);
            }

            if (productId == "prodA" && !this->useWebsocketToExecuteOrder_a) session->sendRequest(request);
            if (productId == "prodB" && !this->useWebsocketToExecuteOrder_b) session->sendRequest(request);

            // if(!this->useWebsocketToExecuteOrder_a && !this->useWebsocketToExecuteOrder_b) session->sendRequest(request);
          } else {
            session->sendRequest(request);
          }
        }

        // legacy
        /*
        // TODO: SEGGREGATE BY PRODUCT, I.E. useWebsocketToExecuteOrder_a, b
        if (this->useWebsocketToExecuteOrder) {
          for (auto& request : requestList) {
            auto operation = request.getOperation();
            if (operation == Request::Operation::CREATE_ORDER || operation == Request::Operation::CANCEL_ORDER) {
              // in live trading check that the correct correlation Id is set within the request

              // legacy
              //request.setCorrelationId(PRIVATE_SUBSCRIPTION_DATA_CORRELATION_ID);
              session->sendRequestByWebsocket(request);
            } else {
              session->sendRequest(request);
            }
          }
        } else {
          for (auto& request : requestList) {
            session->sendRequest(request);
          }

          // original code
          //session->sendRequest(requestList);
        }
        */
      }  // end live

    }  // END if (!requestList.empty())

    return true;
  }  // PROCESS EVENT

  //
  // GLOBAL PARAMETERS SETTINGS
  //
  AppMode appMode{AppMode::MARKET_MAKING};
  std::string previousMessageTimeISODate, exchange, instrumentRest, instrumentWebsocket, baseAsset, quoteAsset, accountId, orderPriceIncrement,
      orderQuantityIncrement, privateDataDirectory, privateDataFilePrefix, privateDataFileSuffix, bestBidPrice, bestBidSize, bestAskPrice, bestAskSize,
      cancelOpenOrdersRequestCorrelationId, getAccountBalancesRequestCorrelationId, cancelBuyOrderRequestCorrelationId, cancelSellOrderRequestCorrelationId;
  double halfSpreadMinimum{}, halfSpreadMaximum{}, inventoryBasePortionTarget{}, baseBalance{}, quoteBalance{}, baseAvailableBalanceProportion{1},
      killSwitchMaximumDrawdown{}, quoteAvailableBalanceProportion{1}, orderQuantityProportion{}, totalBalancePeak{},
      adverseSelectionGuardTriggerInventoryBasePortionMinimum{}, adverseSelectionGuardTriggerInventoryBasePortionMaximum{},
      adverseSelectionGuardActionOrderQuantityProportion{}, adverseSelectionGuardTriggerRollCorrelationCoefficientMaximum{},
      adverseSelectionGuardTriggerRocMinimum{}, adverseSelectionGuardTriggerRocMaximum{}, adverseSelectionGuardTriggerRsiMinimum{},
      adverseSelectionGuardTriggerRsiMaximum{}, privateTradeVolumeInBaseSum{}, privateTradeVolumeInQuoteSum{}, privateTradeFeeInBaseSum{},
      privateTradeFeeInQuoteSum{}, midPrice{};

  TimePoint orderRefreshLastTime{std::chrono::seconds{0}}, cancelOpenOrdersLastTime{std::chrono::seconds{0}},
      getAccountBalancesLastTime{std::chrono::seconds{0}};
  bool useGetAccountsToGetAccountBalances{}, useCancelOrderToCancelOpenOrders{}, useWebsocketToExecuteOrder{}, useWeightedMidPrice{},
      privateDataOnlySaveFinalSummary{}, enableAdverseSelectionGuard{}, enableAdverseSelectionGuardByInventoryLimit{},
      enableAdverseSelectionGuardByInventoryDepletion{}, enableAdverseSelectionGuardByRollCorrelationCoefficient{},
      adverseSelectionGuardActionOrderQuantityProportionRelativeToOneAsset{}, enableAdverseSelectionGuardByRoc{}, enableAdverseSelectionGuardByRsi{},
      immediatelyPlaceNewOrders{}, adverseSelectionGuardTriggerRocOrderDirectionReverse{}, adverseSelectionGuardTriggerRsiOrderDirectionReverse{},
      adverseSelectionGuardTriggerRollCorrelationCoefficientOrderDirectionReverse{}, enableMarketMaking{};

  TradingMode tradingMode{TradingMode::LIVE};

  AdverseSelectionGuardActionType adverseSelectionGuardActionType{AdverseSelectionGuardActionType::NONE};
  std::shared_ptr<std::promise<void>> promisePtr{nullptr};
  // int numOpenOrders{};
  boost::optional<Order> openBuyOrder, openSellOrder;
  std::map<std::string, std::string> credential_2;

  // start: only single order execution
  TimePoint startTimeTp{std::chrono::seconds{0}};
  int totalDurationSeconds{}, numOrderRefreshIntervals{}, orderRefreshIntervalIndex{-1};
  std::string orderSide;
  TradingStrategy tradingStrategy{TradingStrategy::TWAP};
  // end: only single order execution

  // start: only applicable to paper trade and backtest
  double makerFee{}, takerFee{}, marketImpfactFactor{};
  std::string makerBuyerFeeAsset, makerSellerFeeAsset, takerBuyerFeeAsset, takerSellerFeeAsset;
  // end: only applicable to paper trade and backtest

  // database specifics
  std::string dashboardAddress, databaseLogin, databasePassword, databaseName;

  // swl uptdate for two products
  std::string exchange_a, exchange_b, instrumentRest_a, instrumentRest_b, instrumentWebsocket_a, instrumentWebsocket_b, baseAsset_a, quoteAsset_a, baseAsset_b,
      quoteAsset_b, orderPriceIncrement_a, orderQuantityIncrement_a, orderPriceIncrement_b, orderQuantityIncrement_b, bestBidPrice_a, bestBidSize_a,
      bestAskPrice_a, bestAskSize_a, bestBidPrice_b, bestBidSize_b, bestAskPrice_b, bestAskSize_b, accountId_a, accountId_b,
      cancelBuyOrderRequestCorrelationId_a, cancelBuyOrderRequestCorrelationId_b, cancelSellOrderRequestCorrelationId_a, cancelSellOrderRequestCorrelationId_b,
      cancelOpenOrdersRequestCorrelationId_a, cancelOpenOrdersRequestCorrelationId_b, newBestBidPrice_a, newBestAskPrice_a, newBestBidPrice_b,
      newBestAskPrice_b, newBestBidSize_a, newBestAskSize_a, newBestBidSize_b, newBestAskSize_b, instrument_type_a, instrument_type_b, instrument_a,
      instrument_b;

  double baseBalance_a{}, baseBalance_b{}, quoteBalance_a{}, quoteBalance_b{}, privateTradeVolumeInBaseSum_a{}, privateTradeVolumeInBaseSum_b{},
      privateTradeVolumeInQuoteSum_a{}, privateTradeVolumeInQuoteSum_b{}, privateTradeFeeInBaseSum_a{}, privateTradeFeeInBaseSum_b{},
      privateTradeFeeInQuoteSum_a{}, privateTradeFeeInQuoteSum_b{}, position_qty_a{}, position_qty_b{}, amountTaker{}, aSellLevelB = -1, aBuyLevelB = -1,
                                                                                                                       aSellLevelA = -1, aBuyLevelA = -1;
  bool useGetAccountsToGetAccountBalances_a{}, useGetAccountsToGetAccountBalances_b{}, useCancelOrderToCancelOpenOrders_a{false},
      useCancelOrderToCancelOpenOrders_b{false}, useWebsocketToExecuteOrder_a{}, useWebsocketToExecuteOrder_b{};

  double midPriceA{}, midPriceB{};

  // bool isAccountBalanceUpdatedA = false, isAccountBalanceUpdatedB = false;
  int isDoneExtractInstrumentInfo = 0, accountBalanceRefreshWaitSeconds = 0;

  // SESSION AND SUBSCRIPTION STATUS (no subscription is possible without a session up...therefore just consider subscription)
  bool isAllSubscriptionStarted = false;  //(this->tradingMode == TradingMode::PAPER || this->tradingMode == TradingMode::BACKTEST)?true:false;
  // bool isAllSubscriptionStarted=false;

  // session
  // bool is_session_up_private_trade_a=false, is_session_up_private_trade_b=false, is_session_up_order_update_a=false, is_session_up_order_update_b=false,
  //     is_session_up_mkt_depth_a=false, is_session_up_mkt_depth_b=false, is_session_up_trade_a=false, is_session_up_trade_b=false;
  bool is_subscription_started_private_trade_a = false, is_subscription_started_private_trade_b = false, is_subscription_started_order_update_a = false,
       is_subscription_started_order_update_b = false, is_subscription_started_mkt_depth_a = false, is_subscription_started_mkt_depth_b = false,
       is_subscription_started_trade_a = false, is_subscription_started_trade_b = false;

  bool isMismatch = false;

  // ALGORITHM INPUT PARAMETERS
  double normalizedSignalVector_0{}, normalizedSignalVector_1{}, margin{}, stepback{}, typicalOrderSize{}, nc2l{}, theoreticalPrice{},
      anOldBuyLevelA = -1, anOldSellLevelA = -1, anOldBuyLevelB = -1, anOldSellLevelB = -1, normalizedTradingVector_0{}, normalizedTradingVector_1;
  int relativeTargetPosition{}, targetPosition_a{}, targetPosition_b{};

  bool isUpwardCrossing = false, isDownwardCrossing = false;

  double exchangePositionUSD_a = 0.0;
  double exchangePositionUSD_b = 0.0;

  // skewing logic
  int timeLimit = 4;  // skew quotes if two occurences take place within a 4 sec period
  int nbTickSinceDownCrossing = 0;
  int nbTickSinceUpCrossing = 0;
  double skewUpperBound = 1;
  double skewLowerBound = 1;

  // int tmp=0;
  //
  //  set credentials for testnets
  // std::map<std::string, std::string> credential_a = {{"BINANCE_API_KEY", "03197b95f6e6572d45004f5d2d30e3d4d159fce576d427a0cf1a1a85cc582150"},
  //                                                   {"BINANCE_API_SECRET", "601e9b74fe6c21d3b22f876472cba6c1d59fd4b16338212895b6e0c7899e02e0"}};
  // std::map<std::string, std::string> credential_b = {{"DERIBIT_CLIENT_ID", "sa2bkpJ0"},
  //                                                    {"DERIBIT_CLIENT_SECRET", "6e3MfhLYsGNzXz4iiQmn5VdUu8TmGmfgvhkIhYjjADg"}};
  /*
    std::map<std::string, std::string> credential_a = {{CCAPI_BINANCE_COIN_FUTURES_API_KEY, "03197b95f6e6572d45004f5d2d30e3d4d159fce576d427a0cf1a1a85cc582150"},
                                                      {CCAPI_BINANCE_COIN_FUTURES_API_SECRET,
    "601e9b74fe6c21d3b22f876472cba6c1d59fd4b16338212895b6e0c7899e02e0"}}; std::map<std::string, std::string> credential_b = {{CCAPI_DERIBIT_CLIENT_ID,
    "sa2bkpJ0"}, {CCAPI_DERIBIT_CLIENT_SECRET, "6e3MfhLYsGNzXz4iiQmn5VdUu8TmGmfgvhkIhYjjADg"}};
  */

  // if same exchange (i.e. bitmex) then same credentials...
  // testnet
  /*
  std::map<std::string, std::string> credential_a = {{CCAPI_BITMEX_API_KEY, "VbOHtQdznVbXy5nrNgqkfKBd"},
                                                    {CCAPI_BITMEX_API_SECRET, "XCZglN3QQ9euU6R7NvIWnJkyyL3mhkHYCqy9JPBFc4_dDjCC"}};

  std::map<std::string, std::string> credential_b = {{CCAPI_BITMEX_API_KEY, "VbOHtQdznVbXy5nrNgqkfKBd"},
                                                    {CCAPI_BITMEX_API_SECRET, "XCZglN3QQ9euU6R7NvIWnJkyyL3mhkHYCqy9JPBFc4_dDjCC"}};
  */
  // live trading
  /*
  std::map<std::string, std::string> credential_a = {{CCAPI_BITMEX_API_KEY, "pENPEU6rAF9NjrVA2H2o5Ha6"},
                                                  {CCAPI_BITMEX_API_SECRET, "nWzhnAW186eiGo3bP45mIoNEUUt78NKznVG59jQRdrEMHJZ_"}};

  std::map<std::string, std::string> credential_b = {{CCAPI_BITMEX_API_KEY, "pENPEU6rAF9NjrVA2H2o5Ha6"},
                                                  {CCAPI_BITMEX_API_SECRET, "nWzhnAW186eiGo3bP45mIoNEUUt78NKznVG59jQRdrEMHJZ_"}};
*/

  std::string trader_id{};  // retrieved from command line
  bool isReinit{}, isTarget{}, isLiquitation{}, isInitOrder{}, isLiquidationOrder{}, isReinitOrder{};
  double initTargetPosition_a{}, initTargetPosition_b{}, initOrderSize_a{}, initOrderSize_b{};

  bool isLiquidated_a = false, isLiquidated_b = false;
  bool isLiquidationOrderSent_a = false, isLiquidationOrderSent_b = false;
  bool isStopLossTriggered = false;

  bool isAccountBalanceUpdated_a = false, isAccountBalanceUpdated_b = false;
  double remainingLiquidationQty_a = 0.0, remainingLiquidationQty_b = 0.0;

  std::map<std::string, std::string> credential_a = {{CCAPI_DERIBIT_CLIENT_ID, "1N8P15Ra"},
                                                     {CCAPI_DERIBIT_CLIENT_SECRET, "lAajxEEyw8CPym6YjWX7-y7xBH2ntDQ9KFNXPCjeBgs"}};
  std::map<std::string, std::string> credential_b = {
      {CCAPI_BINANCE_COIN_FUTURES_API_KEY, "krZaFk8qbRTxQYiQOaF3cL1gLgfozUit2sXsyBy2pGpLWEGneTDyXoENQtpzdmiA"},
      {CCAPI_BINANCE_COIN_FUTURES_API_SECRET, "rFLn4qGHxcLYJKdWvudwUNhRv4UQmkqsQT4xVqfAHX5evfk8RNWWNM0kFp9sSllQ"}};

  /*
  # binance production
  #API_KEY_A=v2sw3UHHVWrxWcf7KCqCVoJ7XNvUzzpCIf3SX8IXauFxiwBeeM7bIv5lPHqPLmvt
  #API_SECRET_A=jCNgm6524pEu01zlhObTpQ9552bvdbjbN2G6Ow58oSaHDCKHfJPrdwHYiiquK2B5

  # deribit production
  #API_KEY_B=1N8P15Ra
  #API_SECRET_B=lAajxEEyw8CPym6YjWX7-y7xBH2ntDQ9KFNXPCjeBgs
  */
  // SWL_CREDENTIALS
  // this->clientIdName = CCAPI_DERIBIT_CLIENT_ID;
  // this->clientSecretName = CCAPI_DERIBIT_CLIENT_SECRET;
  // this->setupCredential({this->clientIdName, this->clientSecretName});

  // this->apiKeyName = CCAPI_BINANCE_COIN_FUTURES_API_KEY;
  // this->apiSecretName = CCAPI_BINANCE_COIN_FUTURES_API_SECRET;
  // this->setupCredential({this->apiKeyName, this->apiSecretName});

  // credentials for production
  // std::map<std::string, std::string> credential_b = {{"DERIBIT_CLIENT_ID", "1N8P15Ra"},
  //                                                   {"DERIBIT_CLIENT_SECRET", "lAajxEEyw8CPym6YjWX7-y7xBH2ntDQ9KFNXPCjeBgs"}};

  // https://coinsbench.com/trading-binance-perpetuals-and-coin-m-futures-3f391a5a4d65
  // Binance interpret “quantity” as “Number of Contracts”
  // Now, this is different from one exchange to another. Take Deribit, for example: BTC/USD-31DEC21.
  // It’s an inverse with contract size 10 as well. When you call ccxt.exchange.create_order, “amount” should be USD notional (But do NOT divide by contract
  // size)
  bool isContractDenominated_a = false;
  bool isContractDenominated_b = true;

  bool isCSVexport = true;
  bool isCSVexportAll = false;
  bool isMysqlexport = true;

  // lock in order to wait for a cancel confirmation and market (hedge) trade execution
  bool is_lock_cancelled_a = false, is_lock_cancelled_b = false;
  bool is_lock_hedged_a = false, is_lock_hedged_b = false;

  // init quoter parameters
  float epsilon = 1.e-5;  // 0.00001;//0.005f;
  // int counter = -1;

  // gather data every seconds
  bool enableUpdateOrderBookTickByTick = false;
  int clockStepMilliseconds = 1000;

  // int marketChaserDelay = 1000; // -> account balance output frequency

  // in the book distance
  // int offset= 10;//100;
  // int offset= 0;
  int offset = 5;

  int TTL = 10;  // time to live of an order in sec

  int TTLopenBuyOrder_a{}, TTLopenBuyOrder_b{}, TTLopenSellOrder_a{}, TTLopenSellOrder_b{};

  // take into account order size and normalized trading vector & mkt price between two legs
  double orderAmount_a{}, orderAmount_b{};
  // double orderAmount = 500; //in USD
  int relative_position{}, real_position_a{}, real_position_b{};

  bool upperLimitReached_a = false;
  bool lowerLimitReached_a = false;

  bool upperLimitReached_b = false;
  bool lowerLimitReached_b = false;
  // end algorithm specific

  // int numOpenOrders_a{}, numOpenOrders_b{};
  int numOpenOrdersBuy_a{}, numOpenOrdersSell_a{}, numOpenOrdersBuy_b{}, numOpenOrdersSell_b{};
  boost::optional<Order> openBuyOrder_a, openSellOrder_a, openBuyOrder_b, openSellOrder_b;

  std::map<std::string, std::string> credential_2_a;
  std::map<std::string, std::string> credential_2_b;

  // start: only applicable to paper trade and backtest
  double makerFee_a{}, makerFee_b{}, takerFee_a{}, takerFee_b{}, marketImpfactFactor_a{}, marketImpfactFactor_b{};
  std::string makerBuyerFeeAsset_a, makerSellerFeeAsset_a, takerBuyerFeeAsset_a, takerSellerFeeAsset_a, makerBuyerFeeAsset_b, makerSellerFeeAsset_b,
      takerBuyerFeeAsset_b, takerSellerFeeAsset_b;
  // end: only applicable to paper trade and backtest

  // start: only applicable to backtest
  TimePoint historicalMarketDataStartDateTp{std::chrono::seconds{0}}, historicalMarketDataEndDateTp{std::chrono::seconds{0}};
  std::string historicalMarketDataDirectory, historicalMarketDataFilePrefix, historicalMarketDataFileSuffix;
  // end: only applicable to backtest

  // asynchronicity of messaging management
  int numOrdersWaitingCancelSell_a{}, numOrdersWaitingCancelBuy_a{}, numOrdersWaitingCancelSell_b{}, numOrdersWaitingCancelBuy_b{};

  // init to -1 to go through the first iteration
  std::string lastCancelledOrderIdSell_a = "-1";
  std::string lastCancelledOrderIdBuy_a = "-1";
  std::string lastCancelledOrderIdSell_b = "-1";
  std::string lastCancelledOrderIdBuy_b = "-1";

  // end asynchronicity of messaging management

  // check for message rate limits -> aggregated between the two legs...

  // for bitmex:
  // Requests to our REST API are rate limited by one or more limiters in a layered approach. The limiters implement a Token Bucket mechanism and tokens are
  // continuously refilled. Currently, there are two limiters in place: 120 requests per minute on all routes (reduced to 30 when unauthenticated) ->
  // longLimiter 10 requests per second on certain routes (see below) -> shortLimiter

  // rem. don't seggregate by products; consider the most constrained one...
  // int shortLimiter{}, longLimiter{}, shortRateLimit{}, longRateLimit{};
  // conncection limits for binance
  // https://dev.binance.vision/t/about-websocket-limitations-per-second/14096
  bool isShortLimiter = false;
  bool isLongLimiter = false;

  // for BITMEX
  int shortTimeLimiter = 1;    // number of sec == 1[s] -> 5 req see deribit x2
  int longTimeLimiter = 3600;  // 3600s == 1 hours -> 18000 req see deribit -> 2x
  // int shortTimeRateLimit = 10; // 10 requests max for shortLimiter sec
  // int longTimeRateLimit = 120; // 120 requests max for longLimiter sec

  int shortTimeRateLimit = 10;  // 10 requests max for shortLimiter sec

  // Binance Futures provides rate limit adjustment flexibility via a volume-based tier system. The default rate limit per IP is 2,400/min,
  // and the default order limit per account/sub-account is 1,200/min.
  int longTimeRateLimit = 36000;  // 120 requests max for longLimiter sec

  // keep track -> don't change
  bool isShortTimeLimiterLimitBreached = false;
  bool isLongTimeLimiterLimitBreached = false;

  int shortTimeIntervalNumberOfRequest = 0;  // e.g. number of request during that sec for BITMEX
  int longTimeIntervalNumberOfRequest = 0;   // e.g. number of request during that hour for BITMEX

  // init clock running clock in Tp
  TimePoint previousShortTimePeriodTp = UtilTime::now();
  TimePoint previousLongTimePeriodTp = UtilTime::now();

  // backtest (uncomment two lines above for production or paper)
  // bool isInit = false;
  // TimePoint previousShortTimePeriodTp{std::chrono::seconds{0}}, previousLongTimePeriodTp{std::chrono::seconds{0}};

  // sampler to the second
  long previousCrossingMicro = 0;
  long previousExportMicro = 0;
  long previousLockRelease = 0;
  long previousSyncMicro = 0;         // check positions
  long previousSyncBalanceMicro = 0;  // check balance

  // int previousCrossing = UtilTime::getUnixTimestamp(UtilTime::now());
  TimePoint nowMessageTp{};            // UtilTime::now(); // = UtilTime::now();
  int deltaCrossingTime = 1000000;     // number of micro sec between to occurences (beware mkt message rate...) == 1 sec
  int deltaPerfExport = 90000000;      // makes 90 sec + 1 (see < condition) = every 10 sec min a mysql export may occurs
  int deltaLockRelease = 270000000;    // every 270 sec (==3xdeltaPerfExport)
  int deltaSyncPortfolio = 600000000;  // sync portfolio 10 every minute + 1 sec
  long deltaSyncBalance = 4.32e10;     // every 12 hours //3.6e9; // every hour
  // int deltaSyncPortfolio = 100000000;

  // std::string previousSamplingSec="-1"; // at init
  // std::string currentSamplingSec{};
  //

  bool isCancelAllOpenOrders_a = true;
  bool isCancelAllOpenOrders_b = true;

  // mysql variables
  sql::Driver* driver;
  sql::Connection* con;
  sql::PreparedStatement* pstmt;
  sql::PreparedStatement* pstmt_1;
  sql::PreparedStatement* pstmt_2;
  sql::PreparedStatement* pstmt_3;
  sql::ResultSet* res;

 protected:
  long getUnixTimestampMicro(const TimePoint& tp) {
    auto now_ms = std::chrono::time_point_cast<std::chrono::microseconds>(tp);
    auto epoch = now_ms.time_since_epoch();
    auto value = std::chrono::duration_cast<std::chrono::microseconds>(epoch);
    long duration = value.count();

    return duration;
  }

  TimePoint tpnow() {
    TimePoint now{};
    if (this->tradingMode == TradingMode::BACKTEST) {
      now = nowMessageTp;
    } else {
      // this->nowTp = tpnow();
      now = std::chrono::system_clock::now();
    }
    // auto now = std::chrono::system_clock::now();
    // return TimePoint(now);
    return now;
  }

  // process portfolio sync
  virtual void processEventSyncPortfolio(const Event& event, Session* session, std::vector<Request>& requestList, const TimePoint& messageTime) {
    long currentTimeMicro = getUnixTimestampMicro(messageTime);
    const auto& messageTimeReceivedISO = UtilTime::getISOTimestamp(messageTime);

    //
    // int nowSec = UtilTime::getUnixTimestamp(UtilTime::now());
    // int previousCrossing = UtilTime::getUnixTimestamp(UtilTime::now());
    // int deltaCrossingTime = 60; // number of sec between to occurences (beware mkt message rate...)

    // every 10min check for account sync (ev. deconnection)
    if (this->previousSyncMicro + deltaSyncPortfolio < currentTimeMicro) {
      // reset states
      this->previousSyncMicro = currentTimeMicro;

      APP_LOGGER_INFO("#####################################################################################################");
      APP_LOGGER_INFO("##### Check or Sync Exchange position with internal position");
      APP_LOGGER_INFO("##### current crossing: " + messageTimeReceivedISO);

      this->getAccountPositions(event, session, requestList, messageTime, "prodA");
      this->getAccountPositions(event, session, requestList, messageTime, "prodB");
    }

    // every 12 hours (beware message rate limit...) check for account balance (pnl validation)
    // if((this->previousSyncBalanceMicro + (12*6*deltaSyncPortfolio)) < currentTimeMicro) {

    if ((this->previousSyncBalanceMicro + deltaSyncBalance) < currentTimeMicro) {
      // reset states
      this->previousSyncBalanceMicro = currentTimeMicro;

      APP_LOGGER_INFO("#####################################################################################################");
      APP_LOGGER_INFO("##### Account balance information for further validation.");
      APP_LOGGER_INFO("##### current crossing: " + messageTimeReceivedISO);

      this->getAccountBalances(event, session, requestList, messageTime, messageTimeReceivedISO, "prodA");
      this->getAccountBalances(event, session, requestList, messageTime, messageTimeReceivedISO, "prodB");
    }
  }

  // virtual void processEventExportPerf(const Event& event, Session* session, std::vector<Request>& requestList) {
  virtual void processEventExportPerf(const Event& event, Session* session, const TimePoint& messageTime) {
    // const auto& firstMessage = event.getMessageList().at(0);

    // const auto& messageTimeReceived = firstMessage.getTimeReceived();
    // const auto& messageTimeReceivedISO = UtilTime::getISOTimestamp(messageTimeReceived);

    // sampling to the second to prevent excess export to mysql rate
    // TODO: CHECK INTERVAL...POSSIBLY USE SEC INCREMENT...
    // int nowInt = UtilTime::getUnixTimestamp(nowTp);

    // keep track of time
    long currentTimeMicro = getUnixTimestampMicro(messageTime);

    if (this->previousLockRelease + deltaLockRelease < currentTimeMicro) {
      this->previousLockRelease = currentTimeMicro;

      APP_LOGGER_DEBUG("##### Release hedge locks in case taker order message was not recieved or executed...");
      APP_LOGGER_DEBUG("##### Release cancel locks in case cancel response was not recieved...");
      APP_LOGGER_DEBUG("##### Beware of aligning positions...");
      this->is_lock_hedged_a = false;
      this->is_lock_hedged_b = false;

      this->is_lock_cancelled_a = false;
      this->is_lock_cancelled_b = false;
    }

    if (this->previousExportMicro + deltaPerfExport < currentTimeMicro) {
      // database export
      APP_LOGGER_DEBUG("#####################################################################################################");
      APP_LOGGER_DEBUG("##### Pre-Export Perf");
      APP_LOGGER_DEBUG("##### currentSamplingMicro: " + std::to_string(currentTimeMicro));
      APP_LOGGER_DEBUG("##### previousExportMicro: " + std::to_string(this->previousExportMicro));
      APP_LOGGER_DEBUG("#####################################################################################################");

      this->previousExportMicro = currentTimeMicro;

      APP_LOGGER_DEBUG("#####################################################################################################");
      APP_LOGGER_DEBUG("# Export balance for the two legs.");

      APP_LOGGER_DEBUG("# Base balance A: " + std::to_string(this->baseBalance_a));
      APP_LOGGER_DEBUG("# Base balance B: " + std::to_string(this->baseBalance_b));

      APP_LOGGER_DEBUG("# Quote balance A: " + std::to_string(this->quoteBalance_a));
      APP_LOGGER_DEBUG("# Quote balance B: " + std::to_string(this->quoteBalance_b));
      APP_LOGGER_DEBUG("#####################################################################################################");

      APP_LOGGER_DEBUG("best Bid price a: " + this->bestBidPrice_a);
      APP_LOGGER_DEBUG("best Ask price a: " + this->bestAskPrice_a);
      APP_LOGGER_DEBUG("best Bid price b: " + this->bestBidPrice_b);
      APP_LOGGER_DEBUG("best Ask price b: " + this->bestAskPrice_b);
      APP_LOGGER_DEBUG("# mid-price A: " + std::to_string(this->midPriceA));
      APP_LOGGER_DEBUG("# mid-price B: " + std::to_string(this->midPriceB));

      // One has to be sure that after the first 10 sec at bid-ask prices are available...
      if (!this->bestBidPrice_a.empty() && !this->bestAskPrice_a.empty() && !this->bestBidPrice_b.empty() && !this->bestAskPrice_b.empty()) {
        // double midPriceA = (std::stod(this->bestBidPrice_a)+std::stod(this->bestAskPrice_a))/2;
        // double midPriceB = (std::stod(this->bestBidPrice_b)+std::stod(this->bestAskPrice_b))/2;

        double totBalanceA = this->quoteBalance_a + this->baseBalance_a * this->midPriceA;
        double totBalanceB = this->quoteBalance_b + this->baseBalance_b * this->midPriceB;

        APP_LOGGER_DEBUG("# Total balance A: " + std::to_string(totBalanceA));
        APP_LOGGER_DEBUG("# Total balance B: " + std::to_string(totBalanceB));

        APP_LOGGER_DEBUG("#####################################################################################################");

        // export performance to mysql db

        pstmt->setString(1, this->trader_id.c_str());
        pstmt->setDouble(2, this->baseBalance_a);
        pstmt->setDouble(3, this->baseBalance_b);
        pstmt->setDouble(4, this->quoteBalance_a);
        pstmt->setDouble(5, this->quoteBalance_b);
        pstmt->setDouble(6, std::stod(this->bestBidPrice_a));
        pstmt->setDouble(7, std::stod(this->bestAskPrice_a));
        pstmt->setDouble(8, std::stod(this->bestBidPrice_b));
        pstmt->setDouble(9, std::stod(this->bestAskPrice_b));
        pstmt->setDouble(10, totBalanceA);
        pstmt->setDouble(11, totBalanceB);
        pstmt->setDouble(12, totBalanceA + totBalanceB);

        pstmt->executeUpdate();  // insert into db

        // update state
        // pstmt_2 = con->prepareStatement("UPDATE states SET BaseBalanceA=?, BaseBalanceB=?, QuoteBalanceA=?, QuoteBalanceB=?, TheoreticalPrice=? WHERE
        // TraderId='"+this->trader_id+"';");
        pstmt_2->setDouble(1, this->baseBalance_a);
        pstmt_2->setDouble(2, this->baseBalance_b);
        pstmt_2->setDouble(3, this->quoteBalance_a);
        pstmt_2->setDouble(4, this->quoteBalance_b);
        pstmt_2->setDouble(5, this->theoreticalPrice);

        int nbUpdatedRows = pstmt_2->executeUpdate();  // update db
      }

      // legacy
      // this->getAccountBalances(event, session, requestList, messageTimeReceived, messageTimeReceivedISO, "prodA");
      // this->getAccountBalances(event, session, requestList, messageTimeReceived, messageTimeReceivedISO, "prodB");

      // const auto& baseBalanceDecimalNotation =
      // Decimal(UtilString::printDoubleScientific(productId=="prodA"?this->baseBalance_a:this->baseBalance_b)).toString(); const auto&
      // quoteBalanceDecimalNotation = Decimal(UtilString::printDoubleScientific(productId=="prodA"?this->quoteBalance_a:this->quoteBalance_b)).toString();

      // this->isAccountBalanceUpdatedA = false;
      // this->isAccountBalanceUpdatedB = false;
    }
  }

  // REM : THE CORRELATION_ID SHOULD BE UNIQUE THOURHG ALL SUBSCRIPTIONS
  //       LET S SET: Subscription fields: MARKET_DEPTH _ prodA or prodB
  //       THIS WAY WE COULD RETRIEVE THE SUBSCRIPTION CHANNEL AS WELL AS THE PRODUCT ID
  virtual void createSubscriptionList(std::vector<Subscription>& subscriptionList) {
    // market depth -> works -> for backtest the depth is all recorded depth
    {
      // swl incorporate backtest validation and simulated processes
      // that function was originally only called in paper or live mode
      std::string options;
      if (this->tradingMode == TradingMode::LIVE) {
        options += std::string(CCAPI_MARKET_DEPTH_MAX) + "=1";
      } else if (this->tradingMode == TradingMode::PAPER) {
        if (this->appMode == AppMode::MARKET_MAKING) {
          options += std::string(CCAPI_MARKET_DEPTH_MAX) + "=1";
        } else if (this->appMode == AppMode::SINGLE_ORDER_EXECUTION) {
          options += std::string(CCAPI_MARKET_DEPTH_MAX) + "=1000";
        }
      }

      // input stream updated every sec...
      if (!this->enableUpdateOrderBookTickByTick) {
        options += "&" + std::string(CCAPI_CONFLATE_INTERVAL_MILLISECONDS) + "=" + std::to_string(this->clockStepMilliseconds) + "&" +
                   CCAPI_CONFLATE_GRACE_PERIOD_MILLISECONDS + "=0";
      }

      subscriptionList.emplace_back(this->exchange_a, this->instrumentWebsocket_a, CCAPI_MARKET_DEPTH, options, std::string(CCAPI_MARKET_DEPTH) + "#prodA",
                                    this->credential_a);

      subscriptionList.emplace_back(this->exchange_b, this->instrumentWebsocket_b, CCAPI_MARKET_DEPTH, options, std::string(CCAPI_MARKET_DEPTH) + "#prodB",
                                    this->credential_b);

      // legacy implementation for a single product
      // subscriptionList.emplace_back(this->exchange, this->instrumentWebsocket, CCAPI_MARKET_DEPTH, options,
      //                            PUBLIC_SUBSCRIPTION_DATA_MARKET_DEPTH_CORRELATION_ID);
    }
    // end market depth

    // public trade feeds -> works for binance...credentials issues for deribit
    {
      std::string field_a = CCAPI_TRADE;
      std::string field_b = CCAPI_TRADE;
      // std::string options="";
      if (this->exchange_a.rfind("binance", 0) == 0 || this->exchange_a.rfind("binance-coin-futures", 0) == 0) {
        field_a = CCAPI_AGG_TRADE;
      }
      if (this->exchange_b.rfind("binance", 0) == 0 || this->exchange_b.rfind("binance-coin-futures", 0) == 0) {
        field_b = CCAPI_AGG_TRADE;
      }

      subscriptionList.emplace_back(this->exchange_a, this->instrumentWebsocket_a, field_a, "", std::string(CCAPI_TRADE) + "#prodA", this->credential_a);
      subscriptionList.emplace_back(this->exchange_b, this->instrumentWebsocket_b, field_b, "", std::string(CCAPI_TRADE) + "#prodB", this->credential_b);

      // legacy implementation for a single product
      // subscriptionList.emplace_back(this->exchange, this->instrumentWebsocket, field, "", PUBLIC_SUBSCRIPTION_DATA_TRADE_CORRELATION_ID);
    }

    {
      // if (this->tradingMode == TradingMode::LIVE) {
      subscriptionList.emplace_back(this->exchange_a, this->instrumentWebsocket_a, CCAPI_EM_PRIVATE_TRADE, "", std::string(CCAPI_EM_PRIVATE_TRADE) + "#prodA",
                                    this->credential_a);
      subscriptionList.emplace_back(this->exchange_b, this->instrumentWebsocket_b, CCAPI_EM_PRIVATE_TRADE, "", std::string(CCAPI_EM_PRIVATE_TRADE) + "#prodB",
                                    this->credential_b);
      //}
      // subscriptionList.emplace_back(this->exchange_a, this->instrumentWebsocket_a, CCAPI_EM_PRIVATE_TRADE, "",
      //                                std::string(CCAPI_EM_PRIVATE_TRADE)+"#prodA");
      // subscriptionList.emplace_back(this->exchange_a, this->instrumentWebsocket_a, CCAPI_EM_ORDER_UPDATE, "",
      //                                std::string(CCAPI_EM_ORDER_UPDATE)+"#prodA", credential_a);

      // std::map<std::string, std::string> credential_a;
      // credential_a = {{"CCAPI_BINANCE_COIN_FUTURES_API_KEY", "03197b95f6e6572d45004f5d2d30e3d4d159fce576d427a0cf1a1a85cc582150"},
      //                 {"CCAPI_BINANCE_COIN_FUTURES_API_SECRET", "601e9b74fe6c21d3b22f876472cba6c1d59fd4b16338212895b6e0c7899e02e0"}};

      // subscriptionList.emplace_back(this->exchange_a, this->instrumentWebsocket_a, CCAPI_EM_ORDER_UPDATE, "",
      //                               std::string(CCAPI_EM_ORDER_UPDATE)+"#prodA", credential_a);

      // legacy
      // Subscription(std::string exchange, std::string instrument, std::string field, std::string options = "", std::string correlationId = "",
      //         std::map<std::string, std::string> credential = {})
    }
    {
      // if (this->tradingMode == TradingMode::LIVE) {
      subscriptionList.emplace_back(this->exchange_a, this->instrumentWebsocket_a, CCAPI_EM_ORDER_UPDATE, "", std::string(CCAPI_EM_ORDER_UPDATE) + "#prodA",
                                    this->credential_a);
      subscriptionList.emplace_back(this->exchange_b, this->instrumentWebsocket_b, CCAPI_EM_ORDER_UPDATE, "", std::string(CCAPI_EM_ORDER_UPDATE) + "#prodB",
                                    this->credential_b);
      //}
    }
    /*
    {
      if (this->tradingMode == TradingMode::LIVE) {
        subscriptionList.emplace_back(this->exchange_a, this->instrumentWebsocket_a, CCAPI_EM_PRIVATE_TRADE, "",
                                      std::string(CCAPI_EM_PRIVATE_TRADE)+"#prodA");
        subscriptionList.emplace_back(this->exchange_a, this->instrumentWebsocket_a, CCAPI_EM_ORDER_UPDATE, "",
                                      std::string(CCAPI_EM_ORDER_UPDATE)+"#prodA");

        subscriptionList.emplace_back(this->exchange_b, this->instrumentWebsocket_b, CCAPI_EM_PRIVATE_TRADE, "",
                                      std::string(CCAPI_EM_PRIVATE_TRADE)+"#prodB");
        subscriptionList.emplace_back(this->exchange_b, this->instrumentWebsocket_b, CCAPI_EM_ORDER_UPDATE, "",
                                      std::string(CCAPI_EM_ORDER_UPDATE)+"#prodB");

      // legacy implementation for a single product
      //if (this->tradingMode == TradingMode::LIVE) {
      //  subscriptionList.emplace_back(this->exchange, this->instrumentWebsocket, std::string(CCAPI_EM_PRIVATE_TRADE) + "," + CCAPI_EM_ORDER_UPDATE, "",
      //                                PRIVATE_SUBSCRIPTION_DATA_CORRELATION_ID);
      }
    }
    */
    // log subscription information
    for (const auto& subscription : subscriptionList) {
      auto corrIdSub = subscription.getCorrelationId();
      APP_LOGGER_INFO("#####################################################################################################");
      APP_LOGGER_INFO("##### SUBSCRIPTION :" + corrIdSub);
      APP_LOGGER_INFO("##### SUBSCRIPTION CONTENT:" + subscription.toString());
      APP_LOGGER_INFO("#####################################################################################################");
    }
  }

  virtual void postProcessMessageMarketDataEventMarketDepth(const Event& event, Session* session, std::vector<Request>& requestList, const Message& message,
                                                            const TimePoint& messageTime) {
    // const std::string& messageTimeISO = UtilTime::getISOTimestamp(messageTime);

    // sampling to the second to prevent excess massages rate
    long currentTimeMicro = getUnixTimestampMicro(this->tpnow());

    //    } else {
    //  this->nowTp = tpnow();
    //}

    // init previous crossing micro
    // if(this->previousCrossingMicro==0) {
    //  this->previousCrossingMicro= getUnixTimestampMicro(nowTp);
    //}

    // 2023-09-13T06:09:08.789000000Z
    // this->currentSamplingSec = messageTimeISO.substr(18, 1);
    // this->currentSamplingSec = messageTimeISO.substr(17, 2);

    //      int intervalStart = UtilTime::getUnixTimestamp(messageTime) / this->adverseSelectionGuardMarketDataSampleIntervalSeconds *
    //                      this->adverseSelectionGuardMarketDataSampleIntervalSeconds;

    // sampler to the second
    // int nowSec = UtilTime::getUnixTimestamp(UtilTime::now());
    // int previousCrossing = UtilTime::getUnixTimestamp(UtilTime::now());
    // int deltaCrossingTime = 60; // number of sec between to occurences (beware mkt message rate...)

    /*
    APP_LOGGER_DEBUG("#####################################################################################################");
    APP_LOGGER_DEBUG("##### Pre-onQuote");
    APP_LOGGER_DEBUG("##### currentSamplingMicro: "+ std::to_string(currentTimeMicro));
    APP_LOGGER_DEBUG("##### previousSamplingMicro: "+std::to_string(this->previousCrossingMicro));

    if ((!this->bestBidPrice_a.empty() && !this->bestAskPrice_a.empty() &&
      !this->bestBidPrice_b.empty() && !this->bestAskPrice_b.empty())) {
      APP_LOGGER_DEBUG("best Bid price a: " + this->bestBidPrice_a);
      APP_LOGGER_DEBUG("best Ask price a: " + this->bestAskPrice_a );
      APP_LOGGER_DEBUG("best Bid price b: " + this->bestBidPrice_b );
      APP_LOGGER_DEBUG("best Ask price b: " + this->bestAskPrice_b );
    } else {
      APP_LOGGER_DEBUG("##### At least one best-bid-ask empty for one product");
    }
    */

    APP_LOGGER_DEBUG("#####################################################################################################");

    // if(this->previousCrossing < this->currentSamplingSec) {

    // need to freeze for delta crossing time in case a taker order was sent to prevent the quoter to delete the in-flight order...

    // if(this->previousCrossingMicro+deltaCrossingTime<currentTimeMicro) {
    // if(true) {

    // update time increment to prevent multiple operations happening the same sec
    // this->previousCrossingMicro=currentTimeMicro;

    // this->previousSamplingSec = this->currentSamplingSec;

    const auto& messageTimeReceived = message.getTimeReceived();
    const std::string& messageTimeReceivedISO = UtilTime::getISOTimestamp(messageTimeReceived);

    // retrieve message correlation id and filter out product id
    const auto& correlationIdList = message.getCorrelationIdList();

    auto correlationIdTocken = UtilString::split(correlationIdList.at(0), '#');
    std::string correlationId = correlationIdTocken.at(0);
    std::string productId = correlationIdTocken.at(1);

    // ckeck for rate limit breaches
    // if(!this->isShortTimeLimiterLimitBreached && !this->isLongTimeLimiterLimitBreached) {
    //
    // onQuote method for further quoting and execution
    //

    // make sure that at least one of the new top of the book mkt quote is different from the previous one.
    // If not, don't trigger the onQuote method...quoting & message rate management...

    // eventually trigger the onQuote method...
    if ((!this->bestBidPrice_a.empty() && !this->bestAskPrice_a.empty() && !this->bestBidPrice_b.empty() && !this->bestAskPrice_b.empty()) &&
        (this->bestBidPrice_a != this->newBestBidPrice_a || this->bestAskPrice_a != this->newBestAskPrice_a ||
         this->bestBidPrice_b != this->newBestBidPrice_b || this->bestAskPrice_b != this->newBestAskPrice_b)) {
      // set newBestBid/AskPrice price with the new bbo mkt quote
      this->newBestBidPrice_a = this->bestBidPrice_a;
      this->newBestAskPrice_a = this->bestAskPrice_a;
      this->newBestBidPrice_b = this->bestBidPrice_b;
      this->newBestAskPrice_b = this->bestAskPrice_b;

      // set sizes
      this->newBestBidSize_a = this->bestBidSize_a;
      this->newBestAskSize_a = this->bestAskSize_a;
      this->newBestBidSize_b = this->bestBidSize_b;
      this->newBestAskSize_b = this->bestAskSize_b;

      // Implement SWL quoter
      APP_LOGGER_DEBUG("##########################################################");
      APP_LOGGER_DEBUG("onQuote method called");

      // APP_LOGGER_DEBUG("##########################################################");
      // APP_LOGGER_DEBUG("Rate limit status:");

      /*
      if(this->isLongTimeLimiterLimitBreached) {
        APP_LOGGER_DEBUG("isLongTimeLimiterLimitBreached: true");
      } else {
        APP_LOGGER_DEBUG("isLongTimeLimiterLimitBreached: false");
      }

      if(this->isShortTimeLimiterLimitBreached) {
        APP_LOGGER_DEBUG("isShortTimeLimiterLimitBreached: true");
      } else {
        APP_LOGGER_DEBUG("isShortTimeLimiterLimitBreached: false");
      }
      */

      APP_LOGGER_DEBUG("##########################################################");
      APP_LOGGER_DEBUG("new Bid price a: " + this->newBestBidPrice_a);
      APP_LOGGER_DEBUG("new Bid size a: " + this->newBestBidSize_a);
      APP_LOGGER_DEBUG("new Ask price a: " + this->newBestAskPrice_a);
      APP_LOGGER_DEBUG("new Ask size a: " + this->newBestAskSize_a);
      APP_LOGGER_DEBUG("new Bid price b: " + this->newBestBidPrice_b);
      APP_LOGGER_DEBUG("new Bid size b: " + this->newBestBidSize_b);
      APP_LOGGER_DEBUG("new Ask price b: " + this->newBestAskPrice_b);
      APP_LOGGER_DEBUG("new Ask size b: " + this->newBestAskSize_b);
      APP_LOGGER_DEBUG("##########################################################");

      // CROSS ARBITRAGE ALGORITHM
      double marginInVector_0 = -this->normalizedSignalVector_1;
      double marginInVector_1 = this->normalizedSignalVector_0;
      double marginInvSlope = marginInVector_1 / marginInVector_0;

      APP_LOGGER_DEBUG("##### marginInVector_0:" + std::to_string(marginInVector_0));
      APP_LOGGER_DEBUG("##### marginInVector_1:" + std::to_string(marginInVector_1));
      APP_LOGGER_DEBUG("##### marginInvSlope:" + std::to_string(marginInvSlope));

      // TODO: compute signal price, determine whether positively or negatively correlated pair
      double signalPrice_0{}, signalPrice_1{};

      signalPrice_0 = std::stod(this->newBestBidPrice_a) * this->normalizedSignalVector_0 + std::stod(this->newBestAskPrice_b) * this->normalizedSignalVector_1;
      signalPrice_1 = std::stod(this->newBestAskPrice_a) * this->normalizedSignalVector_0 + std::stod(this->newBestBidPrice_b) * this->normalizedSignalVector_1;

      APP_LOGGER_DEBUG("##### normalizedSignalVector_0:" + std::to_string(this->normalizedSignalVector_0));
      APP_LOGGER_DEBUG("##### normalizedSignalVector_1:" + std::to_string(this->normalizedSignalVector_1));
      APP_LOGGER_DEBUG("##### signalPrice_0:" + std::to_string(signalPrice_0));
      APP_LOGGER_DEBUG("##### signalPrice_1:" + std::to_string(signalPrice_1));

      // check if theoretical price was initialized (i.e. theoretical price at t=0)
      if (FP_ZERO == std::fpclassify(this->theoreticalPrice)) {
        this->theoreticalPrice = (signalPrice_0 + signalPrice_1) / 2;
      }

      APP_LOGGER_DEBUG("##### theoretical price before potential move price:" + std::to_string(this->theoreticalPrice));

      // check if a crossing occurs
      double moveTheoPrice = 0.0;

      //
      // dbg crossings
      //
      /*
      double marginLowerX = -this->normalizedSignalVector_0 * (this->margin - this->theoreticalPrice);
      double marginLowerY = -this->normalizedSignalVector_1 * (this->margin - this->theoreticalPrice);
      double marginUpperX = this->normalizedSignalVector_0 * (this->margin + this->theoreticalPrice);
      double marginUpperY = this->normalizedSignalVector_1 * (this->margin + this->theoreticalPrice);

      double yInterceptMarginLower = marginLowerY - marginInVector_1 / marginInVector_0 * marginLowerX;
      double yInterceptMarginUpper = marginUpperY - marginInVector_1 / marginInVector_0 * marginUpperX;

      APP_LOGGER_DEBUG("##### marginInVector_0: " + std::to_string(marginInVector_0));
      APP_LOGGER_DEBUG("##### marginInVector_1: " + std::to_string(marginInVector_1));

      // freeze leg A and quote leg B
      this->aSellLevelB = marginInvSlope * std::stod(this->newBestAskPrice_a) + yInterceptMarginLower;
      this->aBuyLevelB = marginInvSlope * std::stod(this->newBestBidPrice_a) + yInterceptMarginUpper;

      // freeze leg B and quote leg A
      this->aSellLevelA = (std::stod(this->newBestAskPrice_b) - yInterceptMarginUpper) / marginInvSlope;
      this->aBuyLevelA = (std::stod(this->newBestBidPrice_b) - yInterceptMarginLower) / marginInvSlope;
      */

      /*
      if(this->openBuyOrder_a) APP_LOGGER_DEBUG("##### this->openBuyOrder_a.get().limitPrice:"+ this->openBuyOrder_a.get().limitPrice.toString());
      if(this->openSellOrder_a) APP_LOGGER_DEBUG("##### this->openSellOrder_a.get().limitPrice:"+ this->openSellOrder_a.get().limitPrice.toString());
      if(this->openBuyOrder_b) APP_LOGGER_DEBUG("##### this->openBuyOrder_b.get().limitPrice:"+ this->openBuyOrder_b.get().limitPrice.toString());
      if(this->openSellOrder_b) APP_LOGGER_DEBUG("##### this->openSellOrder_b.get().limitPrice:"+ this->openSellOrder_b.get().limitPrice.toString());

      if(this->openBuyOrder_a && Decimal(this->newBestAskPrice_a) <= this->openBuyOrder_a.get().limitPrice) {
      //if(this->anOldBuyLevelA >= std::stod(this->newBestAskPrice_a)) {
        APP_LOGGER_DEBUG("##### Crossing - 1");
      }

      if(this->openSellOrder_a && Decimal(this->newBestBidPrice_a) >= this->openSellOrder_a.get().limitPrice) {
      //if(this->anOldSellLevelA <= std::stod(this->newBestBidPrice_a)) {
        APP_LOGGER_DEBUG("##### Crossing - 2");
      }

      if(this->openBuyOrder_b && Decimal(this->newBestAskPrice_b) <= this->openBuyOrder_b.get().limitPrice) {
      //if(this->anOldBuyLevelB >= std::stod(this->newBestAskPrice_b)) {
        APP_LOGGER_DEBUG("##### Crossing - 3");
      }

      if(this->openSellOrder_b && Decimal(this->newBestBidPrice_b) >= this->openSellOrder_b.get().limitPrice) {
      //if(this->anOldSellLevelB <= std::stod(this->newBestBidPrice_b)) {
        APP_LOGGER_DEBUG("##### Crossing - 4");
      }

      APP_LOGGER_DEBUG("(signalPrice_0 - this->theoreticalPrice ) / this->margin: "+ std::to_string((signalPrice_0 - this->theoreticalPrice ) / this->margin));
      APP_LOGGER_DEBUG("(this->theoreticalPrice - signalPrice_1 ) / this->margin: "+ std::to_string((this->theoreticalPrice - signalPrice_1 ) / this->margin));
      */
      // end debug

      // upward crossing
      // if ((signalPrice_0 - this->theoreticalPrice ) / this->margin > (1 - this->epsilon)){// || this->isUpwardCrossing) {
      bool upwardCrossingCondition = (signalPrice_0 - this->theoreticalPrice) / (this->margin * this->skewUpperBound) > (1 - this->epsilon);

      // dbg

      if (this->isUpwardCrossing) {
        APP_LOGGER_DEBUG("##########################################################");
        APP_LOGGER_DEBUG("##### isUpwardCrossing");
        APP_LOGGER_DEBUG("##########################################################");
      }
      if (this->lowerLimitReached_b) {
        APP_LOGGER_DEBUG("##########################################################");
        APP_LOGGER_DEBUG("##### this->lowerLimitReached_b");
        APP_LOGGER_DEBUG("##########################################################");
      }
      if (upwardCrossingCondition) {
        APP_LOGGER_DEBUG("##########################################################");
        APP_LOGGER_DEBUG("##### upwardCrossingCondition");
        APP_LOGGER_DEBUG("##########################################################");
      }
      if (this->upperLimitReached_a) {
        APP_LOGGER_DEBUG("##########################################################");
        APP_LOGGER_DEBUG("##### this->upperLimitReached_a");
        APP_LOGGER_DEBUG("##########################################################");
      }
      // end dbg

      /*  int timeLimit = 4; // skew quotes if two occurences take place within a 4 sec period
        int nbTickSinceDownCrossing=0;
        int nbTickSinceUpCrossing=0;
        double skewUpperBound=1;
        double skewLowerBound=1;*/

      // skewing logic: keep track of lambda and A (i.e. mkt intensity & volatility)
      this->nbTickSinceDownCrossing++;
      this->nbTickSinceUpCrossing++;

      // if(tmp==411) {
      //   APP_LOGGER_DEBUG("####");
      // }
      // tmp++;

      // BEWARE: Still retain condition (i.e. upwardCrossingCondition) in case the mkt moved and
      //         for some reason our quote(s) is not hit/lifted...remark: removing condition should
      //         lead to the same result in backtest/paper...to be tested
      // if(this->isUpwardCrossing || (this->upperLimitReached_b && upwardCrossingCondition) || (this->lowerLimitReached_a && upwardCrossingCondition)) {
      if (this->isUpwardCrossing || upwardCrossingCondition) {
        // if(this->isUpwardCrossing || (this->upperLimitReached_b && upwardCrossingCondition) || (this->lowerLimitReached_a && upwardCrossingCondition)) {

        moveTheoPrice = ceil((signalPrice_0 - theoreticalPrice - this->margin + epsilon * this->margin) / this->stepback) * this->stepback;

        // first check intensity breach
        // double aNewSkewUpperBound=moveTheoPrice==0.0?1.0:abs(moveTheoPrice)/this->stepback;
        // this->skewUpperBound=std::max(this->skewUpperBound, aNewSkewUpperBound);

        // this->skewUpperBound=1.0;

        // if(this->skewUpperBound!=1.0) {
        //   APP_LOGGER_DEBUG(" Skewing_1 !!!!");
        // }

        // bucketting intensity of quotes arrival rate
        /*
        if(this->nbTickSinceUpCrossing<=180) {
          this->skewUpperBound=2;
          APP_LOGGER_DEBUG(" Skewing_2 !!!!");
        }
        if(this->nbTickSinceUpCrossing<=120) {
          this->skewUpperBound=3;
          APP_LOGGER_DEBUG(" Skewing_3 !!!!");
        }
        if(this->nbTickSinceUpCrossing<=60) {
          this->skewUpperBound=4;
          APP_LOGGER_DEBUG(" Skewing_4 !!!!");
        }
        */

        // if(this->nbTickSinceUpCrossing<=120) {
        //   this->skewUpperBound=2;
        //   APP_LOGGER_DEBUG(" Skewing_2 !!!!");
        // }
        if (this->nbTickSinceUpCrossing <= 90) {
          this->skewUpperBound = 2;
          APP_LOGGER_DEBUG(" Skewing_Up_2, nbTick:" + std::to_string(this->nbTickSinceUpCrossing));
        }
        if (this->nbTickSinceUpCrossing <= 60) {
          this->skewUpperBound = 3;
          APP_LOGGER_DEBUG(" Skewing_UP_3, nbTick:" + std::to_string(this->nbTickSinceUpCrossing));
        }
        if (this->nbTickSinceUpCrossing <= 30) {
          this->skewUpperBound = 4;
          APP_LOGGER_DEBUG(" Skewing_Up_4, nbTick:" + std::to_string(this->nbTickSinceUpCrossing));
        }

        // bucketting intensity of quotes arrival rate
        // if(this->nbTickSinceUpCrossing<8) {
        //  this->skewUpperBound= this->skewUpperBound*(8-this->nbTickSinceUpCrossing)/2;
        //  APP_LOGGER_DEBUG(" Skewing_2 !!!!");
        //}
        /*
        if(this->nbTickSinceUpCrossing>10 && this->nbTickSinceUpCrossing<=15) {
          this->skewUpperBound=3;
          APP_LOGGER_DEBUG(" Skewing_2 !!!!");
        }
        if(this->nbTickSinceUpCrossing>5 && this->nbTickSinceUpCrossing<=10) {
          this->skewUpperBound=4;
          APP_LOGGER_DEBUG(" Skewing_3 !!!!");
        }
        if(this->nbTickSinceUpCrossing>=0 && this->nbTickSinceUpCrossing<=5) {
          this->skewUpperBound=5;
          APP_LOGGER_DEBUG(" Skewing_4 !!!!");
        }
        */

        // if(this->nbTickSinceUpCrossing<this->timeLimit) {
        //   this->skewUpperBound=this->skewUpperBound*(this->timeLimit-this->nbTickSinceUpCrossing);
        //   APP_LOGGER_DEBUG(" Skewing_2 !!!!");
        // }

        // rescale skew state for other leg
        this->skewLowerBound = std::max(1.0, this->skewLowerBound - 1.0);

        APP_LOGGER_DEBUG("##########################################################");
        APP_LOGGER_DEBUG("# theoreticalPrice: " + std::to_string(theoreticalPrice));
        APP_LOGGER_DEBUG("# signalPrice_0: " + std::to_string(signalPrice_0));
        APP_LOGGER_DEBUG("# signalPrice_1: " + std::to_string(signalPrice_1));
        APP_LOGGER_DEBUG("# margin: " + std::to_string(this->margin));
        APP_LOGGER_DEBUG("# stepback: " + std::to_string(this->stepback));
        APP_LOGGER_DEBUG("##########################################################");

        APP_LOGGER_DEBUG("##########################################################");
        APP_LOGGER_DEBUG("# Upward crossing  --- verbose the spread");
        APP_LOGGER_DEBUG("# [bid.a-ask.a] -> [" + this->newBestBidPrice_a + "-" + this->newBestAskPrice_a + "]");
        APP_LOGGER_DEBUG("# [bid.b-ask.b] -> [" + this->newBestBidPrice_b + "-" + this->newBestAskPrice_b + "]");
        APP_LOGGER_DEBUG("# skewUpperBound -> " + std::to_string(this->skewUpperBound));
        APP_LOGGER_DEBUG("# skewLowerBound -> " + std::to_string(this->skewLowerBound));
        APP_LOGGER_DEBUG("# nbTickSinceUpCrossing -> " + std::to_string(this->nbTickSinceUpCrossing));
        APP_LOGGER_DEBUG("##########################################################");

        // reset states
        this->nbTickSinceUpCrossing = 0;

        // adjust relative position
        this->relative_position += 1;

        // reset crossing flag
        this->isUpwardCrossing = false;
      }

      // downward crossing
      // if ((this->theoreticalPrice - signalPrice_1 ) / this->margin > (1 - this->epsilon)){// || this->isDownwardCrossing) {
      bool downwardCrossingCondition = (this->theoreticalPrice - signalPrice_1) / (this->margin * this->skewLowerBound) > (1 - this->epsilon);

      // dbg
      if (this->isDownwardCrossing) {
        APP_LOGGER_DEBUG("##########################################################");
        APP_LOGGER_DEBUG("##### isDownwardCrossing");
        APP_LOGGER_DEBUG("##########################################################");
      }
      if (this->upperLimitReached_b) {
        APP_LOGGER_DEBUG("##########################################################");
        APP_LOGGER_DEBUG("##### this->upperLimitReached_b");
        APP_LOGGER_DEBUG("##########################################################");
      }
      if (downwardCrossingCondition) {
        APP_LOGGER_DEBUG("##########################################################");
        APP_LOGGER_DEBUG("##### downwardCrossingCondition");
        APP_LOGGER_DEBUG("##########################################################");
      }
      if (this->lowerLimitReached_a) {
        APP_LOGGER_DEBUG("##########################################################");
        APP_LOGGER_DEBUG("##### this->lowerLimitReached_a");
        APP_LOGGER_DEBUG("##########################################################");
      }
      // end dbg

      if (this->isDownwardCrossing || downwardCrossingCondition) {
        // if(this->isDownwardCrossing  || (this->lowerLimitReached_b && downwardCrossingCondition) || (this->upperLimitReached_a && downwardCrossingCondition))
        // {// || this->lowerLimitReached_a || this->upperLimitReached_b) {

        moveTheoPrice = -ceil((theoreticalPrice - signalPrice_1 - this->margin + epsilon * this->margin) / this->stepback) * this->stepback;

        // first check intensity breach
        // double aNewSkewLowerBound=moveTheoPrice==0.0?1.0:abs(moveTheoPrice)/this->stepback;
        // this->skewLowerBound=std::max(this->skewLowerBound, aNewSkewLowerBound);

        // this->skewLowerBound=1.0;

        // if(this->skewLowerBound!=1.0) {
        //   APP_LOGGER_DEBUG(" Skewing_1 !!!!");
        // }

        // bucketting intensity of quotes arrival rate
        // aSkewUpperBound[i]=std::max(aSkewUpperBound[i], aSkewUpperBound[i-1])*(2*timeLimit-nbTicksSincePrevUpCrossingInt)/2;
        // if(this->nbTickSinceDownCrossing<8) {
        //  this->skewLowerBound= this->skewLowerBound*(8-this->nbTickSinceDownCrossing)/2;
        //  APP_LOGGER_DEBUG(" Skewing_2 !!!!");
        //}

        // if(this->nbTickSinceDownCrossing<=120) {
        //   this->skewLowerBound=2;
        //   APP_LOGGER_DEBUG(" Skewing_1 !!!!");
        // }
        if (this->nbTickSinceDownCrossing <= 90) {
          this->skewLowerBound = 2;
          APP_LOGGER_DEBUG(" Skewing_Down_2, nbTick:" + std::to_string(this->nbTickSinceDownCrossing));
        }
        if (this->nbTickSinceDownCrossing <= 60) {
          this->skewLowerBound = 3;
          APP_LOGGER_DEBUG(" Skewing_Down_3, nbTick:" + std::to_string(this->nbTickSinceDownCrossing));
        }
        if (this->nbTickSinceDownCrossing <= 30) {
          this->skewLowerBound = 4;
          APP_LOGGER_DEBUG(" Skewing_Down_4, nbTick:" + std::to_string(this->nbTickSinceDownCrossing));
        }

        /*
        if(this->nbTickSinceDownCrossing<=180) {
          this->skewLowerBound=2;
          APP_LOGGER_DEBUG(" Skewing_2 !!!!");
        }
        if(this->nbTickSinceDownCrossing<=120) {
          this->skewLowerBound=3;
          APP_LOGGER_DEBUG(" Skewing_3 !!!!");
        }
        if(this->nbTickSinceDownCrossing<=60) {
          this->skewLowerBound=4;
          APP_LOGGER_DEBUG(" Skewing_4 !!!!");
        }
        */

        /*
                    // bucketting intensity of quotes arrival rate
                    if(this->nbTickSinceDownCrossing>10 && this->nbTickSinceDownCrossing<=15) {
                      this->skewLowerBound=2;
                      APP_LOGGER_DEBUG(" Skewing_2 !!!!");
                    }
                    if(this->nbTickSinceDownCrossing>5 && this->nbTickSinceDownCrossing<=10) {
                      this->skewLowerBound=3;
                      APP_LOGGER_DEBUG(" Skewing_3 !!!!");
                    }
                    if(this->nbTickSinceDownCrossing>=0 && this->nbTickSinceDownCrossing<=5) {
                      this->skewLowerBound=4;
                      APP_LOGGER_DEBUG(" Skewing_4 !!!!");
                    }
         */
        // if(this->nbTickSinceDownCrossing<this->timeLimit) {
        //   this->skewLowerBound=this->skewLowerBound*(this->timeLimit-this->nbTickSinceDownCrossing);
        //   APP_LOGGER_DEBUG(" Skewing_2 !!!!");
        // }

        // reset states on the opposite side
        this->skewUpperBound = std::max(1.0, this->skewUpperBound - 1.0);

        APP_LOGGER_DEBUG("##########################################################");
        APP_LOGGER_DEBUG("# theoreticalPrice: " + std::to_string(theoreticalPrice));
        APP_LOGGER_DEBUG("# signalPrice_0: " + std::to_string(signalPrice_0));
        APP_LOGGER_DEBUG("# signalPrice_1: " + std::to_string(signalPrice_1));
        APP_LOGGER_DEBUG("# margin: " + std::to_string(this->margin));
        APP_LOGGER_DEBUG("# stepback: " + std::to_string(this->stepback));
        APP_LOGGER_DEBUG("##########################################################");

        APP_LOGGER_DEBUG("##########################################################");
        APP_LOGGER_DEBUG("# Downward crossing  --- verbose the spread");
        APP_LOGGER_DEBUG("# [bid.a-ask.a] -> [" + this->newBestBidPrice_a + "-" + this->newBestAskPrice_a + "]");
        APP_LOGGER_DEBUG("# [bid.b-ask.b] -> [" + this->newBestBidPrice_b + "-" + this->newBestAskPrice_b + "]");
        APP_LOGGER_DEBUG("# skewUpperBound -> " + std::to_string(this->skewUpperBound));
        APP_LOGGER_DEBUG("# skewLowerBound -> " + std::to_string(this->skewLowerBound));
        APP_LOGGER_DEBUG("# nbTickSinceDownCrossing -> " + std::to_string(this->nbTickSinceDownCrossing));
        APP_LOGGER_DEBUG("##########################################################");

        this->nbTickSinceDownCrossing = 0;

        // adjust relative position
        this->relative_position -= 1;

        // reset crossing flag
        this->isDownwardCrossing = false;
      }

      // update the theoretical price
      this->theoreticalPrice += moveTheoPrice;

      // quotes skew -> uncomment the two following lines to switch off skewing
      // this->skewLowerBound = 1;
      // this->skewUpperBound = 1;

      double marginLowerX = -this->normalizedSignalVector_0 * (this->margin * this->skewLowerBound - this->theoreticalPrice);
      double marginLowerY = -this->normalizedSignalVector_1 * (this->margin * this->skewLowerBound - this->theoreticalPrice);
      double marginUpperX = this->normalizedSignalVector_0 * (this->margin * this->skewUpperBound + this->theoreticalPrice);
      double marginUpperY = this->normalizedSignalVector_1 * (this->margin * this->skewUpperBound + this->theoreticalPrice);

      APP_LOGGER_DEBUG("##########################################################");
      APP_LOGGER_DEBUG("# recompute margin ");
      APP_LOGGER_DEBUG("# marginLowerX: " + std::to_string(marginLowerX));
      APP_LOGGER_DEBUG("# marginLowerY: " + std::to_string(marginLowerY));
      APP_LOGGER_DEBUG("# marginUpperX: " + std::to_string(marginUpperX));
      APP_LOGGER_DEBUG("# marginUpperY: " + std::to_string(marginUpperY));
      APP_LOGGER_DEBUG("##########################################################");

      double yInterceptMarginLower = marginLowerY - marginInVector_1 / marginInVector_0 * marginLowerX;
      double yInterceptMarginUpper = marginUpperY - marginInVector_1 / marginInVector_0 * marginUpperX;

      APP_LOGGER_DEBUG("##### marginInVector_0: " + std::to_string(marginInVector_0));
      APP_LOGGER_DEBUG("##### marginInVector_1: " + std::to_string(marginInVector_1));

      // freeze leg A and quote leg B
      this->aSellLevelB = marginInvSlope * std::stod(this->newBestAskPrice_a) + yInterceptMarginLower;
      this->aBuyLevelB = marginInvSlope * std::stod(this->newBestBidPrice_a) + yInterceptMarginUpper;

      // freeze leg B and quote leg A
      this->aSellLevelA = (std::stod(this->newBestAskPrice_b) - yInterceptMarginUpper) / marginInvSlope;
      this->aBuyLevelA = (std::stod(this->newBestBidPrice_b) - yInterceptMarginLower) / marginInvSlope;

      APP_LOGGER_DEBUG("##########################################################");
      APP_LOGGER_DEBUG("# not shifted ");
      APP_LOGGER_DEBUG("# aBuyLevelA: " + std::to_string(this->aBuyLevelA));
      APP_LOGGER_DEBUG("# aSellLevelA: " + std::to_string(this->aSellLevelA));
      APP_LOGGER_DEBUG("# aBuyLevelB: " + std::to_string(this->aBuyLevelB));
      APP_LOGGER_DEBUG("# aSellLevelB: " + std::to_string(this->aSellLevelB));
      APP_LOGGER_DEBUG("# skewUpperBound -> " + std::to_string(this->skewUpperBound));
      APP_LOGGER_DEBUG("# skewLowerBound -> " + std::to_string(this->skewLowerBound));
      APP_LOGGER_DEBUG("##########################################################");

      //}
      // rounding check, make sure that we don't send a mkt order...
      // static std::string roundInput(double input, const std::string& inputIncrement, bool roundUp)
      // this->aSellLevelB = std::stod(AppUtil::roundInput(this->aSellLevelB, this->orderPriceIncrement_b, true));
      // this->aBuyLevelB = std::stod(AppUtil::roundInput(this->aBuyLevelB, this->orderPriceIncrement_b, false));

      // this->aSellLevelA = std::stod(AppUtil::roundInput(this->aSellLevelA, this->orderPriceIncrement_a, true));
      // this->aBuyLevelA = std::stod(AppUtil::roundInput(this->aBuyLevelA, this->orderPriceIncrement_a, false));

      this->aSellLevelB = ceil(this->aSellLevelB / std::stod(this->orderPriceIncrement_b)) * std::stod(this->orderPriceIncrement_b);
      this->aBuyLevelB = floor(this->aBuyLevelB / std::stod(this->orderPriceIncrement_b)) * std::stod(this->orderPriceIncrement_b);

      this->aSellLevelA = ceil(this->aSellLevelA / std::stod(this->orderPriceIncrement_a)) * std::stod(this->orderPriceIncrement_a);
      this->aBuyLevelA = floor(this->aBuyLevelA / std::stod(this->orderPriceIncrement_a)) * std::stod(this->orderPriceIncrement_a);

      // double tmp1 = std::stod(this->orderPriceIncrement_b);
      // double tmp2 = std::stod(this->orderPriceIncrement_a);
      // std::string quoteLevel_std = AppUtil::roundInput(quoteLevel, leg==0?this->orderPriceIncrement_a:this->orderPriceIncrement_b, false);

      /*

            if(this->aSellLevelA<=std::stod(this->newBestBidPrice_a)) {
              this->aSellLevelA = std::stod(this->newBestBidPrice_a) + 3*std::stod(this->orderPriceIncrement_a);
            }

            if(this->aBuyLevelA>=std::stod(this->newBestAskPrice_a)) {
              this->aBuyLevelA = std::stod(this->newBestAskPrice_a) - 3*std::stod(this->orderPriceIncrement_a);
            }

            if(this->aSellLevelB<=std::stod(this->newBestBidPrice_b)) {
              this->aSellLevelB = std::stod(this->newBestBidPrice_b) + 3*std::stod(this->orderPriceIncrement_b);
            }

            if(this->aBuyLevelB>=std::stod(this->newBestAskPrice_b)) {
              this->aBuyLevelB = std::stod(this->newBestAskPrice_b) - 3*std::stod(this->orderPriceIncrement_b);
            }
      */
      // == of a crossing most probably...to be checked within trade message
      if (moveTheoPrice != 0) {
        APP_LOGGER_DEBUG("##########################################################");
        APP_LOGGER_DEBUG("# Recompute theoretical price! margin lower x: " + std::to_string(marginLowerX) +
                         ", margin lower y: " + std::to_string(marginLowerY) + ", margin upper x: " + std::to_string(marginUpperX) +
                         ", margin upper y: " + std::to_string(marginUpperY) + ", yInterceptMarginLower: " + std::to_string(yInterceptMarginLower) +
                         ", yInterceptMarginUpper: " + std::to_string(yInterceptMarginUpper));
        APP_LOGGER_DEBUG("##########################################################");
        APP_LOGGER_DEBUG("# shifted ");

        APP_LOGGER_DEBUG("# aBuyLevelA: " + std::to_string(this->aBuyLevelA));
        APP_LOGGER_DEBUG("# aSellLevelA: " + std::to_string(this->aSellLevelA));
        APP_LOGGER_DEBUG("# aBuyLevelB: " + std::to_string(this->aBuyLevelB));
        APP_LOGGER_DEBUG("# aSellLevelB: " + std::to_string(this->aSellLevelB));
        APP_LOGGER_DEBUG("##########################################################");
      }

      // keep track of position level E [-nc2l,nc2l] -> see RM constrains...
      if (moveTheoPrice > 0 && relativeTargetPosition >= -this->nc2l) {
        relativeTargetPosition--;
        APP_LOGGER_DEBUG("# New relative position: " + std::to_string(this->relativeTargetPosition));
      }
      if (moveTheoPrice < 0 && relativeTargetPosition <= this->nc2l) {
        relativeTargetPosition++;
        APP_LOGGER_DEBUG("# New relative position: " + std::to_string(this->relativeTargetPosition));
      }

      // cancel and resubmit new pair quotes only if there is an update in the specific quote pair level
      // initialize parameters
      APP_LOGGER_DEBUG("##########################################################");
      APP_LOGGER_DEBUG("# aBuyLevelA: " + std::to_string(this->aBuyLevelA));
      APP_LOGGER_DEBUG("# aSellLevelA: " + std::to_string(this->aSellLevelA));
      APP_LOGGER_DEBUG("# aBuyLevelB: " + std::to_string(this->aBuyLevelB));
      APP_LOGGER_DEBUG("# aSellLevelB: " + std::to_string(this->aSellLevelB));
      APP_LOGGER_DEBUG("##########################################################");

      APP_LOGGER_DEBUG("##########################################################");
      APP_LOGGER_DEBUG("# anOldBuyLevelA: " + std::to_string(anOldBuyLevelA));
      APP_LOGGER_DEBUG("# anOldSellLevelA: " + std::to_string(anOldSellLevelA));
      APP_LOGGER_DEBUG("# anOldBuyLevelB: " + std::to_string(anOldBuyLevelB));
      APP_LOGGER_DEBUG("# anOldSellLevelB: " + std::to_string(anOldSellLevelB));
      APP_LOGGER_DEBUG("##########################################################");

      // APP_LOGGER_DEBUG("##########################################################");
      // APP_LOGGER_DEBUG("# Position status");
      // APP_LOGGER_DEBUG("# relative position: " + std::to_string(this->relative_position));
      // APP_LOGGER_DEBUG("# quoteBalance_a: " + std::to_string(this->quoteBalance_a));
      // APP_LOGGER_DEBUG("# quoteBalance_b: " + std::to_string(this->quoteBalance_b));
      // APP_LOGGER_DEBUG("# baseBalance_a: " + std::to_string(this->baseBalance_a));
      // APP_LOGGER_DEBUG("# baseBalance_b: " + std::to_string(this->baseBalance_b));
      // APP_LOGGER_DEBUG("##########################################################");

      // compute order sizes
      // double midPriceA{}, midPriceB{};

      // midPriceA = (std::stod(this->newBestBidPrice_a)+std::stod(this->newBestAskPrice_a))/2;
      // midPriceB = (std::stod(this->newBestBidPrice_b)+std::stod(this->newBestAskPrice_b))/2;

      // take into account normalized trading vector -> orderAmount should be > 0
      // BEWARE THE CONTRACT DENOMINATED SCALING IS DEALT WITHIN PLACE ORDER
      // for prod A
      if (this->instrument_type_a != "INVERSE") {
        if (!isContractDenominated_a) {
          this->orderAmount_a = abs((this->typicalOrderSize * this->normalizedTradingVector_0) / this->midPriceA);
        } else {
          // this->orderAmount_a = abs(((this->typicalOrderSize * this->normalizedTradingVector_0)/midPriceA)/std::stod(this->orderQuantityIncrement_a));
          this->orderAmount_a = abs(((this->typicalOrderSize * this->normalizedTradingVector_0) / this->midPriceA));
        }
      } else {
        if (!isContractDenominated_a) {
          this->orderAmount_a = abs(this->typicalOrderSize * this->normalizedTradingVector_0);
        } else {
          // this->orderAmount_a = abs((this->typicalOrderSize * this->normalizedTradingVector_0)/std::stod(this->orderQuantityIncrement_a));
          this->orderAmount_a = abs((this->typicalOrderSize * this->normalizedTradingVector_0));
        }
      }

      // for prod B
      if (this->instrument_type_b != "INVERSE") {
        if (!isContractDenominated_b) {
          this->orderAmount_b = abs((this->typicalOrderSize * this->normalizedTradingVector_1) / this->midPriceB);
        } else {
          // this->orderAmount_b = abs(((this->typicalOrderSize * this->normalizedTradingVector_1)/midPriceB)/std::stod(this->orderQuantityIncrement_b));
          this->orderAmount_b = abs(((this->typicalOrderSize * this->normalizedTradingVector_1) / this->midPriceB));
        }
      } else {
        if (!isContractDenominated_b) {
          this->orderAmount_b = abs(this->typicalOrderSize * this->normalizedTradingVector_1);
        } else {
          // this->orderAmount_b = abs((this->typicalOrderSize * this->normalizedTradingVector_1)/std::stod(this->orderQuantityIncrement_b));
          this->orderAmount_b = abs((this->typicalOrderSize * this->normalizedTradingVector_1));
        }
      }

      // update quotes BUT if there is a partial fill then update the order to take the remaining liquidity...
      bool isPartialFillBuyOrderA = false;
      bool isPartialFillSellOrderA = false;
      bool isPartialFillBuyOrderB = false;
      bool isPartialFillSellOrderB = false;

      if (this->openBuyOrder_a)
        isPartialFillBuyOrderA = this->openBuyOrder_a.get().quantity.toDouble() != this->openBuyOrder_a.get().remainingQuantity.toDouble() &&
                                 this->openBuyOrder_a.get().remainingQuantity.toDouble() > 0;
      if (this->openSellOrder_a)
        isPartialFillSellOrderA = this->openSellOrder_a.get().quantity.toDouble() != this->openSellOrder_a.get().remainingQuantity.toDouble() &&
                                  this->openSellOrder_a.get().remainingQuantity.toDouble() > 0;
      if (this->openBuyOrder_b)
        isPartialFillBuyOrderB = this->openBuyOrder_b.get().quantity.toDouble() != this->openBuyOrder_b.get().remainingQuantity.toDouble() &&
                                 this->openBuyOrder_b.get().remainingQuantity.toDouble() > 0;
      if (this->openSellOrder_b)
        isPartialFillSellOrderB = this->openSellOrder_b.get().quantity.toDouble() != this->openSellOrder_b.get().remainingQuantity.toDouble() &&
                                  this->openSellOrder_b.get().remainingQuantity.toDouble() > 0;

      APP_LOGGER_DEBUG("##########################################################");
      APP_LOGGER_DEBUG("# isPartialFillBuyOrderA: " + std::string(isPartialFillBuyOrderA ? "TRUE" : "FALSE"));
      APP_LOGGER_DEBUG("# isPartialFillSellOrderA: " + std::string(isPartialFillSellOrderA ? "TRUE" : "FALSE"));
      APP_LOGGER_DEBUG("# isPartialFillBuyOrderB: " + std::string(isPartialFillBuyOrderB ? "TRUE" : "FALSE"));
      APP_LOGGER_DEBUG("# isPartialFillSellOrderB: " + std::string(isPartialFillSellOrderB ? "TRUE" : "FALSE"));
      APP_LOGGER_DEBUG("##########################################################");

      // if pending orders discrease TTL
      if (isPartialFillBuyOrderA) TTLopenBuyOrder_a -= 1;
      if (isPartialFillSellOrderA) TTLopenSellOrder_a -= 1;
      if (isPartialFillBuyOrderB) TTLopenBuyOrder_b -= 1;
      if (isPartialFillSellOrderB) TTLopenSellOrder_b -= 1;

      APP_LOGGER_DEBUG("##########################################################");
      APP_LOGGER_DEBUG("# PartialFillBuyOrderA TTL: " + std::to_string(TTLopenBuyOrder_a));
      APP_LOGGER_DEBUG("# PartialFillSellOrderA TTL: " + std::to_string(TTLopenSellOrder_a));
      APP_LOGGER_DEBUG("# PartialFillBuyOrderB TTL: " + std::to_string(TTLopenBuyOrder_b));
      APP_LOGGER_DEBUG("# PartialFillSellOrderB TTL: " + std::to_string(TTLopenSellOrder_b));
      APP_LOGGER_DEBUG("##########################################################");

      // RM
      // max inventory limit implementation -> RM
      APP_LOGGER_DEBUG("# Verbose status");
      APP_LOGGER_DEBUG("# baseBalance_a: " + std::to_string(this->baseBalance_a));
      APP_LOGGER_DEBUG("# quoteBalance_a: " + std::to_string(this->quoteBalance_a));
      APP_LOGGER_DEBUG("# orderAmount_a: " + std::to_string(this->orderAmount_a));

      APP_LOGGER_DEBUG("# baseBalance_b: " + std::to_string(this->baseBalance_b));
      APP_LOGGER_DEBUG("# quoteBalance_b: " + std::to_string(this->quoteBalance_b));
      APP_LOGGER_DEBUG("# orderAmount_b: " + std::to_string(this->orderAmount_b));

      APP_LOGGER_DEBUG("##########################################################");

      // risk wall implementation -> rem. order amount is already potentially denominated in contracts...see above
      double maxInventory_a = abs(this->typicalOrderSize * this->nc2l * this->normalizedTradingVector_0);
      double maxInventory_b = abs(this->typicalOrderSize * this->nc2l * this->normalizedTradingVector_1);

      APP_LOGGER_DEBUG("##########################################################");
      APP_LOGGER_DEBUG("# Risk wall/limit breach status: if abs(exposure) > max inventory then risk wall is reached for that direction.");
      // APP_LOGGER_DEBUG("# Max inventory: " + std::to_string(this->maxInventory));
      APP_LOGGER_DEBUG("# Max inventory A: " + std::to_string(maxInventory_a));
      APP_LOGGER_DEBUG("# Max inventory B: " + std::to_string(maxInventory_b));

      // product A
      if (this->instrument_type_a != "INVERSE") {
        /*
        this->upperLimitReached_a = -maxInventory_a >= this->quoteBalance_a - this->orderAmount_a * midPriceA;
        this->lowerLimitReached_a = maxInventory_a <= this->quoteBalance_a + this->orderAmount_a * midPriceA;

        APP_LOGGER_DEBUG("# [prod_a] -> Lower new exposure : " + std::to_string(this->quoteBalance_a - this->orderAmount_a * midPriceA));
        APP_LOGGER_DEBUG("# [prod_a] -> Upper new exposure : " + std::to_string(this->quoteBalance_a + this->orderAmount_a * midPriceA));
        */
        this->upperLimitReached_a = -maxInventory_a >= -this->baseBalance_a * this->midPriceA - this->orderAmount_a * this->midPriceA;
        this->lowerLimitReached_a = maxInventory_a <= -this->baseBalance_a * this->midPriceA + this->orderAmount_a * this->midPriceA;

        APP_LOGGER_DEBUG("# [prod_a] -> Lower new exposure : " +
                         std::to_string(-this->baseBalance_a * this->midPriceA - this->orderAmount_a * this->midPriceA));
        APP_LOGGER_DEBUG("# [prod_a] -> Upper new exposure : " +
                         std::to_string(-this->baseBalance_a * this->midPriceA + this->orderAmount_a * this->midPriceA));

      } else {
        /*
        this->upperLimitReached_a = -maxInventory_a >= -this->baseBalance_a * midPriceA - this->orderAmount_a;
        this->lowerLimitReached_a = maxInventory_a <= -this->baseBalance_a * midPriceA + this->orderAmount_a;

        APP_LOGGER_DEBUG("# [prod_a] -> Lower new exposure : " + std::to_string( -this->baseBalance_a * midPriceA + this->orderAmount_a));
        APP_LOGGER_DEBUG("# [prod_a] -> Upper new exposure : " + std::to_string( -this->baseBalance_a * midPriceA - this->orderAmount_a));
        */
        this->upperLimitReached_a = -maxInventory_a >= this->quoteBalance_a - this->orderAmount_a;
        this->lowerLimitReached_a = maxInventory_a <= this->quoteBalance_a + this->orderAmount_a;

        APP_LOGGER_DEBUG("# [prod_a] -> Lower new exposure : " + std::to_string(this->quoteBalance_a - this->orderAmount_a));
        APP_LOGGER_DEBUG("# [prod_a] -> Upper new exposure : " + std::to_string(this->quoteBalance_a + this->orderAmount_a));
      }

      // product B
      if (this->instrument_type_b != "INVERSE") {
        /*
        this->upperLimitReached_b = -maxInventory_b >= this->quoteBalance_b - this->orderAmount_b * midPriceB;
        this->lowerLimitReached_b = maxInventory_b <= this->quoteBalance_b + this->orderAmount_b * midPriceB;

        APP_LOGGER_DEBUG("# [prod_b] -> Lower new exposure : " + std::to_string(this->quoteBalance_b - this->orderAmount_b * midPriceB));
        APP_LOGGER_DEBUG("# [prod_b] -> Upper new exposure : " + std::to_string(this->quoteBalance_b + this->orderAmount_b * midPriceB));
        */
        this->upperLimitReached_b = -maxInventory_b >= -this->baseBalance_b * this->midPriceB - this->orderAmount_b * this->midPriceB;
        this->lowerLimitReached_b = maxInventory_b <= -this->baseBalance_b * this->midPriceB + this->orderAmount_b * this->midPriceB;

        APP_LOGGER_DEBUG("# [prod_b] -> Lower new exposure : " +
                         std::to_string(-this->baseBalance_b * this->midPriceB - this->orderAmount_b * this->midPriceB));
        APP_LOGGER_DEBUG("# [prod_b] -> Upper new exposure : " +
                         std::to_string(-this->baseBalance_b * this->midPriceB + this->orderAmount_b * this->midPriceB));

      } else {
        /*
        this->upperLimitReached_b = -maxInventory_b >= -this->baseBalance_b * midPriceB - this->orderAmount_b;
        this->lowerLimitReached_b = maxInventory_b <= -this->baseBalance_b * midPriceB + this->orderAmount_b;

        APP_LOGGER_DEBUG("# [prod_b] -> Lower new exposure : " + std::to_string( -this->baseBalance_b * midPriceB + this->orderAmount_b));
        APP_LOGGER_DEBUG("# [prod_b] -> Upper new exposure : " + std::to_string( -this->baseBalance_b * midPriceB - this->orderAmount_b));
        */
        this->upperLimitReached_b = -maxInventory_b >= this->quoteBalance_b - this->orderAmount_b;
        this->lowerLimitReached_b = maxInventory_b <= this->quoteBalance_b + this->orderAmount_b;

        APP_LOGGER_DEBUG("# [prod_b] -> Lower new exposure : " + std::to_string(this->quoteBalance_b - this->orderAmount_b));
        APP_LOGGER_DEBUG("# [prod_b] -> Upper new exposure : " + std::to_string(this->quoteBalance_b + this->orderAmount_b));
      }

      // end limit settings
      APP_LOGGER_DEBUG("##########################################################");
      APP_LOGGER_DEBUG("# Risk wall/limit breach status:");

      // verbose breach limit test results
      if (this->upperLimitReached_a) {
        APP_LOGGER_DEBUG("## UPPER LIMIT A BREACHED !!!!!!!!");
      } else {
        APP_LOGGER_DEBUG("## UPPER LIMIT A NOT BREACHED");
      }

      if (this->lowerLimitReached_a) {
        APP_LOGGER_DEBUG("## LOWER LIMIT A BREACHED !!!!!!!!");
      } else {
        APP_LOGGER_DEBUG("## LOWER LIMIT A NOT BREACHED");
      }

      if (this->upperLimitReached_b) {
        APP_LOGGER_DEBUG("## UPPER LIMIT B BREACHED !!!!!!!!");
      } else {
        APP_LOGGER_DEBUG("## UPPER LIMIT B NOT BREACHED");
      }

      if (this->lowerLimitReached_b) {
        APP_LOGGER_DEBUG("## LOWER LIMIT B BREACHED !!!!!!!!");
      } else {
        APP_LOGGER_DEBUG("## LOWER LIMIT B NOT BREACHED");
      }

      // in order to mitigate latency send cancel all message and then resubmit submit orders
      // Order management under constrains
      // ProdA
      if ((this->aBuyLevelA != this->anOldBuyLevelA || (this->aBuyLevelA != -1 && !this->openBuyOrder_a)) ||
          (this->aSellLevelA != this->anOldSellLevelA || (this->aSellLevelA != -1 && !this->openSellOrder_a))) {
        // if((this->aBuyLevelA != this->anOldBuyLevelA || (this->aBuyLevelA != -1 && this->numOpenOrdersBuy_a==0)) || (this->aSellLevelA !=
        // this->anOldSellLevelA || (this->aSellLevelA != -1 && this->numOpenOrdersSell_a==0))) {

        // cancel previous orders for leg A
        APP_LOGGER_DEBUG("##########################################################");
        APP_LOGGER_DEBUG("# Try to Cancel Order prodA -> side==all");

        // check that the previous cancel response was recieved and that an hedge is not occuring at that point in time
        // don't lock except for hedging
        // if(!this->is_lock_hedged_a) {
        if (!this->is_lock_cancelled_a && !this->is_lock_hedged_a) {
          APP_LOGGER_DEBUG("# Cancel Order for prodA -> side==all and set lock");

          // set lock
          this->is_lock_cancelled_a = true;

          const std::string& messageTimeISO = UtilTime::getISOTimestamp(tpnow());
          this->cancelAllOpenOrders(event, session, requestList, tpnow(), messageTimeISO, "prodA");

          // this->cancelAllOpenOrders(event, session, requestList, messageTime, messageTimeISO, "prodB");

          // clean up internal order state -> done when response is recieved
          this->openBuyOrder_a = boost::none;
          this->openSellOrder_a = boost::none;

          this->anOldBuyLevelA = this->aBuyLevelA;
          this->anOldSellLevelA = this->aSellLevelA;
        } else {
          APP_LOGGER_DEBUG("# Don't cancel Order for prodA -> previous cancel or hedge response wasn't recieved");
        }

        APP_LOGGER_DEBUG("##########################################################");

        /*
                    APP_LOGGER_DEBUG("##########################################################");
                    // swl quoter, i.e. leg==0 -> leg a, leg==1 -> leg b ## side==0 -> buy, side==1 -> sell, is post only
                    if(!this->upperLimitReached_a) {
                      APP_LOGGER_DEBUG("# Place quote A, side==buy");
                      this->placeQuote(event, session, requestList, messageTimeReceived, this->aBuyLevelA, this->orderAmount_a, 0, 0, true); // place ONE QUOTE,
           therefore one more open order } else { APP_LOGGER_DEBUG("# Risk limit reached, don't place quote A, side==buy");
                    }
                    // swl quoter, i.e. leg==0 -> leg a, leg==1 -> leg b ## side==0 -> buy, side==1 -> sell
                    if(!this->lowerLimitReached_a) {
                      APP_LOGGER_DEBUG("# Place quote A, side==sell");
                      this->placeQuote(event, session, requestList, messageTimeReceived, this->aSellLevelA, this->orderAmount_a, 0, 1, true); // place ONE
           QUOTE, therefore one more open order } else { APP_LOGGER_DEBUG("# Risk limit reached, don't place quote A, side==sell");
                    }
                    APP_LOGGER_DEBUG("##########################################################");
        */
      }

      // prodB
      if ((this->aBuyLevelB != this->anOldBuyLevelB || (this->aBuyLevelB != -1 && !this->openBuyOrder_b)) ||
          (this->aSellLevelB != this->anOldSellLevelB || (this->aSellLevelB != -1 && !this->openSellOrder_b))) {
        // cancel previous orders for leg B
        APP_LOGGER_DEBUG("##########################################################");
        APP_LOGGER_DEBUG("# Try to Cancel Order prodB -> side==all");

        // check that the previous cancel response was recieved and that an hedge is not occuring at that point in time
        if (!this->is_lock_cancelled_b && !this->is_lock_hedged_b) {
          // if(!this->is_lock_hedged_b) {
          APP_LOGGER_DEBUG("# Cancel Order for prodB -> side==all and set lock");

          // set lock
          this->is_lock_cancelled_b = true;

          const std::string& messageTimeISO = UtilTime::getISOTimestamp(tpnow());
          this->cancelAllOpenOrders(event, session, requestList, tpnow(), messageTimeISO, "prodB");

          // this->cancelAllOpenOrders(event, session, requestList, messageTime, messageTimeISO, "prodB");

          // clean up internal order state -> done when response is recieved
          this->openBuyOrder_b = boost::none;
          this->openSellOrder_b = boost::none;

          this->anOldBuyLevelB = this->aBuyLevelB;
          this->anOldSellLevelB = this->aSellLevelB;
        } else {
          APP_LOGGER_DEBUG("# Don't cancel Order for prodB -> previous cancel or hedge response wasn't recieved");
        }

        APP_LOGGER_DEBUG("##########################################################");
        /*
                    // swl quoter, i.e. leg==0 -> leg a, leg==1 -> leg b ## side==0 -> buy, side==1 -> sell
                    if(!this->upperLimitReached_b) {
                      APP_LOGGER_DEBUG("# Place quote B, side==buy");
                      this->placeQuote(event, session, requestList, messageTimeReceived, this->aBuyLevelB, this->orderAmount_b, 1, 0, true); // place ONE QUOTE,
           therefore one more open order } else { APP_LOGGER_DEBUG("# Risk limit reached, don't place quote B, side==buy");
                    }
                    // swl quoter, i.e. leg==0 -> leg a, leg==1 -> leg b ## side==0 -> buy, side==1 -> sell
                    if(!this->lowerLimitReached_b) {
                      APP_LOGGER_DEBUG("# Place quote B, side==sell");
                      this->placeQuote(event, session, requestList, messageTimeReceived, this->aSellLevelB, this->orderAmount_b, 1, 1, true); // place ONE
           QUOTE, therefore one more open order } else { APP_LOGGER_DEBUG("# Risk limit reached, don't place quote B, side==sell");
                    }
                    APP_LOGGER_DEBUG("##########################################################");
        */
      }

      // check for max drawdown and eventually trigger stop loss process
      this->stopLossLimitCheck(event, session, requestList, tpnow());

    }  // end on quote
    //} // end check for rate limit breaches...still eventually call balance retrieval
    //} // delay if taker order was sent
  }  // end MARKET_DATA_EVENTS_MARKET_DEPTH

  // SL IMPLEMENTATION
  virtual void stopLossLimitCheck(const Event& event, Session* session, std::vector<Request>& requestList, const TimePoint& messageTime) {
    const std::string& messageTimeISO = UtilTime::getISOTimestamp(messageTime);

    // const auto& messageTimeReceived = message.getTimeReceived();
    // const std::string& messageTimeReceivedISO = UtilTime::getISOTimestamp(messageTimeReceived);

    double totalBalance = this->baseBalance_a * this->midPriceA + this->quoteBalance_a + this->baseBalance_b * this->midPriceB + this->quoteBalance_b;
    if (totalBalance > this->totalBalancePeak) {
      this->totalBalancePeak = totalBalance;
    }
    // if ((this->totalBalancePeak - totalBalance) / this->totalBalancePeak > this->killSwitchMaximumDrawdown) {
    if ((this->totalBalancePeak - totalBalance) > this->killSwitchMaximumDrawdown && !this->isStopLossTriggered) {
      APP_LOGGER_INFO("##########################################################");
      APP_LOGGER_INFO("##########################################################");
      APP_LOGGER_INFO(" # Kill switch triggered - Maximum drawdown. Liquidate current position and Exit.");
      APP_LOGGER_INFO("##########################################################");
      APP_LOGGER_INFO("##########################################################");

      APP_LOGGER_INFO("#### this->totalBalancePeak : " + std::to_string(this->totalBalancePeak));
      APP_LOGGER_INFO("#### totalBalance : " + std::to_string(totalBalance));

      APP_LOGGER_INFO("#### this->killSwitchMaximumDrawdown : " + std::to_string(this->killSwitchMaximumDrawdown));

      // double conditionKill = (this->totalBalancePeak - totalBalance) / this->totalBalancePeak;
      // APP_LOGGER_INFO("#### conditionKill : " + std::to_string(conditionKill));

      // stop engine for further processing outside liquidation process
      this->isStopLossTriggered = true;
      // this->promisePtr->set_value();
      // this->skipProcessEvent = true;

      // clear request list
      // requestList.clear();

      // compute USD eq of inventory for both legs and align
      double inventoryUSD_A = 0.0;
      double inventoryUSD_B = 0.0;

      // inverse
      if (this->instrument_type_a == "INVERSE") inventoryUSD_A = this->quoteBalance_a;
      if (this->instrument_type_b == "INVERSE") inventoryUSD_B = this->quoteBalance_b;

      // linear -> we have to consider opposite sign to be compared with inverse prod
      if (this->instrument_type_a != "INVERSE") inventoryUSD_A = -this->baseBalance_a * this->midPriceA;
      if (this->instrument_type_b != "INVERSE") inventoryUSD_B = -this->baseBalance_b * this->midPriceB;

      /*
      // linear
      if(this->instrument_type_a != "INVERSE") inventoryUSD_A = this->quoteBalance_a;
      if(this->instrument_type_b != "INVERSE") inventoryUSD_B = this->quoteBalance_b;

      // inverse -> we have to consider opposite sign to be compared with linear prod
      if(this->instrument_type_a == "INVERSE") inventoryUSD_A = -this->baseBalance_a * midPriceA;
      if(this->instrument_type_b == "INVERSE") inventoryUSD_B = -this->baseBalance_b * midPriceB;
      */

      APP_LOGGER_INFO("#### Current quote balance a: " + std::to_string(this->quoteBalance_a) + " base balance a:" + std::to_string(this->baseBalance_a));
      APP_LOGGER_INFO("#### Current quote balance b: " + std::to_string(this->quoteBalance_b) + " base balance b:" + std::to_string(this->baseBalance_b));

      APP_LOGGER_INFO("#### Current Inventory USD eq. A: " + std::to_string(inventoryUSD_A));
      APP_LOGGER_INFO("#### Current Inventory USD eq. B: " + std::to_string(inventoryUSD_B));

      // send a sell mkt order -> taker
      double aSellLevelTakerA = std::stod(this->bestBidPrice_a) -
                                this->offset * abs(std::stod(this->bestBidPrice_a) -
                                                   std::stod(this->bestAskPrice_a));  // sell into the order book == mkt order : BEWARE OF THE DUST...

      // send a buy mkt order
      double aBuyLevelTakerA = std::stod(this->bestAskPrice_a) +
                               this->offset * abs(std::stod(this->bestBidPrice_a) -
                                                  std::stod(this->bestAskPrice_a));  // buy into the order book == mkt order : BEWARE OF THE DUST...

      // send a sell mkt order
      double aSellLevelTakerB = std::stod(this->bestBidPrice_b) -
                                this->offset * abs(std::stod(this->bestBidPrice_b) -
                                                   std::stod(this->bestAskPrice_b));  // sell into the order book == mkt order : BEWARE OF THE DUST...

      // send a buy mkt order
      double aBuyLevelTakerB = std::stod(this->bestAskPrice_b) +
                               this->offset * abs(std::stod(this->bestBidPrice_b) -
                                                  std::stod(this->bestAskPrice_b));  // buy into the order book == mkt order : BEWARE OF THE DUST...

      APP_LOGGER_INFO("# midPriceA: " + std::to_string(this->midPriceA));
      APP_LOGGER_INFO("# midPriceB: " + std::to_string(this->midPriceB));
      APP_LOGGER_INFO("##########################################################");
      APP_LOGGER_INFO("# aSellLevelTakerA: " + std::to_string(aSellLevelTakerA));
      APP_LOGGER_INFO("# aBuyLevelTakerA: " + std::to_string(aBuyLevelTakerA));
      APP_LOGGER_INFO("# aSellLevelTakerB: " + std::to_string(aSellLevelTakerB));
      APP_LOGGER_INFO("# aBuyLevelTakerB: " + std::to_string(aBuyLevelTakerB));
      APP_LOGGER_INFO("##########################################################");

      // clear request list
      requestList.clear();

      // build cancel orders for existing open orders (if they exist)
      // -> liquidate open orders only when liquidation trades took place...
      /*
      APP_LOGGER_INFO("##########################################################");
      APP_LOGGER_INFO(" # Build cancel orders for existing open orders (if they exist).");
      APP_LOGGER_INFO("##########################################################");

      this->cancelAllOpenOrders(event, session, requestList, tpnow(), messageTimeISO, "prodA");
      this->cancelAllOpenOrders(event, session, requestList, tpnow(), messageTimeISO, "prodB");
      */

      // this->cancelAllOpenOrders(event, session, requestList, messageTime, messageTimeISO, "prodA");
      // this->cancelAllOpenOrders(event, session, requestList, messageTime, messageTimeISO, "prodB");

      // this->cancelOpenOrders(event, session, requestList, messageTime, messageTimeISO, false, "prodA", "buy");
      // this->cancelOpenOrders(event, session, requestList, messageTime, messageTimeISO, false, "prodA", "sell");

      // this->cancelOpenOrders(event, session, requestList, messageTime, messageTimeISO, false, "prodB", "buy");
      // this->cancelOpenOrders(event, session, requestList, messageTime, messageTimeISO, false, "prodB", "sell");
      //  end cancel orders

      // create liquidation orders
      APP_LOGGER_INFO("##########################################################");
      APP_LOGGER_INFO("# create liquidation orders");
      APP_LOGGER_INFO("##########################################################");

      // order mgt A
      if (this->instrument_type_a != "INVERSE") {
        this->amountTaker = abs(inventoryUSD_A / this->midPriceA);
      } else {
        this->amountTaker = abs(inventoryUSD_A);
      }

      /*
      // contract denomination dealt within placeorder function...don't duplicate
      if(this->instrument_type_a != "INVERSE") {
        if(!isContractDenominated_a) {
          this->amountTaker = abs(inventoryUSD_A/midPriceA);
        } else {
          this->amountTaker = round(abs(inventoryUSD_A/midPriceA/std::stod(this->orderQuantityIncrement_a)));
        }
      } else {
        if(!isContractDenominated_a) {
          this->amountTaker = abs(inventoryUSD_A);
        } else {
          this->amountTaker = round(abs(inventoryUSD_A)/std::stod(this->orderQuantityIncrement_a));
        }
      }
      */
      // Ensure min order size is at least one lot -> done within placeQuote
      // this->amountTaker = this->amountTaker >= std::stod(this->orderQuantityIncrement_a)?this->amountTaker:std::stod(this->orderQuantityIncrement_a);

      if (inventoryUSD_A < 0) {
        APP_LOGGER_INFO("##########################################################");
        APP_LOGGER_INFO("# Product_a");
        APP_LOGGER_INFO("# Sell amount (USD): " + std::to_string(abs(this->amountTaker)));
        APP_LOGGER_INFO("# At price: " + std::to_string(aSellLevelTakerA));
        APP_LOGGER_INFO("##########################################################");

        // swl quoter, i.e. leg==0 -> leg a, leg==1 -> leg b ## side==0 -> buy, side==1 -> sell -> is post only == false as taker
        this->placeQuote(event, session, requestList, tpnow(), aSellLevelTakerA, abs(this->amountTaker), 0, 1, false);
      } else {
        APP_LOGGER_INFO("##########################################################");
        APP_LOGGER_INFO("# Product_a");
        APP_LOGGER_INFO("# Buy amount (USD): " + std::to_string(abs(this->amountTaker)));
        APP_LOGGER_INFO("# At price: " + std::to_string(aBuyLevelTakerA));
        APP_LOGGER_INFO("##########################################################");

        this->placeQuote(event, session, requestList, tpnow(), aBuyLevelTakerA, abs(this->amountTaker), 0, 0, false);
      }

      // order mgt B
      if (this->instrument_type_b != "INVERSE") {
        this->amountTaker = abs(inventoryUSD_B / this->midPriceB);
      } else {
        this->amountTaker = abs(inventoryUSD_B);
      }
      /*
      if(this->instrument_type_b != "INVERSE") {
        if(!isContractDenominated_b) {
          this->amountTaker = abs(inventoryUSD_B/midPriceB);
        } else {
          this->amountTaker = round(abs(inventoryUSD_B/midPriceB/std::stod(this->orderQuantityIncrement_b)));
        }
      } else {
        if(!isContractDenominated_b) {
          this->amountTaker = abs(inventoryUSD_B);
        } else {
          this->amountTaker = round(abs(inventoryUSD_B)/std::stod(this->orderQuantityIncrement_b));
        }
      }
      */

      // Ensure min order size is at least one lot -> done within placeQuote
      // this->amountTaker = this->amountTaker >= std::stod(this->orderQuantityIncrement_b)?this->amountTaker:std::stod(this->orderQuantityIncrement_b);

      if (inventoryUSD_B < 0) {
        APP_LOGGER_INFO("##########################################################");
        APP_LOGGER_INFO("# Product_b");
        APP_LOGGER_INFO("# Sell amount (USD): " + std::to_string(abs(this->amountTaker)));
        APP_LOGGER_INFO("# At price: " + std::to_string(aSellLevelTakerB));
        APP_LOGGER_INFO("##########################################################");

        // swl quoter, i.e. leg==0 -> leg a, leg==1 -> leg b ## side==0 -> buy, side==1 -> sell
        this->placeQuote(event, session, requestList, tpnow(), aSellLevelTakerB, abs(this->amountTaker), 1, 1, false);
      } else {
        APP_LOGGER_INFO("##########################################################");
        APP_LOGGER_INFO("# Product_b");
        APP_LOGGER_INFO("# Buy amount (USD): " + std::to_string(abs(this->amountTaker)));
        APP_LOGGER_INFO("# At price: " + std::to_string(aBuyLevelTakerB));
        APP_LOGGER_INFO("##########################################################");

        this->placeQuote(event, session, requestList, tpnow(), aBuyLevelTakerB, abs(this->amountTaker), 1, 0, false);
      }

      /*
      APP_LOGGER_INFO("##########################################################");
      APP_LOGGER_INFO(" # Submit request list to exchange if live trading");
      APP_LOGGER_INFO("##########################################################");
      APP_LOGGER_INFO(" # Request list size : " + std::to_string(requestList.size()));

      if (this->tradingMode == TradingMode::LIVE) {
        for (auto& request : requestList) {
          std::string corrId = request.getCorrelationId();

          // retrieve message correlation id and filter out product id
          auto correlationIdTocken = UtilString::split(corrId, '#');
          std::string correlationId = correlationIdTocken.at(0);
          std::string productId = correlationIdTocken.at(1);

          auto operation = request.getOperation();

          if (operation == Request::Operation::CREATE_ORDER || operation == Request::Operation::CANCEL_ORDER) {
            if(productId=="prodA" && this->useWebsocketToExecuteOrder_a) {
              session->sendRequestByWebsocket(request);
            }

            if(productId=="prodB" && this->useWebsocketToExecuteOrder_b) {
              session->sendRequestByWebsocket(request);
            }

            if(productId=="prodA" && !this->useWebsocketToExecuteOrder_a) session->sendRequest(request);
            if(productId=="prodB" && !this->useWebsocketToExecuteOrder_b) session->sendRequest(request);

            //if(!this->useWebsocketToExecuteOrder_a && !this->useWebsocketToExecuteOrder_b) session->sendRequest(request);
          } else {
            session->sendRequest(request);
          }
        }
      }
      */

      APP_LOGGER_INFO("##########################################################");
      APP_LOGGER_INFO(" # End algo - stop loss - triggered");
      APP_LOGGER_INFO("##########################################################");
      /////
      // return;
    }
  }  // end stop loss

  virtual void postProcessMessageMarketDataEventPrivateTrade(const Event& event, Session* session, std::vector<Request>& requestList, const Message& message) {
    const auto& correlationIdList = message.getCorrelationIdList();

    auto correlationIdTocken = UtilString::split(correlationIdList.at(0), '#');
    std::string correlationId = correlationIdTocken.at(0);
    std::string productId = correlationIdTocken.at(1);

    const auto& messageTimeReceived = message.getTimeReceived();
    const auto& messageTimeReceivedISO = UtilTime::getISOTimestamp(messageTimeReceived);

    /*     type = EXECUTION_MANAGEMENT_EVENTS_PRIVATE_TRADE,
      recapType = UNKNOWN,
      time = 2023-04-01T02:01:28.000000000Z,
      timeReceived = 2023-04-01T02:01:28.000000000Z,
      elementList = [
        Element [
          nameValueMap = {
            CLIENT_ORDER_ID = 2023-04-01T02:01:26.000Z_SELL,
            FEE_ASSET = USD,
            FEE_QUANTITY = 0,
            IS_MAKER = 1,
            LAST_EXECUTED_PRICE = 28744,
            LAST_EXECUTED_SIZE = 20,
            ORDER_ID = 3858,
            SIDE = SELL,
            TRADE_ID = 1*/

    for (const auto& element : message.getElementList()) {
      APP_LOGGER_INFO("##########################################################");
      APP_LOGGER_INFO(" POST PROCESS EXECUTION_MANAGEMENT_EVENTS_PRIVATE_TRADE : pretty message: " + message.toStringPretty());

      double lastExecutedPrice = std::stod(element.getValue(CCAPI_EM_ORDER_LAST_EXECUTED_PRICE));
      double lastExecutedSize = std::stod(element.getValue(CCAPI_EM_ORDER_LAST_EXECUTED_SIZE));
      const auto& feeQuantityStr = element.getValue(CCAPI_EM_ORDER_FEE_QUANTITY);

      double feeQuantity = feeQuantityStr.empty() ? 0 : std::stod(feeQuantityStr);
      std::string feeAsset = element.getValue(CCAPI_EM_ORDER_FEE_ASSET);
      bool isMaker = element.getValue(CCAPI_IS_MAKER) == "1";
      std::string side = UtilString::toUpper(element.getValue(CCAPI_EM_ORDER_SIDE));

      const auto& messageTimeReceived = message.getTimeReceived();
      const auto& messageTimeReceivedISO = UtilTime::getISOTimestamp(messageTimeReceived);

      if (isMaker) {
        APP_LOGGER_INFO("# ISMAKER -> TRUE");
      } else {
        APP_LOGGER_INFO("# ISMAKER -> FALSE");
      }

      // compute NUMBER OF CONTRACTS from trade confirmation, Only if we are dealing with derivative products
      // reality check in R
      // check if the trade is a maker or taker order. Taker order == hedge

      // if taker trades then the hedge was supposely realized, then release lock
      if (!isMaker) {
        if (productId == "prodA") {
          if ((this->isReinit && this->isLiquidated_a) || (this->isTarget && this->isLiquidated_a) || (this->isInitOrder && this->isLiquidated_a) ||
              (this->isReinitOrder && this->isLiquidated_a) || !(this->isReinit || this->isTarget || this->isInitOrder || isReinitOrder)) {
            // if((this->isReinit && this->isLiquidated_a) || !this->isReinit) {
            APP_LOGGER_INFO("##########################################################");
            APP_LOGGER_INFO("# A private taker (hedge) trade just occured for leg a; release the cancel all orders lock!!");
            APP_LOGGER_INFO("##########################################################");
            this->is_lock_hedged_a = false;
          }

          if (this->isReinit && !this->isLiquidated_a) {
            APP_LOGGER_INFO("##########################################################");
            APP_LOGGER_INFO("# A liquidation order for legA recieved!!");
            APP_LOGGER_INFO("##########################################################");

            // before assuming an init position is liquidated check for partially filled orders
            APP_LOGGER_INFO("# Previous liquidation qty for leg A: " + std::to_string(this->remainingLiquidationQty_a));

            this->remainingLiquidationQty_a = this->remainingLiquidationQty_a + lastExecutedSize * (side == CCAPI_EM_ORDER_SIDE_BUY ? 1 : -1);

            APP_LOGGER_INFO("# Current liquidation qty for leg A: " + std::to_string(this->remainingLiquidationQty_a));

            if (this->remainingLiquidationQty_a == 0) {
              APP_LOGGER_INFO("# As remaining liquidation qty for leg A is == 0 we assume we are flat on the market.");
              this->isLiquidated_a = true;
            }

            APP_LOGGER_INFO("##########################################################");
          }

          if (this->isReinitOrder && !this->isLiquidated_a) {
            APP_LOGGER_INFO("##########################################################");
            APP_LOGGER_INFO("# A liquidation order for legA recieved!!");
            APP_LOGGER_INFO("##########################################################");

            // before assuming an init position is liquidated check for partially filled orders
            APP_LOGGER_INFO("# Previous liquidation qty for leg A: " + std::to_string(this->remainingLiquidationQty_a));

            if (!this->isContractDenominated_a) {
              this->remainingLiquidationQty_a = this->remainingLiquidationQty_a + lastExecutedSize * (side == CCAPI_EM_ORDER_SIDE_BUY ? -1 : 1);
            } else {
              this->remainingLiquidationQty_a =
                  this->remainingLiquidationQty_a + lastExecutedSize * std::stod(this->orderQuantityIncrement_a) * (side == CCAPI_EM_ORDER_SIDE_BUY ? -1 : 1);
            }

            APP_LOGGER_INFO("# Current liquidation qty for leg A: " + std::to_string(this->remainingLiquidationQty_a));

            if (this->remainingLiquidationQty_a == 0) {
              APP_LOGGER_INFO("# As remaining liquidation qty for leg A is == 0 we assume we are flat on the market.");
              this->isLiquidated_a = true;
            }

            APP_LOGGER_INFO("##########################################################");
          }

          if (this->isTarget && !this->isLiquidated_a) {
            APP_LOGGER_INFO("##########################################################");
            APP_LOGGER_INFO("# An initial position rebalance for legA recieved!!");
            APP_LOGGER_INFO("##########################################################");

            // before assuming an init position is liquidated check for partially filled orders
            APP_LOGGER_INFO("# Previous liquidation qty (USD) for leg A: " + std::to_string(this->remainingLiquidationQty_a));

            // tDenominated in USD
            if (!this->isContractDenominated_a) {
              this->remainingLiquidationQty_a = this->remainingLiquidationQty_a + lastExecutedSize * (side == CCAPI_EM_ORDER_SIDE_BUY ? -1 : 1);
            } else {
              this->remainingLiquidationQty_a =
                  this->remainingLiquidationQty_a + lastExecutedSize * std::stod(this->orderQuantityIncrement_a) * (side == CCAPI_EM_ORDER_SIDE_BUY ? -1 : 1);
            }
            if (this->remainingLiquidationQty_a == 0) {
              APP_LOGGER_INFO("# Initial target position (NOT QUOTE VALUE) for leg A in USD is: " + std::to_string(this->initTargetPosition_a));
              APP_LOGGER_INFO("# quoteBalance_a: " + std::to_string(this->quoteBalance_a));
              APP_LOGGER_INFO("# Remaining order qty in USD for leg A is: " + std::to_string(this->remainingLiquidationQty_a));
              this->isLiquidated_a = true;
            }

            APP_LOGGER_INFO("##########################################################");
          }

          // remark the isLiquidated flag is used here to check if the order is filled (i.e. not partially)
          if (this->isInitOrder && !this->isLiquidated_a) {
            APP_LOGGER_INFO("##########################################################");
            APP_LOGGER_INFO("# An initial order to be sent for legA recieved!!");
            APP_LOGGER_INFO("##########################################################");

            // before assuming an init order is filled check for partially filled orders
            // remaining liquidation quantity correspond to the remaining of the init order to be executed.
            APP_LOGGER_INFO("# Previous execution qty (USD) for leg A: " + std::to_string(this->remainingLiquidationQty_a));

            // Denominated in USD
            if (!this->isContractDenominated_a) {
              this->remainingLiquidationQty_a = this->remainingLiquidationQty_a + lastExecutedSize * (side == CCAPI_EM_ORDER_SIDE_BUY ? -1 : 1);
            } else {
              this->remainingLiquidationQty_a =
                  this->remainingLiquidationQty_a + lastExecutedSize * std::stod(this->orderQuantityIncrement_a) * (side == CCAPI_EM_ORDER_SIDE_BUY ? -1 : 1);
            }
            if (this->remainingLiquidationQty_a == 0) {
              APP_LOGGER_INFO("# Initial order size for leg A in USD is: " + std::to_string(this->initOrderSize_a));
              APP_LOGGER_INFO("# quoteBalance_a: " + std::to_string(this->quoteBalance_a));
              APP_LOGGER_INFO("# Remaining order qty in USD for leg A is: " + std::to_string(this->remainingLiquidationQty_a));
              this->isLiquidated_a = true;
            }

            APP_LOGGER_INFO("##########################################################");
          }

          // if stop loss liquidation trade for leg A occured then cancel remaining open orders
          if (this->isStopLossTriggered) {
            APP_LOGGER_INFO("##########################################################");
            APP_LOGGER_INFO(" # Stop loss triggered");
            APP_LOGGER_INFO(" # recieved confirmation of liquidation trades");
            APP_LOGGER_INFO(" # cancel remaining open orders for leg A.");
            APP_LOGGER_INFO("##########################################################");

            this->cancelAllOpenOrders(event, session, requestList, tpnow(), messageTimeReceivedISO, "prodA");
          }
        } else {
          // prodB
          if ((this->isReinit && this->isLiquidated_b) || (this->isTarget && this->isLiquidated_b) || (this->isInitOrder && this->isLiquidated_b) ||
              (this->isReinitOrder && this->isLiquidated_b) || !(this->isReinit || this->isTarget || this->isInitOrder || isReinitOrder)) {
            // if((this->isReinit && this->isLiquidated_b) || (this->isTarget && this->isLiquidated_b) || !(this->isReinit || this->isTarget)) {
            // if((this->isReinit && this->isLiquidated_b) || !this->isReinit) {
            APP_LOGGER_INFO("##########################################################");
            APP_LOGGER_INFO("# A private taker (hedge) trade just occured for leg b; release the cancel all orders lock!!");
            APP_LOGGER_INFO("##########################################################");
            this->is_lock_hedged_b = false;
          }

          if (this->isReinit && !this->isLiquidated_b) {
            APP_LOGGER_INFO("##########################################################");
            APP_LOGGER_INFO("# A liquidation order for legB recieved!!");
            APP_LOGGER_INFO("##########################################################");

            // before assuming an init position is liquidated check for partially filled orders
            APP_LOGGER_INFO("# Previous liquidation qty for leg B: " + std::to_string(this->remainingLiquidationQty_b));

            this->remainingLiquidationQty_b = this->remainingLiquidationQty_b + lastExecutedSize * (side == CCAPI_EM_ORDER_SIDE_BUY ? 1 : -1);

            APP_LOGGER_INFO("# Current liquidation qty for leg B: " + std::to_string(this->remainingLiquidationQty_b));

            if (this->remainingLiquidationQty_b == 0) {
              APP_LOGGER_INFO("# As remaining liquidation qty for leg B is == 0 we assume we are flat on the market.");
              this->isLiquidated_b = true;
            }

            APP_LOGGER_INFO("##########################################################");
          }

          if (this->isReinitOrder && !this->isLiquidated_b) {
            APP_LOGGER_INFO("##########################################################");
            APP_LOGGER_INFO("# A liquidation order for legB recieved!!");
            APP_LOGGER_INFO("##########################################################");

            // before assuming an init position is liquidated check for partially filled orders
            APP_LOGGER_INFO("# Previous liquidation qty for leg B: " + std::to_string(this->remainingLiquidationQty_b));
            if (!this->isContractDenominated_b) {
              this->remainingLiquidationQty_b = this->remainingLiquidationQty_b + lastExecutedSize * (side == CCAPI_EM_ORDER_SIDE_BUY ? -1 : 1);
            } else {
              this->remainingLiquidationQty_b =
                  this->remainingLiquidationQty_b + lastExecutedSize * std::stod(this->orderQuantityIncrement_b) * (side == CCAPI_EM_ORDER_SIDE_BUY ? -1 : 1);
            }
            APP_LOGGER_INFO("# Current liquidation qty for leg B: " + std::to_string(this->remainingLiquidationQty_b));

            if (this->remainingLiquidationQty_b == 0) {
              APP_LOGGER_INFO("# As remaining liquidation qty for leg B is == 0 we assume we are flat on the market.");
              this->isLiquidated_b = true;
            }

            APP_LOGGER_INFO("##########################################################");
          }

          if (this->isTarget && !this->isLiquidated_b) {
            APP_LOGGER_INFO("##########################################################");
            APP_LOGGER_INFO("# An initial position rebalance for legB recieved!!");
            APP_LOGGER_INFO("##########################################################");

            // before assuming an init position is liquidated check for partially filled orders
            APP_LOGGER_INFO("# Previous liquidation qty (USD) for leg B: " + std::to_string(this->remainingLiquidationQty_b));

            // tDenominated in USD
            if (!this->isContractDenominated_b) {
              this->remainingLiquidationQty_b = this->remainingLiquidationQty_b + lastExecutedSize * (side == CCAPI_EM_ORDER_SIDE_BUY ? -1 : 1);
            } else {
              this->remainingLiquidationQty_b =
                  this->remainingLiquidationQty_b + lastExecutedSize * std::stod(this->orderQuantityIncrement_b) * (side == CCAPI_EM_ORDER_SIDE_BUY ? -1 : 1);
            }
            if (this->remainingLiquidationQty_b == 0) {
              APP_LOGGER_INFO("# Initial target position (NOT QUOTE VALUE) for leg B in USD is: " + std::to_string(this->initTargetPosition_b));
              APP_LOGGER_INFO("# quoteBalance_b: " + std::to_string(this->quoteBalance_b));
              APP_LOGGER_INFO("# Remaining order qty in USD for leg B is: " + std::to_string(this->remainingLiquidationQty_b));
              this->isLiquidated_b = true;
            }

            APP_LOGGER_INFO("##########################################################");
          }

          // remark the isLiquidated flag is used here to check if the order is filled (i.e. not partially)
          if (this->isInitOrder && !this->isLiquidated_b) {
            APP_LOGGER_INFO("##########################################################");
            APP_LOGGER_INFO("# An initial order to be sent for legB recieved!!");
            APP_LOGGER_INFO("##########################################################");

            // before assuming an init order is filled check for partially filled orders
            // remaining liquidation quantity correspond to the remaining of the init order to be executed.
            APP_LOGGER_INFO("# Previous execution qty (USD) for leg B: " + std::to_string(this->remainingLiquidationQty_b));

            // Denominated in USD
            if (!this->isContractDenominated_b) {
              this->remainingLiquidationQty_b = this->remainingLiquidationQty_b + lastExecutedSize * (side == CCAPI_EM_ORDER_SIDE_BUY ? -1 : 1);
            } else {
              this->remainingLiquidationQty_b =
                  this->remainingLiquidationQty_b + lastExecutedSize * std::stod(this->orderQuantityIncrement_b) * (side == CCAPI_EM_ORDER_SIDE_BUY ? -1 : 1);
            }
            if (this->remainingLiquidationQty_b == 0) {
              APP_LOGGER_INFO("# Initial order size for leg B in USD is: " + std::to_string(this->initOrderSize_b));
              APP_LOGGER_INFO("# quoteBalance_b: " + std::to_string(this->quoteBalance_b));
              APP_LOGGER_INFO("# Remaining order qty in USD for leg B is: " + std::to_string(this->remainingLiquidationQty_b));
              this->isLiquidated_b = true;
            }

            APP_LOGGER_INFO("##########################################################");
          }

          // if stop loss liquidation trade for leg B occured then cancel remaining open orders
          if (this->isStopLossTriggered) {
            APP_LOGGER_INFO("##########################################################");
            APP_LOGGER_INFO(" # Stop loss triggered");
            APP_LOGGER_INFO(" # recieved confirmation of liquidation trades");
            APP_LOGGER_INFO(" # cancel remaining open orders for leg B.");
            APP_LOGGER_INFO("##########################################################");

            this->cancelAllOpenOrders(event, session, requestList, tpnow(), messageTimeReceivedISO, "prodB");
          }
        }
      }

      // INFO: The column is_buyer_maker tells us whether the buyer is the maker (i.e. the seller is the taker) or not.
      if (isMaker && abs(lastExecutedSize) > 0) {
        APP_LOGGER_INFO("##########################################################");
        APP_LOGGER_INFO("# A private maker trade just occured !!");
        APP_LOGGER_INFO("# relative position: " + std::to_string(this->relative_position));
        APP_LOGGER_INFO("# quoteBalance_a: " + std::to_string(this->quoteBalance_a));
        APP_LOGGER_INFO("# quoteBalance_b: " + std::to_string(this->quoteBalance_b));
        APP_LOGGER_INFO("# baseBalance_a: " + std::to_string(this->baseBalance_a));
        APP_LOGGER_INFO("# baseBalance_b: " + std::to_string(this->baseBalance_b));
        APP_LOGGER_INFO("##########################################################");

        // if maker trade then lock the other leg that will be the taker one...
        if (productId == "prodA") {
          APP_LOGGER_INFO("##########################################################");
          APP_LOGGER_INFO("# Set cancel all orders lock for leg b!!");
          APP_LOGGER_INFO("##########################################################");
          this->is_lock_hedged_b = true;
        } else {
          APP_LOGGER_INFO("##########################################################");
          APP_LOGGER_INFO("# Set cancel all orders lock for leg a!!");
          APP_LOGGER_INFO("##########################################################");
          this->is_lock_hedged_a = true;
        }

        // allow one sec before onQuote activated to prevent cancel all message deleting this taker order...
        // long currentTimeMicro = getUnixTimestampMicro(this->tpnow());
        // this->previousCrossingMicro=currentTimeMicro;

        // send a mkt order on the other leg B as trade confirmation took place on leg A or the opposite way around
        // adjust order size in order to reach cash neutrality
        // double midPriceA = (std::stod(this->bestBidPrice_a)+std::stod(this->bestAskPrice_a))/2;
        // double midPriceB = (std::stod(this->bestBidPrice_b)+std::stod(this->bestAskPrice_b))/2;

        // send a sell mkt order -> taker
        double aSellLevelTakerA = std::stod(this->bestBidPrice_a) -
                                  this->offset * abs(std::stod(this->bestBidPrice_a) -
                                                     std::stod(this->bestAskPrice_a));  // sell into the order book == mkt order : BEWARE OF THE DUST...

        // send a buy mkt order
        double aBuyLevelTakerA = std::stod(this->bestAskPrice_a) +
                                 this->offset * abs(std::stod(this->bestBidPrice_a) -
                                                    std::stod(this->bestAskPrice_a));  // buy into the order book == mkt order : BEWARE OF THE DUST...

        // send a sell mkt order
        double aSellLevelTakerB = std::stod(this->bestBidPrice_b) -
                                  this->offset * abs(std::stod(this->bestBidPrice_b) -
                                                     std::stod(this->bestAskPrice_b));  // sell into the order book == mkt order : BEWARE OF THE DUST...

        // send a buy mkt order
        double aBuyLevelTakerB = std::stod(this->bestAskPrice_b) +
                                 this->offset * abs(std::stod(this->bestBidPrice_b) -
                                                    std::stod(this->bestAskPrice_b));  // buy into the order book == mkt order : BEWARE OF THE DUST...

        APP_LOGGER_INFO("# midPriceA: " + std::to_string(this->midPriceA));
        APP_LOGGER_INFO("# midPriceB: " + std::to_string(this->midPriceB));
        APP_LOGGER_INFO("##########################################################");
        APP_LOGGER_INFO("# aSellLevelTakerA: " + std::to_string(aSellLevelTakerA));
        APP_LOGGER_INFO("# aBuyLevelTakerA: " + std::to_string(aBuyLevelTakerA));
        APP_LOGGER_INFO("# aSellLevelTakerB: " + std::to_string(aSellLevelTakerB));
        APP_LOGGER_INFO("# aBuyLevelTakerB: " + std::to_string(aBuyLevelTakerB));
        APP_LOGGER_INFO("##########################################################");

        // INVERSE
        /*      elementList = [
          Element [
            nameValueMap = {
              CLIENT_ORDER_ID = 2023-04-01T02:01:26.000Z_SELL,
              FEE_ASSET = USD,
              FEE_QUANTITY = 0,
              IS_MAKER = 1,
              LAST_EXECUTED_PRICE = 28744,
              LAST_EXECUTED_SIZE = 150,
              ORDER_ID = 3858,
              SIDE = SELL,
              TRADE_ID = 1
            }
          ]
        ],*/

        // LINEAR
        /*  elementList = [
          Element [
            nameValueMap = {
              CLIENT_ORDER_ID = 2023-04-01T02:01:32.000Z_SELL,
              FEE_ASSET = USD,
              FEE_QUANTITY = 0.028742108909,
              IS_MAKER = 1,
              LAST_EXECUTED_PRICE = 28770.87978886,
              LAST_EXECUTED_SIZE = 0.004995,
              ORDER_ID = 1160,
              SIDE = SELL,
              TRADE_ID = 2
            }
          ]
        ],*/

        // version 3 spread capture
        // version 3...deal with dust, i.e. partially filled orders...
        // Possibly some messages are delayed...therefore we cannot rely on inventory as per previous implementation
        // Hence simply execute an hedge order in accordance with trading vector on the other leg...
        // order size in USD equivalent -> version 3 -> take into account trading signal for proportionality != cash neutrality
        double makerOrderSizeUSD{};
        double takerOrderSizeUSD{};
        if (productId == "prodA") {
          if (!this->isContractDenominated_a) {
            makerOrderSizeUSD = lastExecutedSize;
          } else {
            makerOrderSizeUSD = lastExecutedSize * std::stod(this->orderQuantityIncrement_a);
          }

          // taker order on leg b
          takerOrderSizeUSD = makerOrderSizeUSD * abs(this->normalizedTradingVector_1 / this->normalizedTradingVector_0);

          // deal with usd/coin denominated contract
          if (this->instrument_type_b != "INVERSE") {
            this->amountTaker = abs(takerOrderSizeUSD / this->midPriceB);
          } else {
            this->amountTaker = abs(takerOrderSizeUSD);
          }
        } else {
          if (!this->isContractDenominated_b) {
            makerOrderSizeUSD = lastExecutedSize;
          } else {
            makerOrderSizeUSD = lastExecutedSize * std::stod(this->orderQuantityIncrement_b);
          }

          // taker order on leg b
          takerOrderSizeUSD = makerOrderSizeUSD * abs(this->normalizedTradingVector_0 / this->normalizedTradingVector_1);

          // deal with usd/coin denominated contract
          if (this->instrument_type_a != "INVERSE") {
            this->amountTaker = abs(takerOrderSizeUSD / this->midPriceA);
          } else {
            this->amountTaker = abs(takerOrderSizeUSD);
          }
        }

        APP_LOGGER_INFO("####################################################################");
        APP_LOGGER_INFO("#### Trying to capture spread (BEWARE: EXECUTION SIZE MAY BE DENOMINATED IN CONTRACT):");
        if (side == CCAPI_EM_ORDER_SIDE_BUY) {
          if (productId == "prodA") {
            APP_LOGGER_INFO("#### Bought as maker legA: " + std::to_string(lastExecutedSize) + "@" + std::to_string(lastExecutedPrice));
          } else {
            APP_LOGGER_INFO("#### Bought as maker legB: " + std::to_string(lastExecutedSize) + "@" + std::to_string(lastExecutedPrice));
          }
        } else {
          if (productId == "prodA") {
            APP_LOGGER_INFO("#### Sold as maker legA: " + std::to_string(lastExecutedSize) + "@" + std::to_string(lastExecutedPrice));
          } else {
            APP_LOGGER_INFO("#### Sold as maker legB: " + std::to_string(lastExecutedSize) + "@" + std::to_string(lastExecutedPrice));
          }
        }

        APP_LOGGER_INFO("#### Current quote balance b: " + std::to_string(this->quoteBalance_b) + " base balance b:" + std::to_string(this->baseBalance_b));
        APP_LOGGER_INFO("#### Current quote balance a: " + std::to_string(this->quoteBalance_a) + " base balance a:" + std::to_string(this->baseBalance_a));

        APP_LOGGER_INFO("#### Maker order size USD eq. taking into account contract denomination: " + std::to_string(makerOrderSizeUSD));
        APP_LOGGER_INFO("#### normalizedTradingVector_0: " + std::to_string(this->normalizedTradingVector_0));
        APP_LOGGER_INFO("#### normalizedTradingVector_1: " + std::to_string(this->normalizedTradingVector_1));
        APP_LOGGER_INFO("#### Taker order size USD eq. taking into account normalized trading vector: " + std::to_string(takerOrderSizeUSD));

        // execution take place here (BEWARE IF PRODUCT==A MEANS A MARKET ORDER WAS LIFTED FOR LEG A, HENCE EXECUTE TAKER ON LEG B)
        if (side == CCAPI_EM_ORDER_SIDE_SELL) {
          // double quoteLevel, double quoteAmount, int leg, int side)
          // swl quoter, i.e. leg==0 -> leg a, leg==1 -> leg b ## side==0 -> buy, side==1 -> sell
          this->placeQuote(event, session, requestList, tpnow(), productId == "prodA" ? aBuyLevelTakerB : aBuyLevelTakerA, this->amountTaker,
                           productId == "prodA" ? 1 : 0, 0, false);

          // move margin
          if (productId == "prodB") {
            this->isDownwardCrossing = true;
          } else {
            this->isUpwardCrossing = true;
          }

        } else {
          this->placeQuote(event, session, requestList, tpnow(), productId == "prodA" ? aSellLevelTakerB : aSellLevelTakerA, this->amountTaker,
                           productId == "prodA" ? 1 : 0, 1, false);

          // move margin
          if (productId == "prodB") {
            this->isUpwardCrossing = true;
          } else {
            this->isDownwardCrossing = true;
          }
        }
      }  // END MAKER ORDER
    }  // next bracket end EXECUTION_MANAGEMENT_EVENTS_PRIVATE_TRADE

    // SWL LOGIC TODO
    // if (this->enableAdverseSelectionGuard) {
    //  int intervalStart = UtilTime::getUnixTimestamp(messageTime) / this->adverseSelectionGuardMarketDataSampleIntervalSeconds *
    //                      this->adverseSelectionGuardMarketDataSampleIntervalSeconds;
    //  for (auto& kv : this->publicTradeMap) {
    //    kv.second.erase(kv.second.begin(), kv.second.upper_bound(intervalStart - this->adverseSelectionGuardMarketDataSampleBufferSizeSeconds));
    //  }
    //  const auto& elementList = message.getElementList();
    //  auto rit = elementList.rbegin();
    //  if (rit != elementList.rend()) {
    // #if APP_PUBLIC_TRADE_LAST != -1
    //        this->publicTradeMap[APP_PUBLIC_TRADE_LAST][intervalStart] = std::stod(rit->getValue(CCAPI_LAST_PRICE));
    // #endif
    //      }
    //    }
  }

  virtual void extractInstrumentInfo_2p(const Element& element, bool isProductA) {
    // seggregate between two legs
    if (isProductA) {
      this->baseAsset_a = element.getValue(CCAPI_BASE_ASSET);
      APP_LOGGER_INFO("Base asset for leg a is " + this->baseAsset_a);
      this->quoteAsset_a = element.getValue(CCAPI_QUOTE_ASSET);
      APP_LOGGER_INFO("Quote asset for leg a is " + this->quoteAsset_a);
      if (this->exchange_a == "bitfinex") {
        this->orderPriceIncrement_a = "0.00000001";
      } else {
        this->orderPriceIncrement_a = Decimal(element.getValue(CCAPI_ORDER_PRICE_INCREMENT)).toString();
      }
      APP_LOGGER_INFO("Order price increment for leg a is " + this->orderPriceIncrement_a);
      if (this->exchange_a == "bitfinex") {
        this->orderQuantityIncrement_a = "0.00000001";
      } else {
        this->orderQuantityIncrement_a = Decimal(element.getValue(CCAPI_ORDER_QUANTITY_INCREMENT)).toString();
      }
      APP_LOGGER_INFO("Order quantity increment for leg a is " + this->orderQuantityIncrement_a);
    } else {
      this->baseAsset_b = element.getValue(CCAPI_BASE_ASSET);
      APP_LOGGER_INFO("Base asset for leg b is " + this->baseAsset_b);
      this->quoteAsset_b = element.getValue(CCAPI_QUOTE_ASSET);
      APP_LOGGER_INFO("Quote asset for leg b is " + this->quoteAsset_b);
      if (this->exchange_b == "bitfinex") {
        this->orderPriceIncrement_b = "0.00000001";
      } else {
        this->orderPriceIncrement_b = Decimal(element.getValue(CCAPI_ORDER_PRICE_INCREMENT)).toString();
      }
      APP_LOGGER_INFO("Order price increment for leg b is " + this->orderPriceIncrement_b);
      if (this->exchange_b == "bitfinex") {
        this->orderQuantityIncrement_b = "0.00000001";
      } else {
        this->orderQuantityIncrement_b = Decimal(element.getValue(CCAPI_ORDER_QUANTITY_INCREMENT)).toString();
      }
      APP_LOGGER_INFO("Order quantity increment for leg b is " + this->orderQuantityIncrement_b);
    }
  }

  /*
  // legacy implementation
  virtual void extractInstrumentInfo(const Element& element) {
    this->baseAsset = element.getValue(CCAPI_BASE_ASSET);
    APP_LOGGER_INFO("Base asset is " + this->baseAsset);
    this->quoteAsset = element.getValue(CCAPI_QUOTE_ASSET);
    APP_LOGGER_INFO("Quote asset is " + this->quoteAsset);
    if (this->exchange == "bitfinex") {
      this->orderPriceIncrement = "0.00000001";
    } else {
      this->orderPriceIncrement = Decimal(element.getValue(CCAPI_ORDER_PRICE_INCREMENT)).toString();
    }
    APP_LOGGER_INFO("Order price increment is " + this->orderPriceIncrement);
    if (this->exchange == "bitfinex") {
      this->orderQuantityIncrement = "0.00000001";
    } else {
      this->orderQuantityIncrement = Decimal(element.getValue(CCAPI_ORDER_QUANTITY_INCREMENT)).toString();
    }
    APP_LOGGER_INFO("Order quantity increment is " + this->orderQuantityIncrement);
  }
  */

  virtual void extractBalanceInfo2L(const Element& element, int leg) {
    const auto& asset = element.getValue(CCAPI_EM_ASSET);
    if (leg == 0) {
      if (UtilString::toLower(asset) == this->baseAsset_a) {
        this->baseBalance_a = std::stod(element.getValue(CCAPI_EM_QUANTITY_AVAILABLE_FOR_TRADING));
      } else if (UtilString::toLower(asset) == this->quoteAsset_a) {
        this->quoteBalance_a = std::stod(element.getValue(CCAPI_EM_QUANTITY_AVAILABLE_FOR_TRADING));
      }
    } else {
      if (UtilString::toLower(asset) == this->baseAsset_b) {
        this->baseBalance_b = std::stod(element.getValue(CCAPI_EM_QUANTITY_AVAILABLE_FOR_TRADING));
      } else if (UtilString::toLower(asset) == this->quoteAsset_b) {
        this->quoteBalance_b = std::stod(element.getValue(CCAPI_EM_QUANTITY_AVAILABLE_FOR_TRADING));
      }
    }
  }

  /*
    // some exchanges, e.g. gemini don't provide such option within their api -> workaround keep track within trade acknowledgment
    virtual void extractPositionInfo(const Element& element, int leg) {
      if(leg==0) {
          this->position_qty_a = std::stod(element.getValue(CCAPI_EM_POSITION_QUANTITY));
        } else {
          this->position_qty_b = std::stod(element.getValue(CCAPI_EM_POSITION_QUANTITY));
        }
    }
  */

  /*
  // deribit -> /private/get_positions
  {
    "jsonrpc": "2.0",
    "id": 2236,
    "result": [
        {
            "average_price": 7440.18,
            "delta": 0.006687487,
          ->  "direction": "buy",
            "estimated_liquidation_price": 1.74,
            "floating_profit_loss": 0,
            "index_price": 7466.79,
            "initial_margin": 0.000197283,
            "instrument_name": "BTC-PERPETUAL",
            "interest_value" : 1.7362511643080387,
            "kind": "future",
            "leverage": 34,
            "maintenance_margin": 0.000143783,
            "mark_price": 7476.65,
            "open_orders_margin": 0.000197288,
            "realized_funding": -1e-8,
            "realized_profit_loss": -9e-9,
            "settlement_price": 7476.65,
          ->  "size": 50,
            "size_currency": 0.006687487,
            "total_profit_loss": 0.000032781
        }
    ]
  }

  // binance -> GET /dapi/v1/positionRisk
      {
          "symbol": "BTCUSD_201225",
        ->  "positionAmt": "1",
          "entryPrice": "11707.70000003",
          "breakEvenPrice": "11707.80000005",  // break-even price
          "markPrice": "11788.66626667",
          "unRealizedProfit": "0.00005866",
          "liquidationPrice": "11667.63509587",
          "leverage": "125",
          "maxQty": "50",
          "marginType": "cross",
          "isolatedMargin": "0.00000000",
          "isAutoAddMargin": "false",
        ->  "positionSide": "LONG",
          "updateTime": 1627026881327
       },

  */

  virtual void extractPositionInfo(const Element& element, const std::string& productId) {
    std::string side = element.getValue(CCAPI_EM_POSITION_SIDE);
    double qty = abs(std::stod(element.getValue(CCAPI_EM_POSITION_QUANTITY)));  // * (side==CCAPI_EM_POSITION_SIDE_LONG?1:-1);

    // element.insert(CCAPI_EM_POSITION_SIDE, std::string(x["positionSide"].GetString())=="LONG"?CCAPI_EM_POSITION_SIDE_LONG:CCAPI_EM_POSITION_SIDE_SHORT);

    if (side != CCAPI_EM_POSITION_SIDE_LONG) {
      qty *= -1;
    }

    /*
    if(abs(qty)>0) {
      qty *= side==CCAPI_EM_POSITION_SIDE_LONG?1:-1;
    }
    */

    if (productId == "prodA") {
      APP_LOGGER_DEBUG("#################################################################################################################");
      APP_LOGGER_DEBUG("##### Rebalance product: prodA");
      APP_LOGGER_DEBUG("##### Local Balances ");

      APP_LOGGER_DEBUG("##### quoteBalance_a: " + std::to_string(this->quoteBalance_a));
      APP_LOGGER_DEBUG("##### baseBalance_a: " + std::to_string(this->baseBalance_a));
      APP_LOGGER_DEBUG("##### position_qty_a: " + std::to_string(this->position_qty_a));
    } else {
      APP_LOGGER_DEBUG("#################################################################################################################");
      APP_LOGGER_DEBUG("##### Rebalance product: prodB");
      APP_LOGGER_DEBUG("##### Local Balances ");

      APP_LOGGER_DEBUG("##### quoteBalance_b: " + std::to_string(this->quoteBalance_b));
      APP_LOGGER_DEBUG("##### baseBalance_b: " + std::to_string(this->baseBalance_b));
      APP_LOGGER_DEBUG("##### position_qty_b: " + std::to_string(this->position_qty_b));
    }

    /* // dbg
    [2023-10-11T11:33:53.409893000Z] DEBUG: ##### Rebalance product: prodA
[2023-10-11T11:33:53.409902000Z] DEBUG: ##### Local Balances
[2023-10-11T11:33:53.409912000Z] DEBUG: ##### quoteBalance_a: 180.000000
[2023-10-11T11:33:53.409921000Z] DEBUG: ##### baseBalance_a: -0.006293
[2023-10-11T11:33:53.409931000Z] DEBUG: ##### position_qty_a: -18.000000
[2023-10-11T11:33:53.409941000Z] DEBUG: #################################################################################################################
[2023-10-11T11:33:53.409950000Z] DEBUG: Exchange Balances -> updated balances prodA
[2023-10-11T11:33:53.409960000Z] DEBUG: ##### quoteBalance_a: 180.000000
[2023-10-11T11:33:53.409970000Z] DEBUG: ##### baseBalance_a: 0.006291
[2023-10-11T11:33:53.409979000Z] DEBUG: ##### position_qty_a: 180.000000
*/

    if (productId == "prodA") {
      if (!isContractDenominated_a) {
        this->position_qty_a = qty / std::stod(this->orderQuantityIncrement_a);
      } else {
        this->position_qty_a = qty;
      }
    } else {
      if (!isContractDenominated_b) {
        this->position_qty_b = qty / std::stod(this->orderQuantityIncrement_b);
      } else {
        this->position_qty_b = qty;
      }
    }

    // update position which is expressed in quote currency for inverse and base currency for linear...prevent deleting accumulated pnl in the other currency
    // respectivelly
    if (productId == "prodA") {
      // seggregate between inverse/linear & contracts/usd denominated
      if (this->instrument_type_a != "INVERSE") {
        if (!isContractDenominated_a) {
          this->baseBalance_a = -qty;
          // this->quoteBalance_a = -qty * this->midPriceA;
        } else {
          this->baseBalance_a = -qty * std::stod(this->orderQuantityIncrement_a);  // with orderQuantityIncrement expressed in base currency
          // this->quoteBalance_a = -qty * std::stod(this->orderQuantityIncrement_a) * this->midPriceA; // with orderQuantityIncrement expressed in base
          // currency
        }
      } else {
        if (!isContractDenominated_a) {
          // this->baseBalance_a = -qty / this->midPriceA;
          this->quoteBalance_a = -qty;
        } else {
          // this->baseBalance_a = -(qty  * std::stod(this->orderQuantityIncrement_a)) / this->midPriceA;
          this->quoteBalance_a = -qty * std::stod(this->orderQuantityIncrement_a);
        }
      }
    } else {
      if (this->instrument_type_b != "INVERSE") {
        if (!isContractDenominated_b) {
          this->baseBalance_b = -qty;
          // this->quoteBalance_b = -qty * this->midPriceB;
        } else {
          this->baseBalance_b = -qty * std::stod(this->orderQuantityIncrement_b);  // with orderQuantityIncrement expressed in base currency
          // this->quoteBalance_b = -qty * std::stod(this->orderQuantityIncrement_b) * this->midPriceB; // with orderQuantityIncrement expressed in base
          // currency
        }
      } else {
        if (!isContractDenominated_b) {
          // this->baseBalance_b = -qty / this->midPriceB;
          this->quoteBalance_b = -qty;
        } else {
          // this->baseBalance_b = -(qty  * std::stod(this->orderQuantityIncrement_b)) / this->midPriceB;
          this->quoteBalance_b = -qty * std::stod(this->orderQuantityIncrement_b);
        }
      }
    }

    if (productId == "prodA") {
      APP_LOGGER_DEBUG("#################################################################################################################");
      APP_LOGGER_DEBUG("Exchange Balances -> updated balances prodA");

      APP_LOGGER_DEBUG("##### quoteBalance_a: " + std::to_string(this->quoteBalance_a));
      APP_LOGGER_DEBUG("##### baseBalance_a: " + std::to_string(this->baseBalance_a));
      APP_LOGGER_DEBUG("##### position_qty_a: " + std::to_string(this->position_qty_a));
    } else {
      APP_LOGGER_DEBUG("#################################################################################################################");
      APP_LOGGER_DEBUG("Exchange Balances -> updated balances prodB");

      APP_LOGGER_DEBUG("##### quoteBalance_b: " + std::to_string(this->quoteBalance_b));
      APP_LOGGER_DEBUG("##### baseBalance_b: " + std::to_string(this->baseBalance_b));
      APP_LOGGER_DEBUG("##### position_qty_b: " + std::to_string(this->position_qty_b));
    }
  }

  // binance
  // case Request::Operation::GET_ACCOUNT_POSITIONS
  /*
      virtual void extractPositionInfo(const Element& element, int leg) {
      if(leg==0) {
          this->position_qty_a = std::stod(element.getValue(CCAPI_EM_POSITION_QUANTITY));
        } else {
          this->position_qty_b = std::stod(element.getValue(CCAPI_EM_POSITION_QUANTITY));
        }
    }
  */
  // legacy implementation
  /*
    virtual void extractBalanceInfo(const Element& element) {
      const auto& asset = element.getValue(CCAPI_EM_ASSET);
      if (asset == this->baseAsset) {
        this->baseBalance = std::stod(element.getValue(CCAPI_EM_QUANTITY_AVAILABLE_FOR_TRADING)) * this->baseAvailableBalanceProportion;
      } else if (asset == this->quoteAsset) {
        this->quoteBalance = std::stod(element.getValue(CCAPI_EM_QUANTITY_AVAILABLE_FOR_TRADING)) * this->quoteAvailableBalanceProportion;
      }
    }
  */
  virtual void updateAccountBalancesByFee(const std::string& feeAsset, double feeQuantity, const std::string& side, bool isMaker,
                                          const std::string& productId) {
    if (productId == "prodA") {
      if (UtilString::toLower(feeAsset) == this->baseAsset_a) {
        this->baseBalance_a -= feeQuantity;
      } else if (UtilString::toLower(feeAsset) == this->quoteAsset) {
        this->quoteBalance_a -= feeQuantity;
      }
    } else {
      if (UtilString::toLower(feeAsset) == this->baseAsset_b) {
        this->baseBalance_b -= feeQuantity;
      } else if (UtilString::toLower(feeAsset) == this->quoteAsset) {
        this->quoteBalance_b -= feeQuantity;
      }
    }
  }

  //
  // CANCEL open orders for the two legs
  //
  virtual void cancelOpenOrders(const Event& event, Session* session, std::vector<Request>& requestList, const TimePoint& messageTime,
                                const std::string& messageTimeISO, bool alwaysCancel, const std::string& productId, const std::string& side) {
    // APP_LOGGER_DEBUG("CANCEL FUNCTION CALLED NUMBER OF ORDER A: " +
    // productId=="prodA"?std::to_string(this->numOpenOrders_a):std::to_string(this->numOpenOrders_b));
    APP_LOGGER_DEBUG("#################################################################################################################");
    APP_LOGGER_DEBUG("##### Within cancelOpenOrders Function");
    if (side == "buy") {
      if (productId == "prodA") {
        APP_LOGGER_DEBUG("Buy cancel called for A, number of open buy orders for A: " + std::to_string(this->numOpenOrdersBuy_a));
      } else {
        APP_LOGGER_DEBUG("Buy cancel called for B, number of open buy orders for B: " + std::to_string(this->numOpenOrdersBuy_b));
      }
    } else {
      if (productId == "prodA") {
        APP_LOGGER_DEBUG("Sell cancel called for A, number of open sell orders for A: " + std::to_string(this->numOpenOrdersSell_a));
      } else {
        APP_LOGGER_DEBUG("Sell cancel called for B, number of open sell orders for B: " + std::to_string(this->numOpenOrdersSell_b));
      }
    }

    // check that we are not  trying to cancel an order that was already cancelled due to the fact that the last order id response is not
    // arrived yet...
    bool isLastOrderIdAvailable = false;

    // PROD A
    if (productId == "prodA" && side == "sell") {
      if (this->openSellOrder_a && !this->openSellOrder_a.get().orderId.empty()) {
        if (this->openSellOrder_a.get().orderId != this->lastCancelledOrderIdSell_a) {
          isLastOrderIdAvailable = true;

          APP_LOGGER_DEBUG(
              "#### this->openSellOrder_a.get().orderId != this->lastCancelledOrderIdSell_a: TRUE; isLastOrderIdAvailable = true; "
              "numOrdersWaitingCancelSell_a: " +
              std::to_string(this->numOrdersWaitingCancelSell_a));
        } else {
          isLastOrderIdAvailable = false;
          this->numOrdersWaitingCancelSell_a += 1;

          APP_LOGGER_DEBUG(
              "#### this->openSellOrder_a.get().orderId != this->lastCancelledOrderIdSell_a: FALSE; isLastOrderIdAvailable = false; "
              "numOrdersWaitingCancelSell_a: " +
              std::to_string(this->numOrdersWaitingCancelSell_a));
        }
      }
    }

    if (productId == "prodA" && side == "buy") {
      if (this->openBuyOrder_a && !this->openBuyOrder_a.get().orderId.empty()) {
        if (this->openBuyOrder_a.get().orderId != this->lastCancelledOrderIdBuy_a) {
          isLastOrderIdAvailable = true;

          APP_LOGGER_DEBUG(
              "#### this->openBuyOrder_a.get().orderId != this->lastCancelledOrderIdBuy_a: TRUE; isLastOrderIdAvailable = true; numOrdersWaitingCancelBuy_a: " +
              std::to_string(this->numOrdersWaitingCancelBuy_a));
        } else {
          isLastOrderIdAvailable = false;
          this->numOrdersWaitingCancelBuy_a += 1;

          APP_LOGGER_DEBUG(
              "#### this->openBuyOrder_a.get().orderId != this->lastCancelledOrderIdBuy_a: FALSE; isLastOrderIdAvailable = "
              "false;numOrdersWaitingCancelBuy_a: " +
              std::to_string(this->numOrdersWaitingCancelBuy_a));
        }
      }
    }
    // END PROD A

    // PROD B
    if (productId == "prodB" && side == "sell") {
      if (this->openSellOrder_b && !this->openSellOrder_b.get().orderId.empty()) {
        if (this->openSellOrder_b.get().orderId != this->lastCancelledOrderIdSell_b) {
          isLastOrderIdAvailable = true;

          APP_LOGGER_DEBUG(
              "#### this->openSellOrder_b.get().orderId != this->lastCancelledOrderIdSell_b: TRUE; isLastOrderIdAvailable = true; "
              "numOrdersWaitingCancelSell_b: " +
              std::to_string(this->numOrdersWaitingCancelSell_b));
        } else {
          isLastOrderIdAvailable = false;
          this->numOrdersWaitingCancelSell_b += 1;

          APP_LOGGER_DEBUG(
              "#### this->openSellOrder_b.get().orderId != this->lastCancelledOrderIdSell_b: FALSE; isLastOrderIdAvailable = false; "
              "numOrdersWaitingCancelSell_b: " +
              std::to_string(this->numOrdersWaitingCancelSell_b));
        }
      }
    }

    if (productId == "prodB" && side == "buy") {
      if (this->openBuyOrder_b && !this->openBuyOrder_b.get().orderId.empty()) {
        if (this->openBuyOrder_b.get().orderId != this->lastCancelledOrderIdBuy_b) {
          isLastOrderIdAvailable = true;

          APP_LOGGER_DEBUG(
              "#### this->openBuyOrder_b.get().orderId != this->lastCancelledOrderIdBuy_b: TRUE; isLastOrderIdAvailable = true; numOrdersWaitingCancelBuy_b: " +
              std::to_string(this->numOrdersWaitingCancelBuy_b));
        } else {
          isLastOrderIdAvailable = false;
          this->numOrdersWaitingCancelBuy_b += 1;

          APP_LOGGER_DEBUG(
              "#### this->openBuyOrder_b.get().orderId != this->lastCancelledOrderIdBuy_b: FALSE; isLastOrderIdAvailable = false; "
              "numOrdersWaitingCancelBuy_b: " +
              std::to_string(this->numOrdersWaitingCancelBuy_b));
        }
      }
    }
    // END PROD B

    // if(lastCancelledOrderId != isLastOrderIdAvailable) {
    if (isLastOrderIdAvailable) {
      // reworked cancel oders
      // LEG A
      if (productId == "prodA") {
        if (side == "sell") {
          // CANCEL SELL

          // check that there exist open sell order for that leg
          if (this->openSellOrder_a && !this->openSellOrder_a.get().orderId.empty()) {
            this->cancelSellOrderRequestCorrelationId_a = "CANCEL_SELL_ORDER#prodA";

            Request request(Request::Operation::CANCEL_ORDER, this->exchange_a, this->instrumentRest_a, this->cancelSellOrderRequestCorrelationId_a,
                            this->credential_a);
            // rem for huobi 2 types of orders-> see ccapi_execution_management_service_huobi.h : CCAPI_EM_ORDER_ID

            request.appendParam({
                {CCAPI_EM_ORDER_ID, this->openSellOrder_a.get().orderId},
            });
            if (!this->credential_2_a.empty()) {
              request.setCredential(this->credential_2_a);
            }
            request.setTimeSent(messageTime);
            requestList.emplace_back(std::move(request));

            APP_LOGGER_DEBUG("############################################################################################################");
            APP_LOGGER_DEBUG("#### Cancel ONE open sell orders for leg a.");
            APP_LOGGER_DEBUG("#### lastCancelledOrderIdSell_a: " + this->lastCancelledOrderIdSell_a);
            APP_LOGGER_DEBUG("#### openSellOrder_a.get().orderId: " + this->openSellOrder_a.get().orderId);
            APP_LOGGER_DEBUG("############################################################################################################");

            // keep track of number of open buy orders
            this->numOpenOrdersSell_a -= 1;
            this->lastCancelledOrderIdSell_a = this->openSellOrder_a.get().orderId;
          }
          if (!this->openSellOrder_a || this->openSellOrder_a.get().orderId.empty()) {
            APP_LOGGER_DEBUG("WARNING: sell orders id missing for cancel for leg a.");
          }
        } else {
          // CANCEL BUY
          // check that there exist open buy order for that leg
          if (this->openBuyOrder_a && !this->openBuyOrder_a.get().orderId.empty()) {
            this->cancelBuyOrderRequestCorrelationId_a = "CANCEL_BUY_ORDER#prodA";

            Request request(Request::Operation::CANCEL_ORDER, this->exchange_a, this->instrumentRest_a, this->cancelBuyOrderRequestCorrelationId_a,
                            this->credential_a);
            // rem for huobi 2 types of orders-> see ccapi_execution_management_service_huobi.h : CCAPI_EM_ORDER_ID
            request.appendParam({
                {CCAPI_EM_ORDER_ID, this->openBuyOrder_a.get().orderId},
            });
            if (!this->credential_2_a.empty()) {
              request.setCredential(this->credential_2_a);
            }
            request.setTimeSent(messageTime);
            requestList.emplace_back(std::move(request));

            APP_LOGGER_DEBUG("############################################################################################################");
            APP_LOGGER_DEBUG("Cancel ONE open buy orders for leg a.");
            APP_LOGGER_DEBUG("#### lastCancelledOrderIdBuy_a: " + this->lastCancelledOrderIdBuy_a);
            APP_LOGGER_DEBUG("#### openBuyOrder_a.get().orderId: " + this->openBuyOrder_a.get().orderId);
            APP_LOGGER_DEBUG("############################################################################################################");

            // keep track of number of open buy orders
            this->numOpenOrdersBuy_a -= 1;
            this->lastCancelledOrderIdBuy_a = this->openBuyOrder_a.get().orderId;
          }
          if (!this->openBuyOrder_a || this->openBuyOrder_a.get().orderId.empty()) {
            APP_LOGGER_DEBUG("WARNING: buy orders id missing for cancel for leg a.");
          }
        }
      }
      // END LEG A

      // LEG B
      if (productId == "prodB") {
        if (side == "sell") {
          // CANCEL SELL

          // check that there exist open sell order for that leg
          if (this->openSellOrder_b && !this->openSellOrder_b.get().orderId.empty()) {
            this->cancelSellOrderRequestCorrelationId_b = "CANCEL_SELL_ORDER#prodB";

            Request request(Request::Operation::CANCEL_ORDER, this->exchange_b, this->instrumentRest_b, this->cancelSellOrderRequestCorrelationId_b,
                            this->credential_b);
            // rem for huobi 2 types of orders-> see ccapi_execution_management_service_huobi.h : CCAPI_EM_ORDER_ID
            request.appendParam({
                {CCAPI_EM_ORDER_ID, this->openSellOrder_b.get().orderId},
            });
            if (!this->credential_2_a.empty()) {
              request.setCredential(this->credential_2_b);
            }
            request.setTimeSent(messageTime);
            requestList.emplace_back(std::move(request));

            APP_LOGGER_DEBUG("############################################################################################################");
            APP_LOGGER_DEBUG("Cancel ONE open sell orders for leg b.");
            APP_LOGGER_DEBUG("#### lastCancelledOrderIdSell_b: " + this->lastCancelledOrderIdSell_b);
            APP_LOGGER_DEBUG("#### openSellOrder_b.get().orderId: " + this->openSellOrder_b.get().orderId);
            APP_LOGGER_DEBUG("############################################################################################################");

            // keep track of number of open buy orders
            this->numOpenOrdersSell_b -= 1;
            this->lastCancelledOrderIdSell_b = this->openSellOrder_b.get().orderId;
          }
          if (!this->openSellOrder_b || this->openSellOrder_b.get().orderId.empty()) {
            APP_LOGGER_DEBUG("WARNING: sell orders id missing for cancel for leg b.");
          }
        } else {
          // CANCEL BUY

          if (this->openBuyOrder_b && !this->openBuyOrder_b.get().orderId.empty()) {
            this->cancelBuyOrderRequestCorrelationId_b = "CANCEL_BUY_ORDER#prodB";

            Request request(Request::Operation::CANCEL_ORDER, this->exchange_b, this->instrumentRest_b, this->cancelBuyOrderRequestCorrelationId_b,
                            this->credential_b);
            // rem for huobi 2 types of orders-> see ccapi_execution_management_service_huobi.h : CCAPI_EM_ORDER_ID
            request.appendParam({
                {CCAPI_EM_ORDER_ID, this->openBuyOrder_b.get().orderId},
            });
            if (!this->credential_2_b.empty()) {
              request.setCredential(this->credential_2_b);
            }
            request.setTimeSent(messageTime);
            requestList.emplace_back(std::move(request));

            APP_LOGGER_DEBUG("############################################################################################################");
            APP_LOGGER_DEBUG("Cancel ONE open buy orders for leg b.");
            APP_LOGGER_DEBUG("#### lastCancelledOrderIdBuy_b: " + this->lastCancelledOrderIdBuy_b);
            APP_LOGGER_DEBUG("#### openBuyOrder_b.get().orderId: " + this->openBuyOrder_b.get().orderId);
            APP_LOGGER_DEBUG("############################################################################################################");

            // keep track of number of open buy orders
            this->numOpenOrdersBuy_b -= 1;
            this->lastCancelledOrderIdBuy_b = this->openBuyOrder_b.get().orderId;
          }
          if (!this->openBuyOrder_b || this->openBuyOrder_b.get().orderId.empty()) {
            APP_LOGGER_DEBUG("WARNING: buy orders id missing for cancel for leg b.");
          }
        }
      }
      APP_LOGGER_DEBUG("#################################################################################################################");
      // END LEG B
      this->orderRefreshLastTime = messageTime;
      this->cancelOpenOrdersLastTime = messageTime;
    }
  }

  // CANCEL ALL OPEN ORDERS FOR A GIVEN EXCHANGE WITHOUT KNOWING THE OPEN ORDER IDS...
  //
  // CANCEL ALL open orders for a given leg
  //
  virtual void cancelAllOpenOrders(const Event& event, Session* session, std::vector<Request>& requestList, const TimePoint& messageTime,
                                   const std::string& messageTimeISO, const std::string& productId) {
    APP_LOGGER_DEBUG("#################################################################################################################");
    APP_LOGGER_DEBUG("##### Within cancelAllOpenOrders Function");

    if (productId == "prodA") {
      APP_LOGGER_DEBUG("Buy cancel called for A, number of open buy orders for A: " + std::to_string(this->numOpenOrdersBuy_a));
      APP_LOGGER_DEBUG("Sell cancel called for A, number of open sell orders for A: " + std::to_string(this->numOpenOrdersSell_a));
    } else {
      APP_LOGGER_DEBUG("Buy cancel called for B, number of open buy orders for B: " + std::to_string(this->numOpenOrdersBuy_b));
      APP_LOGGER_DEBUG("Sell cancel called for B, number of open sell orders for B: " + std::to_string(this->numOpenOrdersSell_b));
    }

    if (productId == "prodA") {
      // Don't check that at least one order (either buy or sell) exist for the leg A -> eventually out of sync...latency
      // if((this->openBuyOrder_a && !this->openBuyOrder_a.get().orderId.empty()) || (this->openSellOrder_a && !this->openSellOrder_a.get().orderId.empty())) {

      this->cancelOpenOrdersRequestCorrelationId_a = "CANCEL_ALL_ORDERS#prodA";

      Request request(Request::Operation::CANCEL_OPEN_ORDERS, this->exchange_a, this->instrumentRest_a, this->cancelOpenOrdersRequestCorrelationId_a,
                      this->credential_a);
      if (this->exchange_a == "huobi") {
        request.appendParam({
            {CCAPI_EM_ACCOUNT_ID, this->accountId_a},
        });
      }

      if (this->exchange_a == "okx") {
        std::string trdId = this->trader_id;

        size_t pos = trdId.find('_');
        if (pos != std::string::npos) {
          trdId.erase(pos, 1);  // erase 1 character at position 'pos'
        }

        request.appendParam({{"instId", this->instrumentRest_a}});           // First value goes to instId
        request.appendParam({{"clOrdId", "prodAz" + trdId + "z" + "BUY"}});  // Next value goes to clOrdId

        request.appendParam({{"instId", this->instrumentRest_a}});            // First value goes to instId
        request.appendParam({{"clOrdId", "prodAz" + trdId + "z" + "SELL"}});  // Next value goes to clOrdId
      }

      // request.setTimeSent(messageTime);
      request.setTimeSent(tpnow());

      // APP_LOGGER_INFO("Cancel all open orders request content: " + request.toString());

      requestList.emplace_back(std::move(request));
      APP_LOGGER_DEBUG("############################################################################################################");
      APP_LOGGER_DEBUG("##### Cancel all open orders for leg A through one query -> sent.");
      APP_LOGGER_DEBUG("############################################################################################################");
      //} else {
      //  APP_LOGGER_DEBUG("############################################################################################################");
      //  APP_LOGGER_DEBUG("##### No order exist for leg A -> Don't send a Cancel All Order to the exchange.");
      //  APP_LOGGER_DEBUG("############################################################################################################");
      //}
    } else {
      // Don't check that at least one order (either buy or sell) exist for the leg B
      // if((this->openBuyOrder_b && !this->openBuyOrder_b.get().orderId.empty()) || (this->openSellOrder_b && !this->openSellOrder_b.get().orderId.empty())) {

      this->cancelOpenOrdersRequestCorrelationId_b = "CANCEL_ALL_ORDERS#prodB";

      Request request(Request::Operation::CANCEL_OPEN_ORDERS, this->exchange_b, this->instrumentRest_b, this->cancelOpenOrdersRequestCorrelationId_b,
                      this->credential_b);
      if (this->exchange_b == "huobi") {
        request.appendParam({
            {CCAPI_EM_ACCOUNT_ID, this->accountId_b},
        });
      }

      if (this->exchange_b == "okx") {
        std::string trdId = this->trader_id;

        size_t pos = trdId.find('_');
        if (pos != std::string::npos) {
          trdId.erase(pos, 1);  // erase 1 character at position 'pos'
        }

        request.appendParam({{"instId", this->instrumentRest_b}});           // First value goes to instId
        request.appendParam({{"clOrdId", "prodBz" + trdId + "z" + "BUY"}});  // Next value goes to clOrdId

        request.appendParam({{"instId", this->instrumentRest_b}});            // First value goes to instId
        request.appendParam({{"clOrdId", "prodBz" + trdId + "z" + "SELL"}});  // Next value goes to clOrdId
      }

      // request.setTimeSent(messageTime);
      request.setTimeSent(tpnow());

      // APP_LOGGER_INFO("Cancel all open orders request content: " + request.toString());

      requestList.emplace_back(std::move(request));
      APP_LOGGER_DEBUG("############################################################################################################");
      APP_LOGGER_DEBUG("##### Cancel all open orders for leg B through one query -> sent.");
      APP_LOGGER_DEBUG("############################################################################################################");
      //} else {
      //  APP_LOGGER_DEBUG("############################################################################################################");
      //  APP_LOGGER_DEBUG("##### No order exist for leg B -> Don't send a Cancel All Order to the exchange.");
      //  APP_LOGGER_DEBUG("############################################################################################################");
      //}
    }
  }

  /*
    //
    // CANCEL open orders for the two legs
    //
    virtual void cancelOpenOrdersId(const Event& event, Session* session, std::vector<Request>& requestList, const TimePoint& messageTime,
                                  const std::string& messageTimeISO, bool alwaysCancel, const std::string& productId, const std::string& side, const
    std::string& orderId) {

      APP_LOGGER_DEBUG("##############################################################################################################");
      APP_LOGGER_DEBUG("##### CANCEL OPEN ORDER FOR ID: "+ orderId);

      if(side=="buy") {
          if(productId=="prodA") {
            APP_LOGGER_DEBUG("##### Buy cancel called for A.");
          } else {
            APP_LOGGER_DEBUG("##### Buy cancel called for B.");
          }
        } else {
        if(productId=="prodA") {
            APP_LOGGER_DEBUG("##### Sell cancel called for A.");
          } else {
            APP_LOGGER_DEBUG("##### Sell cancel called for B.");
          }
        }

      // LEG A
      if(productId=="prodA") {
        if(side=="sell") {
          // CANCEL SELL
          this->cancelSellOrderRequestCorrelationId_a = "CANCEL_SELL_ORDER#prodA";

          Request request(Request::Operation::CANCEL_ORDER, this->exchange_a, this->instrumentRest_a, this->cancelSellOrderRequestCorrelationId_a,
    this->credential_a);
          // rem for huobi 2 types of orders-> see ccapi_execution_management_service_huobi.h : CCAPI_EM_ORDER_ID

          request.appendParam({
              {CCAPI_EM_ORDER_ID, orderId},
          });
          if (!this->credential_2_a.empty()) {
            request.setCredential(this->credential_2_a);
          }
          request.setTimeSent(messageTime);
          requestList.emplace_back(std::move(request));

          APP_LOGGER_DEBUG("##### Cancel ONE open sell orders for leg a -> sent to the exchange.");
        } else {
          // CANCEL BUY
          this->cancelBuyOrderRequestCorrelationId_a = "CANCEL_BUY_ORDER#prodA";

          Request request(Request::Operation::CANCEL_ORDER, this->exchange_a, this->instrumentRest_a, this->cancelBuyOrderRequestCorrelationId_a,
    this->credential_a);
          // rem for huobi 2 types of orders-> see ccapi_execution_management_service_huobi.h : CCAPI_EM_ORDER_ID

          request.appendParam({
              {CCAPI_EM_ORDER_ID, orderId},
          });
          if (!this->credential_2_a.empty()) {
            request.setCredential(this->credential_2_a);
          }
          request.setTimeSent(messageTime);
          requestList.emplace_back(std::move(request));

          APP_LOGGER_DEBUG("##### Cancel ONE open buy orders for leg a -> sent to the exchange.");
        }
      }
      // END LEG A

      // LEG B
      if(productId=="prodB") {
        if(side=="sell") {
          // CANCEL SELL
          this->cancelSellOrderRequestCorrelationId_b = "CANCEL_SELL_ORDER#prodB";

          Request request(Request::Operation::CANCEL_ORDER, this->exchange_b, this->instrumentRest_b, this->cancelSellOrderRequestCorrelationId_b,
    this->credential_b);
          // rem for huobi 2 types of orders-> see ccapi_execution_management_service_huobi.h : CCAPI_EM_ORDER_ID

          request.appendParam({
              {CCAPI_EM_ORDER_ID, orderId},
          });
          if (!this->credential_2_b.empty()) {
            request.setCredential(this->credential_2_b);
          }
          request.setTimeSent(messageTime);
          requestList.emplace_back(std::move(request));

          APP_LOGGER_DEBUG("##### Cancel ONE open sell orders for leg b -> sent to the exchange.");
        } else {
          // CANCEL BUY
          this->cancelBuyOrderRequestCorrelationId_b = "CANCEL_BUY_ORDER#prodB";

          Request request(Request::Operation::CANCEL_ORDER, this->exchange_b, this->instrumentRest_b, this->cancelBuyOrderRequestCorrelationId_b,
    this->credential_b);
          // rem for huobi 2 types of orders-> see ccapi_execution_management_service_huobi.h : CCAPI_EM_ORDER_ID

          request.appendParam({
              {CCAPI_EM_ORDER_ID, orderId},
          });
          if (!this->credential_2_b.empty()) {
            request.setCredential(this->credential_2_b);
          }
          request.setTimeSent(messageTime);
          requestList.emplace_back(std::move(request));

          APP_LOGGER_DEBUG("##### Cancel ONE open buy orders for leg b -> sent to the exchange.");
        }
      }
      // END LEG B

      APP_LOGGER_DEBUG("##############################################################################################################");
    }
    // end reworked cancel orders
  */

  // legacy
  /*
    //
    // CANCEL open orders for the two legs
    //
    virtual void cancelOpenOrders(const Event& event, Session* session, std::vector<Request>& requestList, const TimePoint& messageTime,
                                  const std::string& messageTimeISO, bool alwaysCancel, const std::string& productId, const std::string& side) {

      //APP_LOGGER_DEBUG("CANCEL FUNCTION CALLED NUMBER OF ORDER A: " +
  productId=="prodA"?std::to_string(this->numOpenOrders_a):std::to_string(this->numOpenOrders_b)); if(side=="buy") { if(productId=="prodA") {
            APP_LOGGER_DEBUG("Buy cancel called for A, number of open buy orders for A: " + std::to_string(this->numOpenOrdersBuy_a));
          } else {
            APP_LOGGER_DEBUG("Buy cancel called for B, number of open buy orders for B: " + std::to_string(this->numOpenOrdersBuy_b));
          }
        } else {
        if(productId=="prodA") {
            APP_LOGGER_DEBUG("Sell cancel called for A, number of open sell orders for A: " + std::to_string(this->numOpenOrdersSell_a));
          } else {
            APP_LOGGER_DEBUG("Sell cancel called for B, number of open sell orders for B: " + std::to_string(this->numOpenOrdersSell_b));
          }
        }

      // always cancel flag means cancelling whatever internal position states (e.g. internal missmatch with exchange at trader start)
      // leg a
      if(productId=="prodA") {
        if (alwaysCancel || (side == "sell"?(this->numOpenOrdersSell_a > 0):(this->numOpenOrdersBuy_a > 0))) {
          // seggregate between distinct exchange cancel methodology see. main.cpp
          if (this->useCancelOrderToCancelOpenOrders_a) {
            if (this->openBuyOrder_a && !this->openBuyOrder_a.get().orderId.empty() && side == "buy") {
              this->numOpenOrdersBuy_a =0;
  #ifdef CANCEL_BUY_ORDER_REQUEST_CORRELATION_ID
              this->cancelBuyOrderRequestCorrelationId_a = CANCEL_BUY_ORDER_REQUEST_CORRELATION_ID;
  #else
              this->cancelBuyOrderRequestCorrelationId_a = "CANCEL_BUY_ORDER#prodA";
              // legacy implementation
              //this->cancelBuyOrderRequestCorrelationId = messageTimeISO + "-CANCEL_BUY_ORDER";
  #endif
              Request request(Request::Operation::CANCEL_ORDER, this->exchange_a, this->instrumentRest_a, this->cancelBuyOrderRequestCorrelationId_a);
              request.appendParam({
                  {CCAPI_EM_ORDER_ID, this->openBuyOrder_a.get().orderId},
              });
              request.setTimeSent(messageTime);
              requestList.emplace_back(std::move(request));
              APP_LOGGER_DEBUG("Cancel ONE open buy orders for leg a.");
            }
            if (this->openSellOrder_a && !this->openSellOrder_a.get().orderId.empty() && side == "sell") {
              this->numOpenOrdersSell_a =0;
  #ifdef CANCEL_SELL_ORDER_REQUEST_CORRELATION_ID
              this->cancelSellOrderRequestCorrelationId_a = CANCEL_SELL_ORDER_REQUEST_CORRELATION_ID;
  #else
              this->cancelSellOrderRequestCorrelationId_a = "CANCEL_SELL_ORDER#prodA";
  #endif
              Request request(Request::Operation::CANCEL_ORDER, this->exchange_a, this->instrumentRest_a, this->cancelSellOrderRequestCorrelationId_a);
              if (!this->credential_2_a.empty()) {
                request.setCredential(this->credential_2_a);
              }
              // rem for huobi 2 types of orders-> see ccapi_execution_management_service_huobi.h : CCAPI_EM_ORDER_ID
              request.appendParam({
                  {CCAPI_EM_ORDER_ID, this->openSellOrder_a.get().orderId},
              });
              request.setTimeSent(messageTime);
              requestList.emplace_back(std::move(request));
              APP_LOGGER_DEBUG("Cancel ONE open orders sell for leg a.");
            }
          } else {
  #ifdef CANCEL_OPEN_ORDERS_REQUEST_CORRELATION_ID
            this->cancelOpenOrdersRequestCorrelationId_a = CANCEL_OPEN_ORDERS_REQUEST_CORRELATION_ID;
  #else
            this->cancelOpenOrdersRequestCorrelationId_a = "CANCEL_OPEN_ORDERS#prodA";
  #endif
            Request request(Request::Operation::CANCEL_OPEN_ORDERS, this->exchange_a, this->instrumentRest_a, this->cancelOpenOrdersRequestCorrelationId_a);
            if (this->exchange_a == "huobi") {
              request.appendParam({
                  {CCAPI_EM_ACCOUNT_ID, this->accountId_a},
              });
            }
            request.setTimeSent(messageTime);
            requestList.emplace_back(std::move(request));
            APP_LOGGER_DEBUG("Cancel all open orders for leg a.");
          }
        } else {
          // SWL if one requires the account balance at that point in time...not obvious
          //this->getAccountBalances(event, session, requestList, messageTime, messageTimeISO);
        }

        this->orderRefreshLastTime = messageTime;
        this->cancelOpenOrdersLastTime = messageTime;
      } // end cancelOpenOrders leg a

      // leg b
      if(productId=="prodB") {
        if (alwaysCancel || (side == "sell"?(this->numOpenOrdersSell_b > 0):(this->numOpenOrdersBuy_b > 0))) {
          // seggregate between distinct exchange cancel methodology see. main.cpp
          if (this->useCancelOrderToCancelOpenOrders_b) {
            if (this->openBuyOrder_b && !this->openBuyOrder_b.get().orderId.empty() && side == "buy") {
              this->numOpenOrdersBuy_b =0;
  #ifdef CANCEL_BUY_ORDER_REQUEST_CORRELATION_ID
              this->cancelBuyOrderRequestCorrelationId_b = CANCEL_BUY_ORDER_REQUEST_CORRELATION_ID;
  #else
              this->cancelBuyOrderRequestCorrelationId_b = "CANCEL_BUY_ORDER#prodB";
              // legacy implementation
              //this->cancelBuyOrderRequestCorrelationId = messageTimeISO + "-CANCEL_BUY_ORDER";
  #endif
              Request request(Request::Operation::CANCEL_ORDER, this->exchange_b, this->instrumentRest_b, this->cancelBuyOrderRequestCorrelationId_b);
              request.appendParam({
                  {CCAPI_EM_ORDER_ID, this->openBuyOrder_b.get().orderId},
              });
              request.setTimeSent(messageTime);
              requestList.emplace_back(std::move(request));
              APP_LOGGER_DEBUG("Cancel ONE open buy orders for leg b.");
            }
            if (this->openSellOrder_b && !this->openSellOrder_b.get().orderId.empty() && side == "sell") {
              this->numOpenOrdersSell_b =0;
  #ifdef CANCEL_SELL_ORDER_REQUEST_CORRELATION_ID
              this->cancelSellOrderRequestCorrelationId_b = CANCEL_SELL_ORDER_REQUEST_CORRELATION_ID;
  #else
              this->cancelSellOrderRequestCorrelationId_b = "CANCEL_SELL_ORDER#prodB";
  #endif
              Request request(Request::Operation::CANCEL_ORDER, this->exchange_b, this->instrumentRest_b, this->cancelSellOrderRequestCorrelationId_b);
              if (!this->credential_2_b.empty()) {
                request.setCredential(this->credential_2_b);
              }
              request.appendParam({
                  {CCAPI_EM_ORDER_ID, this->openSellOrder_b.get().orderId},
              });
              request.setTimeSent(messageTime);
              requestList.emplace_back(std::move(request));
              APP_LOGGER_DEBUG("Cancel ONE open sell orders for leg b.");
            }
          } else {
  #ifdef CANCEL_OPEN_ORDERS_REQUEST_CORRELATION_ID
            this->cancelOpenOrdersRequestCorrelationId_b = CANCEL_OPEN_ORDERS_REQUEST_CORRELATION_ID;
  #else
            this->cancelOpenOrdersRequestCorrelationId_b = "CANCEL_OPEN_ORDERS#prodB";
  #endif
            Request request(Request::Operation::CANCEL_OPEN_ORDERS, this->exchange_b, this->instrumentRest_b, this->cancelOpenOrdersRequestCorrelationId_b);
            if (this->exchange_b == "huobi") {
              request.appendParam({
                  {CCAPI_EM_ACCOUNT_ID, this->accountId_b},
              });
            }
            request.setTimeSent(messageTime);
            requestList.emplace_back(std::move(request));
            APP_LOGGER_DEBUG("Cancel open orders for leg b.");
          }
        } else {
          // SWL if one requires the account balance at that point in time...not obvious
          //this->getAccountBalances(event, session, requestList, messageTime, messageTimeISO);
        }
        this->orderRefreshLastTime = messageTime;
        this->cancelOpenOrdersLastTime = messageTime;
      } // end cancelOpenOrders leg b
      APP_LOGGER_DEBUG("END CANCEL FUNCTION CALLED");
    }
  */

  virtual void getAccountBalances(const Event& event, Session* session, std::vector<Request>& requestList, const TimePoint& messageTime,
                                  const std::string& messageTimeISO, const std::string& productId) {
#ifdef GET_ACCOUNT_BALANCES_REQUEST_CORRELATION_ID
    this->getAccountBalancesRequestCorrelationId = GET_ACCOUNT_BALANCES_REQUEST_CORRELATION_ID;
#else
    this->getAccountBalancesRequestCorrelationId = "GET_ACCOUNT_BALANCES";
    // this->getAccountBalancesRequestCorrelationId = messageTimeISO + "-GET_ACCOUNT_BALANCES";
#endif

    if (productId == "prodA") {
      Request request(this->useGetAccountsToGetAccountBalances_a ? Request::Operation::GET_ACCOUNTS : Request::Operation::GET_ACCOUNT_BALANCES,
                      this->exchange_a, "", this->getAccountBalancesRequestCorrelationId + "#prodA", this->credential_a);

      request.setTimeSent(messageTime);
      if (this->exchange_a == "huobi") {
        request.appendParam({
            {CCAPI_EM_ACCOUNT_ID, this->accountId_a},
        });
      } else if (this->exchange_a == "kucoin") {
        request.appendParam({
            {CCAPI_EM_ACCOUNT_TYPE, "trade"},
        });
      }
      requestList.emplace_back(std::move(request));
      this->getAccountBalancesLastTime = messageTime;
      APP_LOGGER_DEBUG("Get account balances for leg a.");

    } else {
      Request request(this->useGetAccountsToGetAccountBalances_b ? Request::Operation::GET_ACCOUNTS : Request::Operation::GET_ACCOUNT_BALANCES,
                      this->exchange_b, "", this->getAccountBalancesRequestCorrelationId + "#prodB", this->credential_b);

      request.setTimeSent(messageTime);
      if (this->exchange_b == "huobi") {
        request.appendParam({
            {CCAPI_EM_ACCOUNT_ID, this->accountId_b},
        });
      } else if (this->exchange_b == "kucoin") {
        request.appendParam({
            {CCAPI_EM_ACCOUNT_TYPE, "trade"},
        });
      }
      requestList.emplace_back(std::move(request));
      this->getAccountBalancesLastTime = messageTime;
      APP_LOGGER_DEBUG("Get account balances for leg b.");
    }
  }

  virtual void getAccountPositions(const Event& event, Session* session, std::vector<Request>& requestList, const TimePoint& messageTime,
                                   const std::string& productId) {
    if (productId == "prodA") {
      Request request(Request::Operation::GET_ACCOUNT_POSITIONS, this->exchange_a, "", "GET_ACCOUNT_POSITIONS#prodA", this->credential_a);

      request.setTimeSent(messageTime);
      if (this->exchange_a == "huobi") {
        request.appendParam({
            {CCAPI_EM_ACCOUNT_ID, this->accountId_a},
        });
      } else if (this->exchange_a == "kucoin") {
        request.appendParam({
            {CCAPI_EM_ACCOUNT_TYPE, "trade"},
        });
      } else if (this->exchange_a == "binance-coin-futures") {
        std::string underlyingSymbol = this->baseAsset_a + this->quoteAsset_a;
        request.appendParam({
            {CCAPI_UNDERLYING_SYMBOL, underlyingSymbol},
        });
      } else if (this->exchange_a == "deribit") {
        request.appendParam({
            {CCAPI_EM_ASSET, this->baseAsset_a},
        });
      }

      requestList.emplace_back(std::move(request));
      APP_LOGGER_INFO("Get account positions for leg a.");

    } else {
      Request request(Request::Operation::GET_ACCOUNT_POSITIONS, this->exchange_b, "", "GET_ACCOUNT_POSITIONS#prodB", this->credential_b);

      request.setTimeSent(messageTime);
      if (this->exchange_b == "huobi") {
        request.appendParam({
            {CCAPI_EM_ACCOUNT_ID, this->accountId_b},
        });
      } else if (this->exchange_b == "kucoin") {
        request.appendParam({
            {CCAPI_EM_ACCOUNT_TYPE, "trade"},
        });
      } else if (this->exchange_b == "binance-coin-futures") {
        std::string underlyingSymbol = this->baseAsset_b + this->quoteAsset_b;
        request.appendParam({
            {CCAPI_UNDERLYING_SYMBOL, underlyingSymbol},
        });
      } else if (this->exchange_b == "deribit") {
        request.appendParam({
            {CCAPI_EM_ASSET, this->baseAsset_b},
        });
      }

      requestList.emplace_back(std::move(request));
      APP_LOGGER_INFO("Get account positions for leg b.");
    }
  }

  /*
    virtual void onPostGetAccountBalancesMarketMaking(const TimePoint& now) {
      this->orderRefreshIntervalIndex += 1;
      if (now >= this->startTimeTp + std::chrono::seconds(this->totalDurationSeconds)) {
        APP_LOGGER_DEBUG("Exit.");
        this->promisePtr->set_value();
        this->skipProcessEvent = true;
      }
    }

    virtual void onPostGetAccountBalancesSingleOrderExecution(const TimePoint& now) {
      this->orderRefreshIntervalIndex += 1;
      if (now >= this->startTimeTp + std::chrono::seconds(this->totalDurationSeconds) ||
          (this->quoteTotalTargetQuantity > 0 ? this->theoreticalQuoteRemainingQuantity <= 0 : this->theoreticalRemainingQuantity <= 0)) {
        APP_LOGGER_DEBUG("Exit.");
        this->promisePtr->set_value();
        this->skipProcessEvent = true;
      }
    }
    */

  // swl quoter, i.e. leg==0 -> leg a, leg==1 -> leg b ## side==0 -> buy, side==1 -> sell
  virtual void placeQuote(const Event& event, Session* session, std::vector<Request>& requestList, const TimePoint& now, double quoteLevel, double quoteAmount,
                          int leg, int side, bool isPostOnly) {
    // verbose status
    // leg a
    if (leg == 0) {
      APP_LOGGER_DEBUG(this->baseAsset_a + " base balance_a is " + Decimal(UtilString::printDoubleScientific(this->baseBalance_a)).toString() + ", " +
                       this->quoteAsset_a + " quote balance_a is " + Decimal(UtilString::printDoubleScientific(this->quoteBalance_a)).toString() + ".");
      APP_LOGGER_DEBUG("Best bid price_a is " + this->bestBidPrice_a + ", best bid size_a is " + this->newBestBidSize_a + ", best ask price_a is " +
                       this->bestAskPrice_a + ", best ask size_a is " + this->newBestAskSize_a + ".");
    } else {
      APP_LOGGER_DEBUG(this->baseAsset_b + " base balance_b is " + Decimal(UtilString::printDoubleScientific(this->baseBalance_b)).toString() + ", " +
                       this->quoteAsset_b + " quote balance_b is " + Decimal(UtilString::printDoubleScientific(this->quoteBalance_b)).toString() + ".");
      APP_LOGGER_DEBUG("Best bid price_b is " + this->bestBidPrice_b + ", best bid size_b is " + this->newBestBidSize_b + ", best ask price_b is " +
                       this->bestAskPrice_b + ", best ask size_b is " + this->newBestAskSize_b + ".");
    }

    if (isPostOnly) {
      APP_LOGGER_DEBUG("##### isPostOnly order: true");
    } else {
      APP_LOGGER_DEBUG("##### isPostOnly order: false");
    }

    if (!isPostOnly) {
      APP_LOGGER_DEBUG("##### isMarketOrder order: true");
    } else {
      APP_LOGGER_DEBUG("##### isMarketOrder order: false");
    }

    // roundInput :: params -> qty, increment, roundup
    // std::string quoteLevel_std = AppUtil::roundInput(quoteLevel, leg==0?this->orderPriceIncrement_a:this->orderPriceIncrement_b, false);
    // std::string quoteAmount_std = AppUtil::roundInput(quoteAmount, leg==0?this->orderQuantityIncrement_a:this->orderQuantityIncrement_b, false);
    /*
    std::string quoteLevel_std =
    std::to_string(round(quoteLevel/std::stod(leg==0?this->orderPriceIncrement_a:this->orderPriceIncrement_b))*std::stod(leg==0?this->orderPriceIncrement_a:this->orderPriceIncrement_b));

    std::string quoteAmount_std{};
    if(leg==0) {
      //std::stod(AppUtil::roundInput(lastExecutedSize / std::stod(this->orderQuantityIncrement_a), "1", false));
      if(!this->isContractDenominated_a) {
        //quoteAmount_std = AppUtil::roundInput(quoteAmount, this->orderQuantityIncrement_a, false);
        quoteAmount_std = std::to_string(round(quoteAmount/std::stod(this->orderQuantityIncrement_a))*std::stod(this->orderQuantityIncrement_a));
      } else {
        //quoteAmount_std = AppUtil::roundInput(quoteAmount, "1.0", false);
        quoteAmount_std = std::to_string(round(quoteAmount/std::stod(this->orderQuantityIncrement_a)));
      }
    } else {
      if(!this->isContractDenominated_b) {
        //quoteAmount_std = AppUtil::roundInput(quoteAmount, this->orderQuantityIncrement_b, false);
        quoteAmount_std = std::to_string(round(quoteAmount/std::stod(this->orderQuantityIncrement_b))*std::stod(this->orderQuantityIncrement_b));
      } else {
        //quoteAmount_std = AppUtil::roundInput(quoteAmount, "1.0", false);
        quoteAmount_std = std::to_string(round(quoteAmount/std::stod(this->orderQuantityIncrement_b)));
      }
    }
    */
    std::string quoteLevel_std = std::to_string(round(quoteLevel / std::stod(leg == 0 ? this->orderPriceIncrement_a : this->orderPriceIncrement_b)) *
                                                std::stod(leg == 0 ? this->orderPriceIncrement_a : this->orderPriceIncrement_b));

    std::string quoteAmount_std{};
    if (leg == 0) {
      if (!this->isContractDenominated_a) {
        // quoteAmount_std = AppUtil::roundInput(quoteAmount, this->orderQuantityIncrement_a, false);
        quoteAmount_std = std::to_string(round(quoteAmount / std::stod(this->orderQuantityIncrement_a)) * std::stod(this->orderQuantityIncrement_a));
      } else {
        // quoteAmount_std = std::to_string(std::stod(AppUtil::roundInput(quoteAmount, this->orderQuantityIncrement_a,
        // false))/std::stod(this->orderQuantityIncrement_a));
        quoteAmount_std = std::to_string(round(quoteAmount / std::stod(this->orderQuantityIncrement_a)));
      }
    } else {
      if (!this->isContractDenominated_b) {
        // quoteAmount_std = AppUtil::roundInput(quoteAmount, this->orderQuantityIncrement_b, false);
        quoteAmount_std = std::to_string(round(quoteAmount / std::stod(this->orderQuantityIncrement_b)) * std::stod(this->orderQuantityIncrement_b));
      } else {
        // quoteAmount_std = std::to_string(std::stod(AppUtil::roundInput(quoteAmount, this->orderQuantityIncrement_b,
        // false))/std::stod(this->orderQuantityIncrement_b));
        quoteAmount_std = std::to_string(round(quoteAmount / std::stod(this->orderQuantityIncrement_b)));
      }
    }
    // std::string quoteAmount_std =
    // std::to_string(round(quoteAmount/std::stod(leg==0?this->orderQuantityIncrement_a:this->orderQuantityIncrement_b))*std::stod(leg==0?this->orderQuantityIncrement_a:this->orderQuantityIncrement_b));

    APP_LOGGER_DEBUG("Rescaled -> quoteLevel_std : " + quoteLevel_std + " quoteAmount_std : " + quoteAmount_std);

    // createRequestForCreateOrder2L(const std::string& side, const std::string& price, const std::string& quantity, const TimePoint& now, int leg)
    // check if amount is large enough to trigger an order...beware of mkt dust trades that might perturbate...
    // filter out DUST
    // Set time to live increment TTL

    // version 1
    // if quote < min order size then submit min order size
    // if(std::stod(quoteAmount_std) < std::stod(leg==0?this->orderQuantityIncrement_a:this->orderQuantityIncrement_b)) {
    // if(leg==0) {
    //    quoteAmount_std = std::to_string(this->orderQuantityIncrement_a);
    //  } else {
    //    quoteAmount_std = std::to_string(this->orderQuantityIncrement_b);
    //  }
    //}

    // version 2
    // if order size is 0 then DONT CREATE A ZERO ORDER SIZE ORDER...but resubmit an order of default order size...
    /*
    if(std::stod(quoteAmount_std) < std::stod(leg==0?this->orderQuantityIncrement_a:this->orderQuantityIncrement_b)) {

      APP_LOGGER_DEBUG("Amount not large enough to trigger an order. Then resubmit default init order size.");

            // compute order sizes
      double midPriceA{}, midPriceB{};

      midPriceA = (std::stod(this->bestBidPrice_a)+std::stod(this->newBestAskPrice_a))/2;
      midPriceB = (std::stod(this->bestBidPrice_b)+std::stod(this->bestAskPrice_b))/2;

      if(leg==0) {
        if(this->instrument_type_a != "INVERSE") {
          quoteAmount_std = std::to_string(this->typicalOrderSize/midPriceA);
        } else {
          quoteAmount_std = std::to_string(this->typicalOrderSize);
        }
      } else {
        if(this->instrument_type_b != "INVERSE") {
          quoteAmount_std = std::to_string(this->typicalOrderSize/midPriceB);
        } else {
          quoteAmount_std = std::to_string(this->typicalOrderSize);
        }
      }
    }
*/
    // if(std::stod(quoteAmount_std) >= std::stod(leg==0?this->orderQuantityIncrement_a:this->orderQuantityIncrement_b)) {
    if (std::stod(quoteAmount_std) > 0) {
      if (leg == 0) {
        if (side == 0) {
          this->numOpenOrdersBuy_a = 1;
          this->TTLopenBuyOrder_a = this->TTL;
        } else {
          this->numOpenOrdersSell_a = 1;
          this->TTLopenSellOrder_a = this->TTL;
        }
      } else {
        if (side == 0) {
          this->numOpenOrdersBuy_b = 1;
          this->TTLopenBuyOrder_b = this->TTL;
        } else {
          this->numOpenOrdersSell_b = 1;
          this->TTLopenSellOrder_b = this->TTL;
        }
      }

      // don't send a quote if qty == 0
      if (std::stod(quoteAmount_std) > 0) {
        Request request = this->createRequestForCreateOrder2L(side == 0 ? CCAPI_EM_ORDER_SIDE_BUY : CCAPI_EM_ORDER_SIDE_SELL, quoteLevel_std, quoteAmount_std,
                                                              now, leg, isPostOnly);
        requestList.emplace_back(std::move(request));
      } else {
        APP_LOGGER_DEBUG("Don't send a quote as order qty == 0");
      }
    }
    //} else {
    //  APP_LOGGER_DEBUG("Amount not large enough to trigger an order. Skip.");
    //}

    // APP_LOGGER_DEBUG(std::string("Submit a new quote for leg: ") + leg==0?"A":"B" + side==0?" Buy ":" Sell " + std::string(quoteAmount_std) +
    // std::string("@") + std::string(quoteLevel_std));

  }  // end quoter

  virtual Request createRequestForCreateOrder2L(const std::string& side, const std::string& price, const std::string& quantity, const TimePoint& now, int leg,
                                                bool isPostOnly) {
    const auto& messageTimeISO = UtilTime::getISOTimestamp<std::chrono::milliseconds>(std::chrono::time_point_cast<std::chrono::milliseconds>(now));

    // seggregate leg a and b
    Request request(Request::Operation::CREATE_ORDER, leg == 0 ? this->exchange_a : this->exchange_b,
                    leg == 0 ? this->instrumentRest_a : this->instrumentRest_b, "CREATE_ORDER_" + side + (leg == 0 ? "#prodA" : "#prodB"),
                    leg == 0 ? this->credential_a : this->credential_b);

    // legacy
    // Request request(Request::Operation::CREATE_ORDER, this->exchange, this->instrumentRest, "CREATE_ORDER_" + side);

    /*
    if(leg==0) {
      if (!this->credential_2_a.empty() && side == CCAPI_EM_ORDER_SIDE_SELL) {
        request.setCredential(this->credential_2_a);
      }
    } else {
     if (!this->credential_2_b.empty() && side == CCAPI_EM_ORDER_SIDE_SELL) {
        request.setCredential(this->credential_2_b);
      }
    }
    */

    std::map<std::string, std::string> param = {
        {CCAPI_EM_ORDER_SIDE, side}, {CCAPI_EM_ORDER_QUANTITY, quantity},
        //{CCAPI_EM_ORDER_LIMIT_PRICE, price},
    };

    // price should be set only for limit order (!=mkt order) in production and not in paper or backtest
    if ((isPostOnly && this->tradingMode == TradingMode::LIVE) || this->tradingMode != TradingMode::LIVE) {
      param.insert({CCAPI_EM_ORDER_LIMIT_PRICE, price});
    }

    if ((leg == 0 ? this->exchange_a : this->exchange_b) == "kucoin") {
      param.insert({CCAPI_EM_CLIENT_ORDER_ID, AppUtil::generateUuidV4()});
    } else {
      if (this->tradingMode == TradingMode::BACKTEST || this->tradingMode == TradingMode::PAPER) {
        std::string clientOrderId;
        clientOrderId += messageTimeISO;
        clientOrderId += "_";
        clientOrderId += side;
        param.insert({CCAPI_EM_CLIENT_ORDER_ID, clientOrderId});
      }
    }
    if ((leg == 0 ? this->exchange_a : this->exchange_b) == "huobi") {
      param.insert({CCAPI_EM_ACCOUNT_ID, (leg == 0 ? this->accountId_a : this->accountId_b)});
    }

    // update to seggregate maker (POST_ONLY) request versus Taker
    // deribit
    if (isPostOnly && (leg == 0 ? this->exchange_a : this->exchange_b) == "deribit") {
      param.insert({"post_only", "true"});
    }

    // on okx some additional parameters for a create order
    if ((leg == 0 ? this->exchange_a : this->exchange_b) == "okx") {
      std::string trdId = this->trader_id;

      size_t pos = trdId.find('_');
      if (pos != std::string::npos) {
        trdId.erase(pos, 1);  // erase 1 character at position 'pos'
      }

      if (isPostOnly) {
        param.insert({"tdMode", "cross"});
        param.insert({"clOrdId", (leg == 0 ? "prodAz" : "prodBz") + trdId + "z" + side});

        param.insert({"ordType", "post_only"});
      } else {
        param.insert({"tdMode", "cross"});
        param.insert({"clOrdId", (leg == 0 ? "prodAz" : "prodBz") + trdId + "z" + side + "Taker"});

        param.insert({"ordType", "market"});
      }
    }

    // binance-coin-futures -> order will be rejected if doesn't satisfy to maker condition see.
    // When placing order with timeInForce FOK or GTX(Post-only), if the order can't meet execution criteria,
    // order will get rejected directly and receive error response, no order_trade_update message in websocket.
    // The order can't be found in GET /fapi/v1/order or GET /fapi/v1/allOrders
    if (isPostOnly && (leg == 0 ? this->exchange_a : this->exchange_b) == "binance-coin-futures") {
      param.insert({"timeInForce", "GTX"});
    }

    // update to seggregate maker (POST_ONLY) request versus Taker
    if (!isPostOnly && (leg == 0 ? this->exchange_a : this->exchange_b) == "deribit") {
      param.insert({"type", "market"});
    }

    //"type": "MARKET"
    if (!isPostOnly && (leg == 0 ? this->exchange_a : this->exchange_b) == "binance-coin-futures") {
      param.insert({"type", "MARKET"});
    }

    // for binance coin/contract one has to specify the side of the order in case we are in one way mode
    // see. https://www.binance.com/en/support/faq/what-is-hedge-mode-and-how-to-use-it-360041513552
    // see. https://github.com/ccxt/ccxt/issues/7195#issuecomment-650705896
    /*
    if((leg==0?this->exchange_a:this->exchange_b) == "binance-coin-futures") {
      if(side==CCAPI_EM_ORDER_SIDE_BUY) {
        param.insert({"positionSide", "LONG"});
      } else {
        param.insert({"positionSide", "SHORT"});
      }
    }
    */

    request.appendParam(param);
    request.setTimeSent(tpnow());

    // APP_LOGGER_INFO("Place order request content: " + request.toString());

    APP_LOGGER_DEBUG("Place order - side: " + side + ", price: " + price + ", quantity: " + quantity + ".");
    return request;
  }

  /*
    virtual Request createRequestForCreateMktOrder2L(const std::string& side, const std::string& quantity, const TimePoint& now, int leg) {

      const auto& messageTimeISO = UtilTime::getISOTimestamp<std::chrono::milliseconds>(std::chrono::time_point_cast<std::chrono::milliseconds>(now));

      // seggregate leg a and b
      Request request(Request::Operation::CREATE_ORDER, leg==0?this->exchange_a:this->exchange_b, leg==0?this->instrumentRest_a:this->instrumentRest_b,
    "CREATE_ORDER_" + side +(leg==0?"#prodA":"#prodB"));

      // legacy
      //Request request(Request::Operation::CREATE_ORDER, this->exchange, this->instrumentRest, "CREATE_ORDER_" + side);

      if(leg==0) {
        if (!this->credential_2_a.empty() && side == CCAPI_EM_ORDER_SIDE_SELL) {
          request.setCredential(this->credential_2_a);
        }
      } else {
       if (!this->credential_2_b.empty() && side == CCAPI_EM_ORDER_SIDE_SELL) {
          request.setCredential(this->credential_2_b);
        }
      }

      // if in backtest or paper simulate mkt order with a lmt order at the bbo
      std::map<std::string, std::string> param;
      std::string price;
      if (this->tradingMode == TradingMode::BACKTEST || this->tradingMode == TradingMode::PAPER) {
        if(leg==0) {
          price = (side == CCAPI_EM_ORDER_SIDE_SELL)? this->newBestAskPrice_a:this->bestBidPrice_a;
        } else {
          price = (side == CCAPI_EM_ORDER_SIDE_SELL)? this->bestAskPrice_b:this->bestBidPrice_b;
        }
        param = {
            {CCAPI_EM_ORDER_SIDE, side},
            {CCAPI_EM_ORDER_QUANTITY, quantity},
            {CCAPI_EM_ORDER_LIMIT_PRICE, price},
        };
      } else {
        param = {
            {CCAPI_EM_ORDER_SIDE, side},
            {CCAPI_EM_ORDER_QUANTITY, quantity},
        };
        // live mode, no price for mkt order
        // some complexity involved due to lack of standard
        // e.g. Gemini -> https://docs.gemini.com/rest-api/#new-order
        // WHAT ABOUT MARKET ORDERS?
        // The API doesn't directly support market orders because they provide you with no price protection.
        // Instead, use the “immediate-or-cancel” order execution option, coupled with an aggressive limit price (i.e. very high for a buy order or very low for
    a sell order), to achieve the same result. if ((leg==0?this->exchange_a:this->exchange_b) == "gemini") { "options": ["maker-or-cancel"]
          param.insert({CCAPI_EM_CLIENT_ORDER_ID, clientOrderId});
        }

      }

      if ((leg==0?this->exchange_a:this->exchange_b) == "kucoin") {
        param.insert({CCAPI_EM_CLIENT_ORDER_ID, AppUtil::generateUuidV4()});
      } else {
        if (this->tradingMode == TradingMode::BACKTEST || this->tradingMode == TradingMode::PAPER) {
          std::string clientOrderId;
          clientOrderId += messageTimeISO;
          clientOrderId += "_";
          clientOrderId += side;
          param.insert({CCAPI_EM_CLIENT_ORDER_ID, clientOrderId});
        }
      }
      if ((leg==0?this->exchange_a:this->exchange_b) == "huobi") {
        param.insert({CCAPI_EM_ACCOUNT_ID, (leg==0?this->accountId_a:this->accountId_b)});
      }
      request.appendParam(param);
      request.setTimeSent(now);
      APP_LOGGER_DEBUG("Place order - side: " + side + ", price: " + price + ", quantity: " + quantity + ".");
      return request;
    }
  */

  virtual Request createRequestForCreateOrder(const std::string& side, const std::string& price, const std::string& quantity, const TimePoint& now) {
    const auto& messageTimeISO = UtilTime::getISOTimestamp<std::chrono::milliseconds>(std::chrono::time_point_cast<std::chrono::milliseconds>(now));
    Request request(Request::Operation::CREATE_ORDER, this->exchange, this->instrumentRest, "CREATE_ORDER_" + side);

    if (!this->credential_2.empty() && side == CCAPI_EM_ORDER_SIDE_SELL) {
      request.setCredential(this->credential_2);
    }
    std::map<std::string, std::string> param = {
        {CCAPI_EM_ORDER_SIDE, side},
        {CCAPI_EM_ORDER_QUANTITY, quantity},
        {CCAPI_EM_ORDER_LIMIT_PRICE, price},
    };
    if (this->exchange == "kucoin") {
      param.insert({CCAPI_EM_CLIENT_ORDER_ID, AppUtil::generateUuidV4()});
    } else {
      if (this->tradingMode == TradingMode::BACKTEST || this->tradingMode == TradingMode::PAPER) {
        std::string clientOrderId;
        clientOrderId += messageTimeISO;
        clientOrderId += "_";
        clientOrderId += side;
        param.insert({CCAPI_EM_CLIENT_ORDER_ID, clientOrderId});
      }
    }
    if (this->exchange == "huobi") {
      param.insert({CCAPI_EM_ACCOUNT_ID, this->accountId});
    }
    request.appendParam(param);
    request.setTimeSent(now);
    APP_LOGGER_DEBUG("Place order - side: " + side + ", price: " + price + ", quantity: " + quantity + ".");
    return request;
  }

  virtual void extractOrderInfo(Element& element, const Order& order) {
    element.insert(CCAPI_EM_ORDER_ID, order.orderId);
    element.insert(CCAPI_EM_CLIENT_ORDER_ID, order.clientOrderId);
    element.insert(CCAPI_EM_ORDER_SIDE, order.side);
    element.insert(CCAPI_EM_ORDER_LIMIT_PRICE, order.limitPrice.toString());
    element.insert(CCAPI_EM_ORDER_QUANTITY, order.quantity.toString());
    element.insert(CCAPI_EM_ORDER_CUMULATIVE_FILLED_QUANTITY, order.cumulativeFilledQuantity.toString());
    element.insert(CCAPI_EM_ORDER_REMAINING_QUANTITY, order.remainingQuantity.toString());
    element.insert(CCAPI_EM_ORDER_STATUS, order.status);
  }

  //
  // Fake server response
  //
  //  virtual void fakeServerResponse(const Event& event, Session* session, std::vector<Request>& requestList, const TimePoint& messageTime,
  //                                  const std::string& messageTimeISO) {
  virtual void fakeServerResponse(Session* session, std::vector<Request>& requestList) {
    APP_LOGGER_DEBUG("####################################################################");
    APP_LOGGER_DEBUG("# Inside fakeserverresponse, loop through request list, with request list size" + std::to_string(requestList.size()));

    for (const auto& request : requestList) {
      APP_LOGGER_DEBUG(request.toString());

      // SWL: retrieve correlation Id of each request (one correlation id per request, i.e. action#prodA/B)
      const std::string correlationId_req = request.getCorrelationId();

      auto correlationIdTocken = UtilString::split(correlationId_req, '#');
      const std::string correlationId = correlationIdTocken.at(0);
      const std::string productId = correlationIdTocken.at(1);

      bool createdBuyOrder_a = false;
      bool createdBuyOrder_b = false;
      const auto& now = request.getTimeSent();
      Event virtualEvent;
      Event virtualEvent_2;
      Event virtualEvent_3;
      Message message;
      Message message_2;
      message.setTime(now);
      message.setTimeReceived(now);
      message_2.setTime(now);
      message_2.setTimeReceived(now);
      message_2.setCorrelationIdList({request.getCorrelationId()});

      std::vector<Element> elementList;
      const auto& operation = request.getOperation();
      if (operation == Request::Operation::GET_ACCOUNT_BALANCES || operation == Request::Operation::GET_ACCOUNTS) {
        virtualEvent.setType(Event::Type::RESPONSE);
        message.setCorrelationIdList({request.getCorrelationId()});
        message.setType(operation == Request::Operation::GET_ACCOUNT_BALANCES ? Message::Type::GET_ACCOUNT_BALANCES : Message::Type::GET_ACCOUNTS);
        std::vector<Element> elementList;
        {
          Element element;
          element.insert(CCAPI_EM_ASSET, productId == "prodA" ? this->baseAsset_a : this->baseAsset_b);

          // BEWARE THE FOLLOWING INFORMATION IS WRONG! ONE SHOULD SUBSTRACT THE VALUE OF THE DERIVATIVE CONTRACTS...
          element.insert(CCAPI_EM_QUANTITY_AVAILABLE_FOR_TRADING,
                         Decimal(UtilString::printDoubleScientific(productId == "prodA" ? this->baseBalance_a : this->baseBalance_b)).toString());
          // Decimal(UtilString::printDoubleScientific(this->baseBalance / this->baseAvailableBalanceProportion)).toString());
          elementList.emplace_back(std::move(element));
        }
        {
          Element element;
          element.insert(CCAPI_EM_ASSET, productId == "prodA" ? this->quoteAsset_a : this->quoteAsset_b);
          element.insert(CCAPI_EM_QUANTITY_AVAILABLE_FOR_TRADING,
                         Decimal(UtilString::printDoubleScientific(productId == "prodA" ? this->quoteBalance_a : this->quoteBalance_b)).toString());
          // Decimal(UtilString::printDoubleScientific(this->quoteBalance / this->quoteAvailableBalanceProportion)).toString());
          elementList.emplace_back(std::move(element));
        }
        message.setElementList(elementList);
        std::vector<Message> messageList;
        messageList.emplace_back(std::move(message));
        virtualEvent.setMessageList(messageList);
      } else if (operation == Request::Operation::GET_ACCOUNT_POSITIONS) {
        /*
                  element.insert(CCAPI_INSTRUMENT, x["instrument_name"].GetString());
                  element.insert(CCAPI_EM_POSITION_QUANTITY, x["size"].GetString());
                  element.insert(CCAPI_EM_POSITION_ENTRY_PRICE, x["average_price"].GetString());
                  element.insert(CCAPI_EM_POSITION_LEVERAGE, x["leverage"].GetString());

                  // JFA: add direction to return object
                   element.insert(CCAPI_EM_POSITION_SIDE,
           std::string(x["direction"].GetString())=="buy"?CCAPI_EM_POSITION_SIDE_LONG:CCAPI_EM_POSITION_SIDE_SHORT); // -> Direction: buy, sell or zero

        */
        virtualEvent.setType(Event::Type::RESPONSE);
        message.setCorrelationIdList({request.getCorrelationId()});
        message.setType(Message::Type::GET_ACCOUNT_POSITIONS);
        std::vector<Element> elementList;
        {
          Element element;

          element.insert(CCAPI_INSTRUMENT, productId == "prodA" ? this->instrument_a : this->instrument_b);

          if (productId == "prodA") {
            // seggregate between inverse/linear & contracts/usd denominated
            if (this->instrument_type_a != "INVERSE") {
              if (!isContractDenominated_a) {
                element.insert(CCAPI_EM_POSITION_QUANTITY, Decimal(UtilString::printDoubleScientific(abs(this->baseBalance_a))).toString());
                element.insert(CCAPI_EM_POSITION_SIDE, this->baseBalance_a < 0 ? CCAPI_EM_POSITION_SIDE_LONG : CCAPI_EM_POSITION_SIDE_SHORT);
              } else {
                element.insert(CCAPI_EM_POSITION_QUANTITY,
                               Decimal(UtilString::printDoubleScientific(abs(this->baseBalance_a / std::stod(this->orderQuantityIncrement_a)))).toString());
                element.insert(CCAPI_EM_POSITION_SIDE, this->baseBalance_a < 0 ? CCAPI_EM_POSITION_SIDE_LONG : CCAPI_EM_POSITION_SIDE_SHORT);
              }
            } else {
              if (!isContractDenominated_a) {
                element.insert(CCAPI_EM_POSITION_QUANTITY, Decimal(UtilString::printDoubleScientific(abs(this->quoteBalance_a))).toString());
                element.insert(CCAPI_EM_POSITION_SIDE, this->quoteBalance_a < 0 ? CCAPI_EM_POSITION_SIDE_LONG : CCAPI_EM_POSITION_SIDE_SHORT);
              } else {
                element.insert(CCAPI_EM_POSITION_QUANTITY,
                               Decimal(UtilString::printDoubleScientific(abs(this->quoteBalance_a / std::stod(this->orderQuantityIncrement_a)))).toString());
                element.insert(CCAPI_EM_POSITION_SIDE, this->quoteBalance_a < 0 ? CCAPI_EM_POSITION_SIDE_LONG : CCAPI_EM_POSITION_SIDE_SHORT);
              }
            }
          } else {
            // seggregate between inverse/linear & contracts/usd denominated
            if (this->instrument_type_b != "INVERSE") {
              if (!isContractDenominated_b) {
                element.insert(CCAPI_EM_POSITION_QUANTITY, Decimal(UtilString::printDoubleScientific(abs(this->baseBalance_b))).toString());
                element.insert(CCAPI_EM_POSITION_SIDE, this->baseBalance_b < 0 ? CCAPI_EM_POSITION_SIDE_LONG : CCAPI_EM_POSITION_SIDE_SHORT);
              } else {
                element.insert(CCAPI_EM_POSITION_QUANTITY,
                               Decimal(UtilString::printDoubleScientific(abs(this->baseBalance_b / std::stod(this->orderQuantityIncrement_b)))).toString());
                element.insert(CCAPI_EM_POSITION_SIDE, this->baseBalance_b < 0 ? CCAPI_EM_POSITION_SIDE_LONG : CCAPI_EM_POSITION_SIDE_SHORT);
              }
            } else {
              if (!isContractDenominated_b) {
                element.insert(CCAPI_EM_POSITION_QUANTITY, Decimal(UtilString::printDoubleScientific(abs(this->quoteBalance_b))).toString());
                element.insert(CCAPI_EM_POSITION_SIDE, this->quoteBalance_b < 0 ? CCAPI_EM_POSITION_SIDE_LONG : CCAPI_EM_POSITION_SIDE_SHORT);
              } else {
                element.insert(CCAPI_EM_POSITION_QUANTITY,
                               Decimal(UtilString::printDoubleScientific(abs(this->quoteBalance_b / std::stod(this->orderQuantityIncrement_b)))).toString());
                element.insert(CCAPI_EM_POSITION_SIDE, this->quoteBalance_b < 0 ? CCAPI_EM_POSITION_SIDE_LONG : CCAPI_EM_POSITION_SIDE_SHORT);
              }
            }
          }
          elementList.emplace_back(std::move(element));
        }
        message.setElementList(elementList);
        std::vector<Message> messageList;
        messageList.emplace_back(std::move(message));
        virtualEvent.setMessageList(messageList);
      } else if (operation == Request::Operation::CREATE_ORDER) {
        auto newBaseBalance = productId == "prodA" ? this->baseBalance_a : this->baseBalance_b;
        auto newQuoteBalance = productId == "prodA" ? this->quoteBalance_a : this->quoteBalance_b;
        const auto& param = request.getParamList().at(0);
        const auto& side = param.at(CCAPI_EM_ORDER_SIDE);
        const auto& price = param.at(CCAPI_EM_ORDER_LIMIT_PRICE);
        const auto& quantity = param.at(CCAPI_EM_ORDER_QUANTITY);
        const auto& clientOrderId = param.at(CCAPI_EM_CLIENT_ORDER_ID);
        // bool sufficientBalance = false;
        bool sufficientBalance = true;
        double transactedAmount = 0.0;

        // the following calculation is only performed to assess the sufficient amount not to update the balance (quote/base) therefore the maker fees is used
        // even though we don't know which fees (maker/taker) should be considered...
        /*
                    if(this->instrument_type_b != "INVERSE") {
                      this->baseBalance_b += lastExecutedSize;
                      this->quoteBalance_b -= lastExecutedPrice * lastExecutedSize;
                    } else {
                      this->baseBalance_b += lastExecutedSize/lastExecutedPrice;
                      this->quoteBalance_b -= lastExecutedSize;
                    }
        */
        /*
        if (side == CCAPI_EM_ORDER_SIDE_BUY) {
          //if(productId=="prodA"?(instrument_type_a != "INVERSE"):(instrument_type_b != "INVERSE")) {
          //  transactedAmount = std::stod(price) * std::stod(quantity);
          //} else {
          //  transactedAmount = std::stod(quantity);
          //}

          //double transactedAmount = std::stod(price) * std::stod(quantity);
          if (UtilString::toLower(productId=="prodA"?this->makerBuyerFeeAsset_a:this->makerBuyerFeeAsset_b) ==
        UtilString::toLower(productId=="prodA"?this->quoteAsset_a:this->quoteAsset_b)) { transactedAmount *= 1 +
        (productId=="prodA"?this->makerFee_a:this->makerFee_b);
          }
          newQuoteBalance -= transactedAmount;
          if (newQuoteBalance >= 0) {
            sufficientBalance = true;
          } else {
            APP_LOGGER_DEBUG("Insufficient quote balance for product: " + productId);
          }
        } else if (side == CCAPI_EM_ORDER_SIDE_SELL) {
          if(productId=="prodA"?(instrument_type_a != "INVERSE"):(instrument_type_b != "INVERSE")) {
            transactedAmount = std::stod(quantity);
          } else {
            transactedAmount = std::stod(quantity)/std::stod(price);
          }

          // double transactedAmount = std::stod(quantity);
          if (UtilString::toLower(productId=="prodA"?this->makerSellerFeeAsset_a:this->makerSellerFeeAsset_b) ==
        UtilString::toLower(productId=="prodA"?this->baseAsset_a:this->baseAsset_b)) { transactedAmount *= 1 +
        (productId=="prodA"?this->makerFee_a:this->makerFee_b);
          }
          newBaseBalance -= transactedAmount;
          if (newBaseBalance >= 0) {
            sufficientBalance = true;
          } else {
            APP_LOGGER_DEBUG("Insufficient base balance for product: " + productId);
          }

          APP_LOGGER_DEBUG("quantity"+quantity);
          APP_LOGGER_DEBUG("price"+price);
          APP_LOGGER_DEBUG("transactedAmount"+Decimal(UtilString::printDoubleScientific(transactedAmount)).toString());
          APP_LOGGER_DEBUG("newBaseBalance"+Decimal(UtilString::printDoubleScientific(newBaseBalance)).toString());
        }
        */

        // we don't check if there is sufficient balance
        sufficientBalance = true;

        if (sufficientBalance) {
          virtualEvent.setType(Event::Type::SUBSCRIPTION_DATA);
          message.setCorrelationIdList({std::string(CCAPI_EM_PRIVATE_TRADE) + (productId == "prodA" ? "#prodA" : "#prodB")});
          // message.setCorrelationIdList({PRIVATE_SUBSCRIPTION_DATA_CORRELATION_ID});
          message.setType(Message::Type::EXECUTION_MANAGEMENT_EVENTS_ORDER_UPDATE);
          Order order;
          order.orderId = std::to_string(++(productId == "prodA" ? this->virtualOrderId_a : this->virtualOrderId_b));
          order.clientOrderId = clientOrderId;
          order.side = side;
          order.limitPrice = Decimal(price);
          order.quantity = Decimal(quantity);
          order.cumulativeFilledQuantity = Decimal("0");
          order.remainingQuantity = order.quantity;
          order.status = APP_EVENT_HANDLER_BASE_ORDER_STATUS_NEW;
          Element element;
          this->extractOrderInfo(element, order);
          if (productId == "prodA") {
            createdBuyOrder_a = side == CCAPI_EM_ORDER_SIDE_BUY;
          } else {
            createdBuyOrder_b = side == CCAPI_EM_ORDER_SIDE_BUY;
          }
          //
          if (productId == "prodA") {
            if (createdBuyOrder_a) {
              this->openBuyOrder_a = order;
            } else {
              this->openSellOrder_a = order;
            }
          } else {
            if (createdBuyOrder_b) {
              this->openBuyOrder_b = order;
            } else {
              this->openSellOrder_b = order;
            }
          }

          std::vector<Element> elementList;
          std::vector<Element> elementList_2;
          elementList.emplace_back(std::move(element));
          elementList_2 = elementList;
          message.setElementList(elementList);
          std::vector<Message> messageList;
          messageList.emplace_back(std::move(message));
          virtualEvent.setMessageList(messageList);
          virtualEvent_2.setType(Event::Type::RESPONSE);
          message_2.setType(Message::Type::CREATE_ORDER);
          message_2.setElementList(elementList_2);
          std::vector<Message> messageList_2;
          messageList_2.emplace_back(std::move(message_2));
          virtualEvent_2.setMessageList(messageList_2);
        } else {
          virtualEvent_2.setType(Event::Type::RESPONSE);
          message_2.setType(Message::Type::RESPONSE_ERROR);
          Element element;
          element.insert(CCAPI_ERROR_MESSAGE, "insufficient balance");
          std::vector<Element> elementList;
          elementList.emplace_back(std::move(element));
          message_2.setElementList(elementList);
          std::vector<Message> messageList_2;
          messageList_2.emplace_back(std::move(message_2));
          virtualEvent_2.setMessageList(messageList_2);
        }
      } else if (operation == Request::Operation::CANCEL_OPEN_ORDERS) {
        virtualEvent.setType(Event::Type::SUBSCRIPTION_DATA);

        // the two next lines should be equivalent as SWL don't consider the correlation id for action type
        // message.setCorrelationIdList({std::string(CCAPI_EM_PRIVATE_TRADE)+(productId=="prodA"?"#prodA":"#prodB")});
        message.setCorrelationIdList({std::string(CCAPI_EM_ORDER_UPDATE) + (productId == "prodA" ? "#prodA" : "#prodB")});

        // legacy
        // message.setCorrelationIdList({PRIVATE_SUBSCRIPTION_DATA_CORRELATION_ID});
        message.setType(Message::Type::EXECUTION_MANAGEMENT_EVENTS_ORDER_UPDATE);
        if (productId == "prodA" ? this->openBuyOrder_a : this->openBuyOrder_b) {
          if (productId == "prodA") {
            this->openBuyOrder_a.get().status = APP_EVENT_HANDLER_BASE_ORDER_STATUS_CANCELED;
          } else {
            this->openBuyOrder_b.get().status = APP_EVENT_HANDLER_BASE_ORDER_STATUS_CANCELED;
          }
          Element element;
          this->extractOrderInfo(element, productId == "prodA" ? this->openBuyOrder_a.get() : this->openBuyOrder_b.get());
          elementList.emplace_back(std::move(element));

          if (productId == "prodA") {
            this->openBuyOrder_a = boost::none;
          } else {
            this->openBuyOrder_b = boost::none;
          }
        }

        if (productId == "prodA" ? this->openSellOrder_a : this->openSellOrder_b) {
          if (productId == "prodA") {
            this->openSellOrder_a.get().status = APP_EVENT_HANDLER_BASE_ORDER_STATUS_CANCELED;
          } else {
            this->openSellOrder_b.get().status = APP_EVENT_HANDLER_BASE_ORDER_STATUS_CANCELED;
          }
          Element element;
          this->extractOrderInfo(element, productId == "prodA" ? this->openSellOrder_a.get() : this->openSellOrder_b.get());
          elementList.emplace_back(std::move(element));
          if (productId == "prodA") {
            this->openSellOrder_a = boost::none;
          } else {
            this->openSellOrder_b = boost::none;
          }
        }

        std::vector<Element> elementList_2;
        if (!elementList.empty()) {
          elementList_2 = elementList;
          message.setElementList(elementList);
          virtualEvent.setMessageList({message});
        }
        virtualEvent_2.setType(Event::Type::RESPONSE);
        message_2.setType(Message::Type::CANCEL_OPEN_ORDERS);
        message_2.setElementList(elementList_2);
        std::vector<Message> messageList_2;
        messageList_2.emplace_back(std::move(message_2));
        virtualEvent_2.setMessageList(messageList_2);
      } else if (operation == Request::Operation::CANCEL_ORDER) {
        virtualEvent.setType(Event::Type::SUBSCRIPTION_DATA);
        // the two next lines should be equivalent as SWL don't consider the correlation id for action type
        // message.setCorrelationIdList({std::string(CCAPI_EM_PRIVATE_TRADE)+(productId=="prodA"?"#prodA":"#prodB")});
        message.setCorrelationIdList({std::string(CCAPI_EM_ORDER_UPDATE) + (productId == "prodA" ? "#prodA" : "#prodB")});

        // legacy implementation
        // message.setCorrelationIdList({PRIVATE_SUBSCRIPTION_DATA_CORRELATION_ID});
        message.setType(Message::Type::EXECUTION_MANAGEMENT_EVENTS_ORDER_UPDATE);

        // cancelBuyOrderRequestCorrelationId
        if (productId == "prodA") {
          if (this->openBuyOrder_a && request.getCorrelationId() == this->cancelBuyOrderRequestCorrelationId_a) {
            this->openBuyOrder_a.get().status = APP_EVENT_HANDLER_BASE_ORDER_STATUS_CANCELED;
            Element element;
            this->extractOrderInfo(element, this->openBuyOrder_a.get());
            elementList.emplace_back(std::move(element));
            // order set to none within order cancel response...duplicate init order state -> flag: JFACANCEL
            // this->openBuyOrder_a = boost::none;
          }
        } else {
          if (this->openBuyOrder_b && request.getCorrelationId() == this->cancelBuyOrderRequestCorrelationId_b) {
            this->openBuyOrder_b.get().status = APP_EVENT_HANDLER_BASE_ORDER_STATUS_CANCELED;
            Element element;
            this->extractOrderInfo(element, this->openBuyOrder_b.get());
            elementList.emplace_back(std::move(element));
            // order set to none within order cancel response...duplicate init order state -> flag: JFACANCEL
            // this->openBuyOrder_b = boost::none;
          }
        }

        // cancelSellOrderRequestCorrelationId
        if (productId == "prodA") {
          if (this->openSellOrder_a && request.getCorrelationId() == this->cancelSellOrderRequestCorrelationId_a) {
            this->openSellOrder_a.get().status = APP_EVENT_HANDLER_BASE_ORDER_STATUS_CANCELED;
            Element element;
            this->extractOrderInfo(element, this->openSellOrder_a.get());
            elementList.emplace_back(std::move(element));
            // order set to none within order cancel response...duplicate init order state -> flag: JFACANCEL
            // this->openSellOrder_a = boost::none;
          }
        } else {
          if (this->openSellOrder_b && request.getCorrelationId() == this->cancelSellOrderRequestCorrelationId_b) {
            this->openSellOrder_b.get().status = APP_EVENT_HANDLER_BASE_ORDER_STATUS_CANCELED;
            Element element;
            this->extractOrderInfo(element, this->openSellOrder_b.get());
            elementList.emplace_back(std::move(element));
            // order set to none within order cancel response...duplicate init order state -> flag: JFACANCEL
            // this->openSellOrder_b = boost::none;
          }
        }

        std::vector<Element> elementList_2;
        if (!elementList.empty()) {
          elementList_2 = elementList;
          message.setElementList(elementList);
          virtualEvent.setMessageList({message});
        }
        virtualEvent_2.setType(Event::Type::RESPONSE);

        message_2.setType(Message::Type::CANCEL_ORDER);
        message_2.setElementList(elementList_2);
        std::vector<Message> messageList_2;
        messageList_2.emplace_back(std::move(message_2));
        virtualEvent_2.setMessageList(messageList_2);
      }
      if (!virtualEvent.getMessageList().empty()) {
        // APP_LOGGER_DEBUG("Generated a virtual event: " + virtualEvent.toStringPretty());
        this->processEvent(virtualEvent, session);
      }
      if (!virtualEvent_2.getMessageList().empty()) {
        // APP_LOGGER_DEBUG("Generated a virtual event: " + virtualEvent_2.toStringPretty());
        this->processEvent(virtualEvent_2, session);
      }

      // if action is create order then reply after matching available mkt qty
      // create order leg a
      if (operation == Request::Operation::CREATE_ORDER && productId == "prodA") {
        if (this->openBuyOrder_a || this->openSellOrder_a) {
          std::vector<Element> elementListPrivateTrade;
          auto it = this->snapshotAsk_a.begin();
          auto rit = this->snapshotBid_a.rbegin();
          Order matchedOrder = createdBuyOrder_a ? this->openBuyOrder_a.get() : this->openSellOrder_a.get();
          std::string takerFeeAsset = createdBuyOrder_a ? this->takerBuyerFeeAsset_a : this->takerSellerFeeAsset_a;

          while ((createdBuyOrder_a && it != this->snapshotAsk_a.end()) || (!createdBuyOrder_a && rit != this->snapshotBid_a.rend())) {
            Decimal priceToMatch = createdBuyOrder_a ? it->first : rit->first;
            if ((createdBuyOrder_a && this->openBuyOrder_a->limitPrice >= priceToMatch) ||
                (!createdBuyOrder_a && this->openSellOrder_a->limitPrice <= priceToMatch)) {
              APP_LOGGER_DEBUG("#########################################################################");
              APP_LOGGER_DEBUG("Consume order book leg A -> see taker order");

              Decimal quantityToMatch = Decimal(createdBuyOrder_a ? it->second : rit->second);
              // Decimal matchedQuantity = std::min(quantityToMatch, matchedOrder.remainingQuantity); // if two next lines uncommented then comment that line
              Decimal matchedQuantity = matchedOrder.remainingQuantity;  // JFA -> DIFF TO REALITY

              // priceToMatch = Decimal(createdBuyOrder_a?this->bestAskPrice_a:this->bestBidPrice_a);//JFA -> DIFF TO REALITY

              matchedOrder.cumulativeFilledQuantity = matchedOrder.cumulativeFilledQuantity.add(matchedQuantity);
              matchedOrder.remainingQuantity = matchedOrder.remainingQuantity.subtract(matchedQuantity);

              //

              /*
                                  if(this->instrument_type_b != "INVERSE") {
                                    if(!this->isContractDenominated_b) {
                                      this->baseBalance_b += lastFilledQuantity.toDouble();
                                      this->quoteBalance_b -= order.limitPrice.toDouble() * lastFilledQuantity.toDouble();
                                    } else {
                                      this->baseBalance_b += lastFilledQuantity.toDouble() * std::stod(this->orderQuantityIncrement_b);
                                      this->quoteBalance_b -= order.limitPrice.toDouble() * lastFilledQuantity.toDouble() *
                 std::stod(this->orderQuantityIncrement_b);
                                    }
                                  } else {
                                    if(!this->isContractDenominated_b) {
                                      this->baseBalance_b += lastFilledQuantity.toDouble()/order.limitPrice.toDouble();
                                      this->quoteBalance_b -= lastFilledQuantity.toDouble();
                                    } else {
                                      this->baseBalance_b += (lastFilledQuantity.toDouble() *
                 std::stod(this->orderQuantityIncrement_b))/order.limitPrice.toDouble(); this->quoteBalance_b -= lastFilledQuantity.toDouble() *
                 std::stod(this->orderQuantityIncrement_b);
                                    }
                                  }

              */

              if (createdBuyOrder_a) {
                if (this->instrument_type_a != "INVERSE") {
                  if (!this->isContractDenominated_a) {
                    this->baseBalance_a += matchedQuantity.toDouble();
                    this->quoteBalance_a -= priceToMatch.toDouble() * matchedQuantity.toDouble();
                  } else {
                    this->baseBalance_a += matchedQuantity.toDouble() * std::stod(this->orderQuantityIncrement_a);
                    this->quoteBalance_a -= priceToMatch.toDouble() * matchedQuantity.toDouble() * std::stod(this->orderQuantityIncrement_a);
                  }
                } else {
                  if (!this->isContractDenominated_a) {
                    this->baseBalance_a += matchedQuantity.toDouble() / priceToMatch.toDouble();
                    this->quoteBalance_a -= matchedQuantity.toDouble();
                  } else {
                    this->baseBalance_a += (matchedQuantity.toDouble() * std::stod(this->orderQuantityIncrement_a)) / priceToMatch.toDouble();
                    this->quoteBalance_a -= matchedQuantity.toDouble() * std::stod(this->orderQuantityIncrement_a);
                  }
                }
              } else {
                if (this->instrument_type_a != "INVERSE") {
                  if (!this->isContractDenominated_a) {
                    this->baseBalance_a -= matchedQuantity.toDouble();
                    this->quoteBalance_a += priceToMatch.toDouble() * matchedQuantity.toDouble();
                  } else {
                    this->baseBalance_a -= matchedQuantity.toDouble() * std::stod(this->orderQuantityIncrement_a);
                    this->quoteBalance_a += priceToMatch.toDouble() * matchedQuantity.toDouble() * std::stod(this->orderQuantityIncrement_a);
                  }
                } else {
                  if (!this->isContractDenominated_a) {
                    this->baseBalance_a -= matchedQuantity.toDouble() / priceToMatch.toDouble();
                    this->quoteBalance_a += matchedQuantity.toDouble();
                  } else {
                    this->baseBalance_a -= (matchedQuantity.toDouble() * std::stod(this->orderQuantityIncrement_a)) / priceToMatch.toDouble();
                    this->quoteBalance_a += matchedQuantity.toDouble() * std::stod(this->orderQuantityIncrement_a);
                  }
                }
              }

              /* legacy 2
                            if (createdBuyOrder_a) {
                              if(instrument_type_a != "INVERSE") {
                                this->baseBalance_a += matchedQuantity.toDouble();
                                this->quoteBalance_a -= priceToMatch.toDouble() * matchedQuantity.toDouble();
                              } else {
                                this->baseBalance_a += matchedQuantity.toDouble() / priceToMatch.toDouble();
                                this->quoteBalance_a -= matchedQuantity.toDouble();
                              }
                            } else {
                              if(instrument_type_a != "INVERSE") {
                                this->baseBalance_a -= matchedQuantity.toDouble();
                                this->quoteBalance_a += priceToMatch.toDouble() * matchedQuantity.toDouble();
                              } else {
                                this->baseBalance_a -= matchedQuantity.toDouble() / priceToMatch.toDouble();
                                this->quoteBalance_a += matchedQuantity.toDouble();
                              }
                            }
              */
              /*
              // legacy
              if (createdBuyOrder_a) {
                this->baseBalance_a += matchedQuantity.toDouble();
                this->quoteBalance_a -= priceToMatch.toDouble() * matchedQuantity.toDouble();
              } else {
                this->baseBalance_a -= matchedQuantity.toDouble();
                this->quoteBalance_a += priceToMatch.toDouble() * matchedQuantity.toDouble();
              }
              */
              /*
              double feeQuantity = 0;
              if (UtilString::toLower(takerFeeAsset) == UtilString::toLower(this->baseAsset_a)) {
                if(instrument_type_a != "INVERSE") {
                  feeQuantity = matchedQuantity.toDouble() * this->takerFee_a;
                  this->baseBalance_a -= feeQuantity;
                } else {
                  feeQuantity = matchedQuantity.toDouble()/priceToMatch.toDouble() * this->takerFee_a;
                  this->baseBalance_a -= feeQuantity;
                }
              } else if (UtilString::toLower(takerFeeAsset) == UtilString::toLower(this->quoteAsset_a)) {
                if(instrument_type_a != "INVERSE") {
                  feeQuantity = priceToMatch.toDouble() * matchedQuantity.toDouble() * this->takerFee_a;
                  this->quoteBalance_a -= feeQuantity;
                } else {
                    feeQuantity = priceToMatch.toDouble() * this->takerFee_a;
                  this->quoteBalance_a -= feeQuantity;
                }
              }
              */
              double feeQuantity = 0;
              if (UtilString::toLower(takerFeeAsset) == UtilString::toLower(this->baseAsset_a)) {
                if (instrument_type_a != "INVERSE") {
                  if (!this->isContractDenominated_a) {
                    feeQuantity = matchedQuantity.toDouble() * this->takerFee_a;
                  } else {
                    feeQuantity = matchedQuantity.toDouble() * std::stod(this->orderQuantityIncrement_a) * this->takerFee_a;
                  }
                } else {
                  if (!this->isContractDenominated_a) {
                    feeQuantity = matchedQuantity.toDouble() / priceToMatch.toDouble() * this->takerFee_a;
                  } else {
                    feeQuantity = (matchedQuantity.toDouble() * std::stod(this->orderQuantityIncrement_a)) / priceToMatch.toDouble() * this->takerFee_a;
                  }
                }

                this->baseBalance_a -= feeQuantity;
              } else if (UtilString::toLower(takerFeeAsset) == UtilString::toLower(this->quoteAsset_a)) {
                if (instrument_type_a != "INVERSE") {
                  if (!this->isContractDenominated_a) {
                    feeQuantity = priceToMatch.toDouble() * matchedQuantity.toDouble() * this->takerFee_a;
                  } else {
                    feeQuantity = priceToMatch.toDouble() * matchedQuantity.toDouble() * std::stod(this->orderQuantityIncrement_a) * this->takerFee_a;
                  }
                } else {
                  if (!this->isContractDenominated_a) {
                    feeQuantity = matchedQuantity.toDouble() * this->takerFee_a;
                  } else {
                    feeQuantity = matchedQuantity.toDouble() * std::stod(this->orderQuantityIncrement_a) * this->takerFee_a;
                  }
                }
                this->quoteBalance_a -= feeQuantity;
              }

              Element element;
              element.insert(CCAPI_TRADE_ID, std::to_string(++this->virtualTradeId_a));
              element.insert(CCAPI_EM_ORDER_LAST_EXECUTED_PRICE, priceToMatch.toString());
              element.insert(CCAPI_EM_ORDER_LAST_EXECUTED_SIZE, matchedQuantity.toString());
              element.insert(CCAPI_EM_ORDER_SIDE, matchedOrder.side);
              element.insert(CCAPI_IS_MAKER, "0");
              element.insert(CCAPI_EM_ORDER_ID, matchedOrder.orderId);
              element.insert(CCAPI_EM_CLIENT_ORDER_ID, matchedOrder.clientOrderId);
              element.insert(CCAPI_EM_ORDER_FEE_QUANTITY, Decimal(UtilString::printDoubleScientific(feeQuantity)).toString());
              element.insert(CCAPI_EM_ORDER_FEE_ASSET, takerFeeAsset);
              elementListPrivateTrade.emplace_back(std::move(element));
              if (matchedOrder.remainingQuantity == Decimal("0")) {
                break;
              }
            } else {
              break;
            }
            if (createdBuyOrder_a) {
              it++;
            } else {
              rit++;
            }
          }
          if ((createdBuyOrder_a && it == this->snapshotAsk_a.end()) || (!createdBuyOrder_a && rit == this->snapshotBid_a.rend())) {
            APP_LOGGER_WARN(std::string("All ") + (createdBuyOrder_a ? "asks" : "bids") +
                            " in the order book have been depleted. Do your order book data have enough depth?");
          }
          if (matchedOrder.remainingQuantity == Decimal("0")) {
            matchedOrder.status = APP_EVENT_HANDLER_BASE_ORDER_STATUS_FILLED;
            if (createdBuyOrder_a) {
              this->openBuyOrder_a = boost::none;
            } else {
              this->openSellOrder_a = boost::none;
            }
          } else {
            matchedOrder.status = APP_EVENT_HANDLER_BASE_ORDER_STATUS_PARTIALLY_FILLED;
            if (createdBuyOrder_a) {
              this->openBuyOrder_a = matchedOrder;
            } else {
              this->openSellOrder_a = matchedOrder;
            }
          }
          if (!elementListPrivateTrade.empty()) {
            virtualEvent_3.setType(Event::Type::SUBSCRIPTION_DATA);
            std::vector<Message> messageList;
            {
              Message message;
              message.setCorrelationIdList({std::string(CCAPI_EM_PRIVATE_TRADE) + "#prodA"});
              // message.setCorrelationIdList({PRIVATE_SUBSCRIPTION_DATA_CORRELATION_ID});
              message.setType(Message::Type::EXECUTION_MANAGEMENT_EVENTS_PRIVATE_TRADE);
              message.setTime(now);
              message.setTimeReceived(now);
              message.setElementList(elementListPrivateTrade);
              messageList.emplace_back(std::move(message));
            }
            {
              Message message;
              message.setCorrelationIdList({std::string(CCAPI_EM_ORDER_UPDATE) + "#prodA"});
              // message.setCorrelationIdList({PRIVATE_SUBSCRIPTION_DATA_CORRELATION_ID});
              message.setType(Message::Type::EXECUTION_MANAGEMENT_EVENTS_ORDER_UPDATE);
              message.setTime(now);
              message.setTimeReceived(now);
              Element element;
              this->extractOrderInfo(element, matchedOrder);
              std::vector<Element> elementList;
              elementList.emplace_back(std::move(element));
              message.setElementList(elementList);
              messageList.emplace_back(std::move(message));
            }
            virtualEvent_3.setMessageList(messageList);
          }
        }
      }  // end leg a

      // create order leg b
      if (operation == Request::Operation::CREATE_ORDER && productId == "prodB") {
        if (this->openBuyOrder_b || this->openSellOrder_b) {
          std::vector<Element> elementListPrivateTrade;
          auto it = this->snapshotAsk_b.begin();
          auto rit = this->snapshotBid_b.rbegin();
          Order matchedOrder = createdBuyOrder_b ? this->openBuyOrder_b.get() : this->openSellOrder_b.get();
          std::string takerFeeAsset = createdBuyOrder_b ? this->takerBuyerFeeAsset_b : this->takerSellerFeeAsset_b;

          while ((createdBuyOrder_b && it != this->snapshotAsk_b.end()) || (!createdBuyOrder_b && rit != this->snapshotBid_b.rend())) {
            Decimal priceToMatch = createdBuyOrder_b ? it->first : rit->first;
            if ((createdBuyOrder_b && this->openBuyOrder_b->limitPrice >= priceToMatch) ||
                (!createdBuyOrder_b && this->openSellOrder_b->limitPrice <= priceToMatch)) {
              APP_LOGGER_DEBUG("#########################################################################");
              APP_LOGGER_DEBUG("Consume order book leg B -> see taker order");

              Decimal quantityToMatch = Decimal(createdBuyOrder_b ? it->second : rit->second);
              // Decimal matchedQuantity = std::min(quantityToMatch, matchedOrder.remainingQuantity);
              Decimal matchedQuantity = matchedOrder.remainingQuantity;                                 // JFA -> DIFF TO REALITY
              priceToMatch = Decimal(createdBuyOrder_b ? this->bestAskPrice_b : this->bestBidPrice_b);  // JFA -> DIFF TO REALITY

              matchedOrder.cumulativeFilledQuantity = matchedOrder.cumulativeFilledQuantity.add(matchedQuantity);
              matchedOrder.remainingQuantity = matchedOrder.remainingQuantity.subtract(matchedQuantity);

              if (createdBuyOrder_b) {
                if (this->instrument_type_b != "INVERSE") {
                  if (!this->isContractDenominated_b) {
                    this->baseBalance_b += matchedQuantity.toDouble();
                    this->quoteBalance_b -= priceToMatch.toDouble() * matchedQuantity.toDouble();
                  } else {
                    this->baseBalance_b += matchedQuantity.toDouble() * std::stod(this->orderQuantityIncrement_b);
                    this->quoteBalance_b -= priceToMatch.toDouble() * matchedQuantity.toDouble() * std::stod(this->orderQuantityIncrement_b);
                  }
                } else {
                  if (!this->isContractDenominated_b) {
                    this->baseBalance_b += matchedQuantity.toDouble() / priceToMatch.toDouble();
                    this->quoteBalance_b -= matchedQuantity.toDouble();
                  } else {
                    this->baseBalance_b += (matchedQuantity.toDouble() * std::stod(this->orderQuantityIncrement_b)) / priceToMatch.toDouble();
                    this->quoteBalance_b -= matchedQuantity.toDouble() * std::stod(this->orderQuantityIncrement_b);
                  }
                }
              } else {
                if (this->instrument_type_b != "INVERSE") {
                  if (!this->isContractDenominated_b) {
                    this->baseBalance_b -= matchedQuantity.toDouble();
                    this->quoteBalance_b += priceToMatch.toDouble() * matchedQuantity.toDouble();
                  } else {
                    this->baseBalance_b -= matchedQuantity.toDouble() * std::stod(this->orderQuantityIncrement_b);
                    this->quoteBalance_b += priceToMatch.toDouble() * matchedQuantity.toDouble() * std::stod(this->orderQuantityIncrement_b);
                  }
                } else {
                  if (!this->isContractDenominated_b) {
                    this->baseBalance_b -= matchedQuantity.toDouble() / priceToMatch.toDouble();
                    this->quoteBalance_b += matchedQuantity.toDouble();
                  } else {
                    this->baseBalance_b -= (matchedQuantity.toDouble() * std::stod(this->orderQuantityIncrement_b)) / priceToMatch.toDouble();
                    this->quoteBalance_b += matchedQuantity.toDouble() * std::stod(this->orderQuantityIncrement_b);
                  }
                }
              }
              /* legacy 2
                            if (createdBuyOrder_b) {
                              if(instrument_type_b != "INVERSE") {
                                this->baseBalance_b += matchedQuantity.toDouble();
                                this->quoteBalance_b -= priceToMatch.toDouble() * matchedQuantity.toDouble();
                              } else {
                                this->baseBalance_b += matchedQuantity.toDouble() / priceToMatch.toDouble();
                                this->quoteBalance_b -= matchedQuantity.toDouble();
                              }
                            } else {
                              if(instrument_type_b != "INVERSE") {
                                this->baseBalance_b -= matchedQuantity.toDouble();
                                this->quoteBalance_b += priceToMatch.toDouble() * matchedQuantity.toDouble();
                              } else {
                                this->baseBalance_b -= matchedQuantity.toDouble() / priceToMatch.toDouble();
                                this->quoteBalance_b += matchedQuantity.toDouble();
                              }
                            }
              */
              // legacy
              /*
              if (createdBuyOrder_b) {
                this->baseBalance_b += matchedQuantity.toDouble();
                this->quoteBalance_b -= priceToMatch.toDouble() * matchedQuantity.toDouble();
              } else {
                this->baseBalance_b -= matchedQuantity.toDouble();
                this->quoteBalance_b += priceToMatch.toDouble() * matchedQuantity.toDouble();
              }
              */

              double feeQuantity = 0;
              if (UtilString::toLower(takerFeeAsset) == UtilString::toLower(this->baseAsset_b)) {
                if (instrument_type_b != "INVERSE") {
                  if (!this->isContractDenominated_b) {
                    feeQuantity = matchedQuantity.toDouble() * this->takerFee_b;
                  } else {
                    feeQuantity = matchedQuantity.toDouble() * std::stod(this->orderQuantityIncrement_b) * this->takerFee_b;
                  }
                } else {
                  if (!this->isContractDenominated_b) {
                    feeQuantity = matchedQuantity.toDouble() / priceToMatch.toDouble() * this->takerFee_b;
                  } else {
                    feeQuantity = (matchedQuantity.toDouble() * std::stod(this->orderQuantityIncrement_b)) / priceToMatch.toDouble() * this->takerFee_b;
                  }
                }

                this->baseBalance_b -= feeQuantity;
              } else if (UtilString::toLower(takerFeeAsset) == UtilString::toLower(this->quoteAsset_b)) {
                if (instrument_type_b != "INVERSE") {
                  if (!this->isContractDenominated_b) {
                    feeQuantity = priceToMatch.toDouble() * matchedQuantity.toDouble() * this->takerFee_b;
                  } else {
                    feeQuantity = priceToMatch.toDouble() * matchedQuantity.toDouble() * std::stod(this->orderQuantityIncrement_b) * this->takerFee_b;
                  }
                } else {
                  if (!this->isContractDenominated_b) {
                    feeQuantity = matchedQuantity.toDouble() * this->takerFee_b;
                  } else {
                    feeQuantity = matchedQuantity.toDouble() * std::stod(this->orderQuantityIncrement_b) * this->takerFee_b;
                  }
                }
                this->quoteBalance_b -= feeQuantity;
              }

              Element element;
              element.insert(CCAPI_TRADE_ID, std::to_string(++this->virtualTradeId_b));
              element.insert(CCAPI_EM_ORDER_LAST_EXECUTED_PRICE, priceToMatch.toString());
              element.insert(CCAPI_EM_ORDER_LAST_EXECUTED_SIZE, matchedQuantity.toString());
              element.insert(CCAPI_EM_ORDER_SIDE, matchedOrder.side);
              element.insert(CCAPI_IS_MAKER, "0");
              element.insert(CCAPI_EM_ORDER_ID, matchedOrder.orderId);
              element.insert(CCAPI_EM_CLIENT_ORDER_ID, matchedOrder.clientOrderId);
              element.insert(CCAPI_EM_ORDER_FEE_QUANTITY, Decimal(UtilString::printDoubleScientific(feeQuantity)).toString());
              element.insert(CCAPI_EM_ORDER_FEE_ASSET, takerFeeAsset);
              elementListPrivateTrade.emplace_back(std::move(element));
              if (matchedOrder.remainingQuantity == Decimal("0")) {
                break;
              }
            } else {
              break;
            }
            if (createdBuyOrder_b) {
              it++;
            } else {
              rit++;
            }
          }
          if ((createdBuyOrder_b && it == this->snapshotAsk_b.end()) || (!createdBuyOrder_b && rit == this->snapshotBid_b.rend())) {
            APP_LOGGER_WARN(std::string("All ") + (createdBuyOrder_b ? "asks" : "bids") +
                            " in the order book have been depleted. Do your order book data have enough depth?");
          }
          if (matchedOrder.remainingQuantity == Decimal("0")) {
            matchedOrder.status = APP_EVENT_HANDLER_BASE_ORDER_STATUS_FILLED;
            if (createdBuyOrder_b) {
              this->openBuyOrder_b = boost::none;
            } else {
              this->openSellOrder_b = boost::none;
            }
          } else {
            matchedOrder.status = APP_EVENT_HANDLER_BASE_ORDER_STATUS_PARTIALLY_FILLED;
            if (createdBuyOrder_b) {
              this->openBuyOrder_b = matchedOrder;
            } else {
              this->openSellOrder_b = matchedOrder;
            }
          }
          if (!elementListPrivateTrade.empty()) {
            virtualEvent_3.setType(Event::Type::SUBSCRIPTION_DATA);
            std::vector<Message> messageList;
            {
              Message message;
              message.setCorrelationIdList({std::string(CCAPI_EM_PRIVATE_TRADE) + "#prodB"});
              // message.setCorrelationIdList({PRIVATE_SUBSCRIPTION_DATA_CORRELATION_ID});
              message.setType(Message::Type::EXECUTION_MANAGEMENT_EVENTS_PRIVATE_TRADE);
              message.setTime(now);
              message.setTimeReceived(now);
              message.setElementList(elementListPrivateTrade);
              messageList.emplace_back(std::move(message));
            }
            {
              Message message;
              message.setCorrelationIdList({std::string(CCAPI_EM_ORDER_UPDATE) + "#prodB"});
              // message.setCorrelationIdList({PRIVATE_SUBSCRIPTION_DATA_CORRELATION_ID});
              message.setType(Message::Type::EXECUTION_MANAGEMENT_EVENTS_ORDER_UPDATE);
              message.setTime(now);
              message.setTimeReceived(now);
              Element element;
              this->extractOrderInfo(element, matchedOrder);
              std::vector<Element> elementList;
              elementList.emplace_back(std::move(element));
              message.setElementList(elementList);
              messageList.emplace_back(std::move(message));
            }
            virtualEvent_3.setMessageList(messageList);
          }
        }
      }  // end leg b

      if (!virtualEvent_3.getMessageList().empty()) {
        // APP_LOGGER_DEBUG("Generated a virtual event: " + virtualEvent_3.toStringPretty());
        this->processEvent(virtualEvent_3, session);
      }
    }
  }  // end fake server response

  CsvWriter* privateTradeCsvWriter = nullptr;
  CsvWriter* orderUpdateCsvWriter = nullptr;
  CsvWriter* accountBalanceCsvWriter = nullptr;
  CsvWriter* positionCsvWriter = nullptr;

  int64_t virtualTradeId{}, virtualOrderId{};
  int64_t virtualTradeId_a{}, virtualOrderId_a{}, virtualTradeId_b{}, virtualOrderId_b{};
  std::map<int, std::map<int, double>> publicTradeMap;
  std::map<Decimal, std::string> snapshotBid, snapshotAsk;
  std::map<Decimal, std::string> snapshotBid_a, snapshotAsk_a, snapshotBid_b, snapshotAsk_b;
  bool skipProcessEvent{};
};
} /* namespace ccapi */
#endif  // APP_INCLUDE_APP_EVENT_HANDLER_BASE_H_
