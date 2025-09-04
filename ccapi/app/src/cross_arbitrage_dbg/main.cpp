#ifdef CCAPI_APP_USE_CUSTOM_EVENT_HANDLER
#include "custom_event_handler.h"
#else
#include "app/event_handler_base_swl.h"
//#include "event_handler_base_swl.h"
#endif

#include "app/ConfigReader.h"
//#include "../ConfigReader.h"

namespace ccapi {
AppLogger appLogger;
AppLogger* AppLogger::logger = &appLogger;
CcapiLogger ccapiLogger;
Logger* Logger::logger = &ccapiLogger;
} /* namespace ccapi */
using ::ccapi::AppLogger;
using ::ccapi::CcapiLogger;
using ::ccapi::Element;
using ::ccapi::Event;
#ifdef CCAPI_APP_USE_CUSTOM_EVENT_HANDLER
using ::ccapi::CustomEventHandler;
#endif
using ::ccapi::EventHandlerBase;
using ::ccapi::Logger;
using ::ccapi::Message;
using ::ccapi::Request;
using ::ccapi::Session;
using ::ccapi::Subscription;
using ::ccapi::UtilString;
using ::ccapi::UtilSystem;
using ::ccapi::UtilTime;
#ifndef CCAPI_APP_IS_BACKTEST
using ::ccapi::Queue;
using ::ccapi::SessionConfigs;
using ::ccapi::SessionOptions;
#endif

// retrieve parameters from config file
// C++ Header File(s)
#include <fstream>
#include <filesystem>
//#include <algorithm>

using namespace std;
using namespace cppsecrets;

#define NULL_PTR 0

ConfigReader* ConfigReader::m_pInstance = NULL_PTR;

ConfigReader::ConfigReader()
{
   m_ConfigSettingMap.clear();
}

ConfigReader::~ConfigReader()
{
   m_ConfigSettingMap.clear();
}

ConfigReader* ConfigReader::getInstance()
{
   // No need to use double re-check lock mechanism here
   // because this getInstance() will call at the time of
   // initialization only and mostly, at the time of
   // initialization, there will be only one thread.

   if(NULL_PTR == m_pInstance)
   {
      m_pInstance = new ConfigReader;
   }
   return m_pInstance;
}

bool ConfigReader::getValue(std::string tag, int& value)
{
   map<string, string>::iterator it ;
   it = m_ConfigSettingMap.find(tag);
   if(it != m_ConfigSettingMap.end())
   {
      value = atoi((it->second).c_str());
      return true;
   }
   return false;
}

bool ConfigReader::getValue(std::string tag, std::string& value)
{
   map<string, string>::iterator it ;
   it = m_ConfigSettingMap.find(tag);
   if(it != m_ConfigSettingMap.end())
   {
      value = it->second;
      return true;
   }
   return false;
}

bool ConfigReader::parseFile(std::string fileName)
{
  ifstream inputFile;
  inputFile.open(fileName.c_str());
  string delimeter = "=";
  int initPos = 0;

  if (inputFile.fail())
  {
    cout << "Unable to find defaultConfig file" << endl;
    return false;
  }

   string line;
   while(getline(inputFile, line))
   {
      // Remove comment Lines
      size_t found = line.find_first_of('#');
      string configData = line.substr(0, found);

      // Remove ^M from configData
      configData.erase(std::remove(configData.begin(), configData.end(), '\r'), configData.end());

      if(configData.empty())
         continue;

      unsigned int length = configData.find(delimeter);

      string tag, value;

      // rem. per compiler warning: warning: result of comparison of constant with expression of type 'unsigned int' is always true
      //if (length!=string::npos)
      //{
         tag   = configData.substr(initPos, length);
         value = configData.substr(length+1);
      //}

      // Trim white spaces
      tag   = reduce(tag);
      value = reduce(value);
      
      if(tag.empty() || value.empty())
         continue;

      // Check if any of the tags is repeated more than one times
      // it needs to pick the latest one instead of the old one.

      // Search, if the tag is already present or not
      // If it is already present, then delete an existing one

      std::map<std::string, std::string>::iterator itr = m_ConfigSettingMap.find(tag);
      if(itr != m_ConfigSettingMap.end())
      {
         m_ConfigSettingMap.erase(tag);
      }

      m_ConfigSettingMap.insert(std::pair<string, string>(tag, value));
   }
   return true;
}

std::string ConfigReader::trim(const std::string& str, const std::string& whitespace)
{
   size_t strBegin = str.find_first_not_of(whitespace);
   if (strBegin == std::string::npos)
      return "";

   size_t strEnd = str.find_last_not_of(whitespace);
   size_t strRange = strEnd - strBegin + 1;

   return str.substr(strBegin, strRange);
}

std::string ConfigReader::reduce(const std::string& str,
      const std::string& fill,
      const std::string& whitespace)
{
   // trim first
   string result = trim(str, whitespace);

   // replace sub ranges
   size_t beginSpace = result.find_first_of(whitespace);
   while (beginSpace != std::string::npos)
   {
      size_t endSpace = result.find_first_not_of(whitespace, beginSpace);
      size_t range = endSpace - beginSpace;

      result.replace(beginSpace, range, fill);

      size_t newStart = beginSpace + fill.length();
      beginSpace = result.find_first_of(whitespace, newStart);
   }

   return result;
}

void ConfigReader::dumpFileValues()
{
   map<string, string>::iterator it;
   for(it=m_ConfigSettingMap.begin(); it!=m_ConfigSettingMap.end(); ++it)
   {
      cout << it->first << " = " << it->second << endl;
   }
}

int main(int argc, char** argv) {
  
    // retrieve traderId
  std::string current_exec_name = argv[0];
    // retrieve trader id from command line
  std::string trader_id = argv[1]; 

  std::string inputFct{};
  std::string initTargetPosition_a="0.0", initTargetPosition_b="0.0";
  std::string initOrderSize_a="0.0", initOrderSize_b="0.0"; 

  if ( argc < 2 ) { // argc should be 2 for correct execution
    // We print argv[0] assuming it is the program name
    APP_LOGGER_INFO("usage: "+ current_exec_name+" traderId");
    return EXIT_FAILURE;
  } 

  // there is a function associated to the restart of the trader; e.g. reinit, target
  if(argc>2) {
    inputFct = argv[2]; 

    if(UtilString::toLower(inputFct)=="reinit"){
      if ( argc !=3 ) { 
        APP_LOGGER_INFO("usage: "+ current_exec_name+" traderId reinit");
        return EXIT_FAILURE;
      } 
    }
    
    if(UtilString::toLower(inputFct)=="target"){
      if ( argc !=5 ) { 
        APP_LOGGER_INFO("usage: "+ current_exec_name+" traderId target target_position_a target_position_b");
        return EXIT_FAILURE;
      } else {
        initTargetPosition_a=argv[3]; 
        initTargetPosition_b=argv[4]; 
      }
    }

    if(UtilString::toLower(inputFct)=="liquidation"){
      if ( argc !=3 ) { 
        APP_LOGGER_INFO("usage: "+ current_exec_name+" traderId liquidation");
        return EXIT_FAILURE;
      } 
    }

    // dealing with multiple bots per subaccounts...no specific position retrieval from exchange possible
    if(UtilString::toLower(inputFct)=="initorder"){
      if ( argc !=5 ) { 
        APP_LOGGER_INFO("usage: "+ current_exec_name+" traderId initorder order_size_a_usd order_size_b_usd");
        return EXIT_FAILURE;
      } else {
        initOrderSize_a=argv[3]; 
        initOrderSize_b=argv[4]; 
      }
    }

    if(UtilString::toLower(inputFct)=="liquidationorder"){
      if ( argc !=5 ) { 
        APP_LOGGER_INFO("usage: "+ current_exec_name+" traderId liquidationorder order_size_a_usd order_size_b_usd");
        return EXIT_FAILURE;
      } else {
        initOrderSize_a=argv[3]; 
        initOrderSize_b=argv[4]; 
      }
    }

    if(UtilString::toLower(inputFct)=="reinitorder"){
      if ( argc !=5 ) { 
        APP_LOGGER_INFO("usage: "+ current_exec_name+" traderId reinitorder order_size_a_usd order_size_b_usd");
        return EXIT_FAILURE;
      } else {
        initOrderSize_a=argv[3]; 
        initOrderSize_b=argv[4]; 
      }
    }

  }

  APP_LOGGER_INFO("*********************************************************");
  APP_LOGGER_INFO("******** CROSS EXCHANGE MARKET MAKER ALGO v0.1   ********");
  APP_LOGGER_INFO("******** for trader id -"+ trader_id +"- started.");
  
  if(UtilString::toLower(inputFct)=="reinit") {
    APP_LOGGER_INFO("******** Reinitialize and liquidate position for trader (reset init parameters).");

  } else if(UtilString::toLower(inputFct)=="target") {
    APP_LOGGER_INFO("******** Set new target position for trader (DONT reset init parameters).");

    APP_LOGGER_INFO("******** Initialize target position a: "+initTargetPosition_a);
    APP_LOGGER_INFO("******** Initialize target position b: "+initTargetPosition_b);

  } else if(UtilString::toLower(inputFct)=="liquidation") {
    APP_LOGGER_INFO("******** Liquidation and stop trader.");

  } else if(UtilString::toLower(inputFct)=="initorder") {
    APP_LOGGER_INFO("******** Send initial orders for trader (DONT reset init parameters).");

    APP_LOGGER_INFO("******** Initial order size a (usd): "+initOrderSize_a);
    APP_LOGGER_INFO("******** Initial order size b (usd): "+initOrderSize_b);

  } else if(UtilString::toLower(inputFct)=="liquidationorder") {
    APP_LOGGER_INFO("******** Send liquidationorder orders for trader (DONT reset init parameters).");

    APP_LOGGER_INFO("******** Liquidation order size a (usd): "+initOrderSize_a);
    APP_LOGGER_INFO("******** Liquidation order size b (usd): "+initOrderSize_b);

  } else if(UtilString::toLower(inputFct)=="reinitorder") {
    APP_LOGGER_INFO("******** Send reinitorder for trader (Reset parameters).");

    APP_LOGGER_INFO("******** Reinit order size a (usd): "+initOrderSize_a);
    APP_LOGGER_INFO("******** Reinit order size b (usd): "+initOrderSize_b);

  } else {
    APP_LOGGER_INFO("******** Restarting trader with previous states & keep current position (if any).");
  }
  APP_LOGGER_INFO("*********************************************************");

  auto now = UtilTime::now();


  // legacy
  // read configuration file using helper lib
  // for the base spot MM -> 2 files, i.e. config.env & config_advanced.env
  // Create object of the class ConfigReader
  // parse the configuration file
  namespace fs = std::filesystem;

  // /Users/faria/swl/working/ccapi/build/app/src/spot_market_making/../../../../app/include 
  fs::path pth = fs::current_path();
  
  //filesystem::path pth = current_path();

  // outside run
  std::string pth_str = pth.generic_string() + "/../../../../app/include/config_cross_arbitrage_"+trader_id+".env"; 

  // IDE path
  //std::string pth_str = pth.generic_string() + "/app/include/config_cross_arbitrage_"+trader_id+".env";
  
  ConfigReader* p = ConfigReader::getInstance();
  p->parseFile(pth_str);

   // Define variables to store the value
  std::string tradingMode{}, exchange_a{}, exchange_b{}, instrument_a{}, instrument_b{}, instrumentRest_a{}, instrumentRest_b{}, instrumentWebsocket_a{}, instrumentWebsocket_b{}, instrumentType_a{}, instrumentType_b{};
  std::string accountID_a{}, accountID_b{}, apiKey_a{}, apiKey_b{}, apiSecret_a{}, apiSecret_b{}, apiPassphrase_a{}, apiPassphrase_b{}, baseAssetOverride_a{}, baseAssetOverride_b{}, quoteAssetOverride_a{}, quoteAssetOverride_b{};
  std::string orderPriceIncrementOverride_a{}, orderPriceIncrementOverride_b{}, orderQuantityIncrementOverride_a{}, orderQuantityIncrementOverride_b{};
  std::string historicalMarketDataStartDate{}, historicalMarketDataEndDate{}, historicalMarketDataDirectory{}, historicalMarketDataFilePrefix{}, historicalMarketDataFileSuffix{}, resultsDirectory{}, privateDataOnlySaveFinalSummary{};
  std::string initialBaseBalance_a{}, initialBaseBalance_b{}, initialQuoteBalance_a{}, initialQuoteBalance_b{}, makerFee_a{}, makerFee_b{}, takerFee_a{}, takerFee_b{};
  std::string makerBuyerFeeAsset_a{}, makerBuyerFeeAsset_b{}, makerSellerFeeAsset_a{}, makerSellerFeeAsset_b{}, takerBuyerFeeAsset_a{}, takerBuyerFeeAsset_b{}, takerSellerFeeAsset_a{}, takerSellerFeeAsset_b{};
  std::string marketImpactFactor_a{}, marketImpactFactor_b{}, startTime{}, totalDurationSeconds{}, accountBalanceRefreshWaitSeconds{};
  std::string killSwitchMaximumDrawdownStr{};

  // simulation account flag for okx
  std::string apiSimulated_a{}, apiSimulated_b{};
  // database specific
  std::string dashboardAddress{}, databaseLogin{}, databasePassword{}, databaseName{};

  // algorithm specific configuration
  std::string normalizedSignalVector_0{}, normalizedSignalVector_1{}, normalizedTradingVector_0{}, normalizedTradingVector_1{}, margin{}, stepback{}, typicalOrdersize{}, nc2l{};
  std::string isContractDenominatedStr_a{}, isContractDenominatedStr_b{}, isCSVexportStr{}, isMysqlexportStr{};

  // Update the variable by the value present in the configuration file.
  p->getValue("TRADING_MODE", tradingMode);

  p->getValue("EXCHANGE_A", exchange_a);
  p->getValue("EXCHANGE_B", exchange_b);

  p->getValue("INSTRUMENT_A", instrument_a);
  p->getValue("INSTRUMENT_B", instrument_b);

  p->getValue("INSTRUMENT_REST_A", instrumentRest_a);
  p->getValue("INSTRUMENT_REST_B", instrumentRest_b);

  p->getValue("INSTRUMENT_WEBSOCKET_A", instrumentWebsocket_a);
  p->getValue("INSTRUMENT_WEBSOCKET_B", instrumentWebsocket_b);

  p->getValue("INSTRUMENT_TYPE_A", instrumentType_a);
  p->getValue("INSTRUMENT_TYPE_B", instrumentType_b);

  p->getValue("ACCOUNT_ID_A", accountID_a);
  p->getValue("ACCOUNT_ID_B", accountID_b);

  p->getValue("API_KEY_A", apiKey_a);
  p->getValue("API_KEY_B", apiKey_b);

  p->getValue("API_SECRET_A", apiSecret_a);
  p->getValue("API_SECRET_B", apiSecret_b);

  p->getValue("API_PASSPHRASE_A", apiPassphrase_a);
  p->getValue("API_PASSPHRASE_B", apiPassphrase_b);

  p->getValue("API_X_SIMULATED_A", apiSimulated_a);
  p->getValue("API_X_SIMULATED_B", apiSimulated_b);

  p->getValue("BASE_ASSET_OVERRIDE_A", baseAssetOverride_a);
  p->getValue("BASE_ASSET_OVERRIDE_B", baseAssetOverride_b);

  p->getValue("QUOTE_ASSET_OVERRIDE_A", quoteAssetOverride_a);
  p->getValue("QUOTE_ASSET_OVERRIDE_B", quoteAssetOverride_b);

  p->getValue("ORDER_PRICE_INCREMENT_OVERRIDE_A", orderPriceIncrementOverride_a);
  p->getValue("ORDER_PRICE_INCREMENT_OVERRIDE_B", orderPriceIncrementOverride_b);

  p->getValue("ORDER_QUANTITY_INCREMENT_OVERRIDE_A", orderQuantityIncrementOverride_a);
  p->getValue("ORDER_QUANTITY_INCREMENT_OVERRIDE_B", orderQuantityIncrementOverride_b);

  p->getValue("HISTORICAL_MARKET_DATA_START_DATE", historicalMarketDataStartDate);
  p->getValue("HISTORICAL_MARKET_DATA_END_DATE", historicalMarketDataEndDate);

  p->getValue("HISTORICAL_MARKET_DATA_DIRECTORY", historicalMarketDataDirectory);
  p->getValue("HISTORICAL_MARKET_DATA_FILE_PREFIX", historicalMarketDataFilePrefix);
  p->getValue("HISTORICAL_MARKET_DATA_FILE_SUFFIX", historicalMarketDataFileSuffix);  
  p->getValue("RESULTS_DIRECTORY", resultsDirectory);  
  p->getValue("PRIVATE_DATA_ONLY_SAVE_FINAL_SUMMARY", privateDataOnlySaveFinalSummary);  

  p->getValue("START_TIME", startTime);  
  p->getValue("TOTAL_DURATION_SECONDS", totalDurationSeconds);  

  p->getValue("ACCOUNT_BALANCE_REFRESH_WAIT_SECONDS", accountBalanceRefreshWaitSeconds);  
  
  p->getValue("INITIAL_BASE_BALANCE_A", initialBaseBalance_a);  
  p->getValue("INITIAL_BASE_BALANCE_B", initialBaseBalance_b);  

  p->getValue("INITIAL_QUOTE_BALANCE_A", initialQuoteBalance_a);  
  p->getValue("INITIAL_QUOTE_BALANCE_B", initialQuoteBalance_b);  

  p->getValue("MAKER_FEE_A", makerFee_a); 
  p->getValue("MAKER_FEE_B", makerFee_b); 

  p->getValue("TAKER_FEE_A", takerFee_a);     
  p->getValue("TAKER_FEE_B", takerFee_b);       

  p->getValue("MAKER_BUYER_FEE_ASSET_A", makerBuyerFeeAsset_a); 
  p->getValue("MAKER_BUYER_FEE_ASSET_B", makerBuyerFeeAsset_b); 

  p->getValue("MAKER_SELLER_FEE_ASSET_A", makerSellerFeeAsset_a); 
  p->getValue("MAKER_SELLER_FEE_ASSET_B", makerSellerFeeAsset_b); 

  p->getValue("TAKER_BUYER_FEE_ASSET_A", takerBuyerFeeAsset_a); 
  p->getValue("TAKER_BUYER_FEE_ASSET_B", takerBuyerFeeAsset_b); 

  p->getValue("TAKER_SELLER_FEE_ASSET_A", takerSellerFeeAsset_a); 
  p->getValue("TAKER_SELLER_FEE_ASSET_B", takerSellerFeeAsset_b); 

  p->getValue("MARKET_IMPACT_FACTOR_A", marketImpactFactor_a); 
  p->getValue("MARKET_IMPACT_FACTOR_B", marketImpactFactor_b); 

  // ALGORITHM SPECIFIC CONFIGURATION
  p->getValue("NORMALIZED_SIGNAL_VECTOR_0", normalizedSignalVector_0); 
  p->getValue("NORMALIZED_SIGNAL_VECTOR_1", normalizedSignalVector_1);

  p->getValue("NORMALIZED_TRADING_VECTOR_0", normalizedTradingVector_0); 
  p->getValue("NORMALIZED_TRADING_VECTOR_1", normalizedTradingVector_1);   

  p->getValue("MARGIN", margin);   
  p->getValue("STEPBACK", stepback);   

  p->getValue("TYPICAL_ORDER_SIZE", typicalOrdersize);  
  p->getValue("NC2L", nc2l);  

  p->getValue("KILL_SWITCH_MAX_DRAWDOWN", killSwitchMaximumDrawdownStr);

  p->getValue("IS_CONTRACT_DENOMINATED_A", isContractDenominatedStr_a);
  p->getValue("IS_CONTRACT_DENOMINATED_B", isContractDenominatedStr_b);
  p->getValue("IS_CSV_EXPORT", isCSVexportStr);
  p->getValue("IS_MYSQL_EXPORT", isMysqlexportStr);

  p->getValue("DASHBOARD_ADDRESSE", dashboardAddress);
  p->getValue("DATABASE_LOGIN", databaseLogin);
  p->getValue("DATABASE_PASSWORD", databasePassword);
  p->getValue("DATABASE_NAME", databaseName);

  // legacy
  //std::string exchange = UtilSystem::getEnvAsString("EXCHANGE");
  //std::string instrumentRest = UtilSystem::getEnvAsString("INSTRUMENT");

#ifdef CCAPI_APP_USE_CUSTOM_EVENT_HANDLER
  CustomEventHandler eventHandler;
#else
  EventHandlerBase eventHandler;
#endif

  // configure parameters within handler
  eventHandler.exchange_a = exchange_a;
  eventHandler.exchange_b = exchange_b;

  eventHandler.instrument_a = instrument_a;
  eventHandler.instrument_b = instrument_b;

  eventHandler.instrumentRest_a = instrumentRest_a;
  eventHandler.instrumentRest_b = instrumentRest_b;

  eventHandler.instrumentWebsocket_a = instrumentWebsocket_a;
  eventHandler.instrumentWebsocket_b = instrumentWebsocket_b;

  eventHandler.instrument_type_a = instrumentType_a;
  eventHandler.instrument_type_b = instrumentType_b;

  eventHandler.accountId_a = accountID_a;
  eventHandler.accountId_b = accountID_b;

  eventHandler.privateDataDirectory = resultsDirectory;
  eventHandler.privateDataFilePrefix = "";
  eventHandler.privateDataFileSuffix = "";
  eventHandler.privateDataOnlySaveFinalSummary = UtilString::toLower(privateDataOnlySaveFinalSummary)=="true";

  eventHandler.baseAsset_a = baseAssetOverride_a;
  eventHandler.baseAsset_b = baseAssetOverride_b;

  eventHandler.quoteAsset_a = quoteAssetOverride_a;
  eventHandler.quoteAsset_b = quoteAssetOverride_b;

  eventHandler.orderPriceIncrement_a = orderPriceIncrementOverride_a;
  eventHandler.orderPriceIncrement_b = orderPriceIncrementOverride_b;

  eventHandler.orderQuantityIncrement_a = orderQuantityIncrementOverride_a;
  eventHandler.orderQuantityIncrement_b = orderQuantityIncrementOverride_b;

  std::string startTimeStr = startTime;
  eventHandler.startTimeTp = startTimeStr.empty() ? now : UtilTime::parse(startTimeStr);

  eventHandler.totalDurationSeconds = std::stoi(totalDurationSeconds);
  eventHandler.killSwitchMaximumDrawdown = std::stod(killSwitchMaximumDrawdownStr);

  eventHandler.isContractDenominated_a = UtilString::toLower(isContractDenominatedStr_a)=="true";
  eventHandler.isContractDenominated_b = UtilString::toLower(isContractDenominatedStr_b)=="true";

  eventHandler.isCSVexport = UtilString::toLower(isCSVexportStr)=="true";
  eventHandler.isMysqlexport = UtilString::toLower(isMysqlexportStr)=="true";

  eventHandler.trader_id = trader_id;

  // calls available for Binance or account where we don't aggregate same products within same sub-account
  eventHandler.isReinit =UtilString::toLower(inputFct)=="reinit";
  eventHandler.isTarget =UtilString::toLower(inputFct)=="target";
  eventHandler.isLiquitation =UtilString::toLower(inputFct)=="liquidation";
  eventHandler.isInitOrder =UtilString::toLower(inputFct)=="initorder";
  eventHandler.isLiquidationOrder =UtilString::toLower(inputFct)=="liquidationorder";
  eventHandler.isReinitOrder =UtilString::toLower(inputFct)=="reinitorder";
  
  // init target position should be expressed in USD NOT in contracts!!
  eventHandler.initTargetPosition_a=std::stod(initTargetPosition_a);
  eventHandler.initTargetPosition_b=std::stod(initTargetPosition_b);

  // init order to be send prior to starting bot should be expressed in USD NOT in contracts!!
  eventHandler.initOrderSize_a=std::stod(initOrderSize_a);
  eventHandler.initOrderSize_b=std::stod(initOrderSize_b);

  // database specific
  eventHandler.dashboardAddress = dashboardAddress;
  eventHandler.databaseLogin = databaseLogin;
  eventHandler.databasePassword = databasePassword;
  eventHandler.databaseName = databaseName;

  // legacy; retrieval from env variables vs currently config file
  //eventHandler.exchange_a = exchange_a;
  
  //eventHandler.instrumentRest = instrumentRest;
  //eventHandler.instrumentWebsocket = instrumentWebsocket;

  //eventHandler.accountId = UtilSystem::getEnvAsString("ACCOUNT_ID");
  //eventHandler.privateDataDirectory = UtilSystem::getEnvAsString("PRIVATE_DATA_DIRECTORY");
  //eventHandler.privateDataFilePrefix = UtilSystem::getEnvAsString("PRIVATE_DATA_FILE_PREFIX");
  //eventHandler.privateDataFileSuffix = UtilSystem::getEnvAsString("PRIVATE_DATA_FILE_SUFFIX");
  //eventHandler.privateDataOnlySaveFinalSummary = UtilString::toLower(UtilSystem::getEnvAsString("PRIVATE_DATA_ONLY_SAVE_FINAL_SUMMARY")) == "true";

  // TODO: STOP LOSS
  //eventHandler.killSwitchMaximumDrawdown = UtilSystem::getEnvAsDouble("KILL_SWITCH_MAXIMUM_DRAWDOWN");

  //eventHandler.enableMarketMaking = UtilString::toLower(UtilSystem::getEnvAsString("ENABLE_MARKET_MAKING", "true")) == "true";

  //eventHandler.baseAsset = UtilSystem::getEnvAsString("BASE_ASSET_OVERRIDE");
  //eventHandler.quoteAsset = UtilSystem::getEnvAsString("QUOTE_ASSET_OVERRIDE");
  //eventHandler.orderPriceIncrement = UtilString::normalizeDecimalString(UtilSystem::getEnvAsString("ORDER_PRICE_INCREMENT_OVERRIDE"));
  //eventHandler.orderQuantityIncrement = UtilString::normalizeDecimalString(UtilSystem::getEnvAsString("ORDER_QUANTITY_INCREMENT_OVERRIDE"));
  
  //std::string startTimeStr = UtilSystem::getEnvAsString("START_TIME");
  //eventHandler.startTimeTp = startTimeStr.empty() ? now : UtilTime::parse(startTimeStr);

  eventHandler.appMode = EventHandlerBase::AppMode::MARKET_MAKING;

//#ifdef CCAPI_APP_IS_BACKTEST
//  APP_LOGGER_INFO("CCAPI_APP_IS_BACKTEST is defined!");
//#endif
//std::string tradingMode = UtilSystem::getEnvAsString("TRADING_MODE");

  APP_LOGGER_INFO("******** Trading mode is " + tradingMode + "! ********");

  if (tradingMode == "paper") {
    eventHandler.tradingMode = EventHandlerBase::TradingMode::PAPER;
  } else if (tradingMode == "backtest") {
    eventHandler.tradingMode = EventHandlerBase::TradingMode::BACKTEST;
  } else if (tradingMode == "live") {
    eventHandler.tradingMode = EventHandlerBase::TradingMode::LIVE;
  }
  //if (eventHandler.tradingMode == EventHandlerBase::TradingMode::PAPER || eventHandler.tradingMode == EventHandlerBase::TradingMode::BACKTEST) {

    eventHandler.makerFee_a = std::stod(makerFee_a);
    eventHandler.makerFee_b = std::stod(makerFee_b); 

    eventHandler.makerBuyerFeeAsset_a = makerBuyerFeeAsset_a;
    eventHandler.makerBuyerFeeAsset_b = makerBuyerFeeAsset_b;

    eventHandler.makerSellerFeeAsset_a = makerSellerFeeAsset_a;
    eventHandler.makerSellerFeeAsset_b = makerSellerFeeAsset_b;

    eventHandler.takerFee_a = std::stod(takerFee_a);
    eventHandler.takerFee_b = std::stod(takerFee_b);

    eventHandler.takerBuyerFeeAsset_a = takerBuyerFeeAsset_a;
    eventHandler.takerBuyerFeeAsset_b = takerBuyerFeeAsset_b;

    eventHandler.takerSellerFeeAsset_a = takerSellerFeeAsset_a;
    eventHandler.takerSellerFeeAsset_b = takerSellerFeeAsset_b;

    eventHandler.baseBalance_a = std::stod(initialBaseBalance_a);
    eventHandler.baseBalance_b = std::stod(initialBaseBalance_b);

    eventHandler.quoteBalance_a = std::stod(initialQuoteBalance_a);
    eventHandler.quoteBalance_b = std::stod(initialQuoteBalance_b);

    eventHandler.marketImpfactFactor_a = std::stod(marketImpactFactor_a);
    eventHandler.marketImpfactFactor_b = std::stod(marketImpactFactor_b);

    // ALGORITHM SPECIFIC CONFIGURATION
    eventHandler.normalizedSignalVector_0 = std::stod(normalizedSignalVector_0);
    eventHandler.normalizedSignalVector_1 = std::stod(normalizedSignalVector_1);
    
    eventHandler.normalizedTradingVector_0 = std::stod(normalizedTradingVector_0);
    eventHandler.normalizedTradingVector_1 = std::stod(normalizedTradingVector_1);

    eventHandler.margin = std::stod(margin);
    eventHandler.stepback = std::stod(stepback);
    eventHandler.typicalOrderSize = std::stod(typicalOrdersize);    
    eventHandler.nc2l = std::stod(nc2l);     

    // legacy
    //eventHandler.makerFee = UtilSystem::getEnvAsDouble("MAKER_FEE");
    //eventHandler.makerBuyerFeeAsset = UtilSystem::getEnvAsString("MAKER_BUYER_FEE_ASSET");
    //eventHandler.makerSellerFeeAsset = UtilSystem::getEnvAsString("MAKER_SELLER_FEE_ASSET");
    //eventHandler.takerFee = UtilSystem::getEnvAsDouble("TAKER_FEE");
    //eventHandler.takerBuyerFeeAsset = UtilSystem::getEnvAsString("TAKER_BUYER_FEE_ASSET");
    //eventHandler.takerSellerFeeAsset = UtilSystem::getEnvAsString("TAKER_SELLER_FEE_ASSET");
    //eventHandler.baseBalance = UtilSystem::getEnvAsDouble("INITIAL_BASE_BALANCE") * eventHandler.baseAvailableBalanceProportion;
    //eventHandler.quoteBalance = UtilSystem::getEnvAsDouble("INITIAL_QUOTE_BALANCE") * eventHandler.quoteAvailableBalanceProportion;
    //eventHandler.marketImpfactFactor = UtilSystem::getEnvAsDouble("MARKET_IMPACT_FACTOR");
  //}
  if (eventHandler.tradingMode == EventHandlerBase::TradingMode::BACKTEST) {
    eventHandler.historicalMarketDataStartDateTp = UtilTime::parse(historicalMarketDataStartDate, "%F");
    // legacy
    //eventHandler.historicalMarketDataStartDateTp = UtilTime::parse(UtilSystem::getEnvAsString("HISTORICAL_MARKET_DATA_START_DATE"), "%F");
    if (startTimeStr.empty()) {
      eventHandler.startTimeTp = eventHandler.historicalMarketDataStartDateTp;
    }
    eventHandler.historicalMarketDataEndDateTp = UtilTime::parse(historicalMarketDataEndDate, "%F");
    eventHandler.historicalMarketDataDirectory = historicalMarketDataDirectory;
    eventHandler.historicalMarketDataFilePrefix = historicalMarketDataFilePrefix;
    eventHandler.historicalMarketDataFileSuffix = historicalMarketDataFileSuffix;    

    // legacy
    //eventHandler.historicalMarketDataEndDateTp = UtilTime::parse(UtilSystem::getEnvAsString("HISTORICAL_MARKET_DATA_END_DATE"), "%F");
    //eventHandler.historicalMarketDataDirectory = UtilSystem::getEnvAsString("HISTORICAL_MARKET_DATA_DIRECTORY");
    //eventHandler.historicalMarketDataFilePrefix = UtilSystem::getEnvAsString("HISTORICAL_MARKET_DATA_FILE_PREFIX");
    //eventHandler.historicalMarketDataFileSuffix = UtilSystem::getEnvAsString("HISTORICAL_MARKET_DATA_FILE_SUFFIX");
  }
  std::set<std::string> useGetAccountsToGetAccountBalancesExchangeSet{"coinbase", "kucoin"};
  if (useGetAccountsToGetAccountBalancesExchangeSet.find(eventHandler.exchange_a) != useGetAccountsToGetAccountBalancesExchangeSet.end()) {
    eventHandler.useGetAccountsToGetAccountBalances_a = true;
  }
  if (useGetAccountsToGetAccountBalancesExchangeSet.find(eventHandler.exchange_b) != useGetAccountsToGetAccountBalancesExchangeSet.end()) {
    eventHandler.useGetAccountsToGetAccountBalances_b = true;
  }
  std::shared_ptr<std::promise<void>> promisePtr(new std::promise<void>());
  eventHandler.promisePtr = promisePtr;
#ifndef CCAPI_APP_IS_BACKTEST
  SessionOptions sessionOptions;

  // Some exchanges cache account balances. Therefore after canceling all open orders, we'd wait a little bit for the held
  // funds to be released.

  // set refresh delay for account balance
  eventHandler.accountBalanceRefreshWaitSeconds = std::stoi(accountBalanceRefreshWaitSeconds);

  //sessionOptions.httpConnectionPoolIdleTimeoutMilliSeconds = 1 + eventHandler.accountBalanceRefreshWaitSeconds;
  sessionOptions.httpMaxNumRetry = 0;
  sessionOptions.httpMaxNumRedirect = 0;

  // sessionOptions.httpConnectionPoolMaxSize = 12;
  // sessionOptions.httpConnectionKeepAliveTimeoutSeconds = 30;
  // sessionOptions.enableOneHttpConnectionPerRequest = false;

  SessionConfigs sessionConfigs;
  Session session(sessionOptions, sessionConfigs, &eventHandler);
  eventHandler.onInit(&session);
#else
  Session session;
  eventHandler.onInit(&session);
#endif


  // 
  // PRODUCT A & B
  //
  // set exchange credential -> beware non uniform naming of keys...in the pair {key,value}
  if (exchange_a.rfind("binance", 0) == 0  || exchange_a.rfind("binance-coin-futures", 0) == 0) {
    eventHandler.credential_a = {
      {CCAPI_BINANCE_COIN_FUTURES_API_KEY, apiKey_a},
      {CCAPI_BINANCE_COIN_FUTURES_API_SECRET, apiSecret_a},
    };
  }

  if (exchange_a.rfind("deribit", 0) == 0) {
    eventHandler.credential_a = {
      {CCAPI_DERIBIT_CLIENT_ID, apiKey_a},
      {CCAPI_DERIBIT_CLIENT_SECRET, apiSecret_a},
    };
  }

  if (exchange_a.rfind("okx", 0) == 0) {
    eventHandler.credential_a = {
      {CCAPI_OKX_API_KEY, apiKey_a},
      {CCAPI_OKX_API_SECRET, apiSecret_a},
      {CCAPI_OKX_API_PASSPHRASE, apiPassphrase_a},
      {CCAPI_OKX_API_X_SIMULATED_TRADING, apiSimulated_a},
    };
  }

  // set exchange credential -> beware non uniform naming of keys...in the pair {key,value}
  if (exchange_b.rfind("binance", 0) == 0  || exchange_b.rfind("binance-coin-futures", 0) == 0) {
    eventHandler.credential_b = {
      {CCAPI_BINANCE_COIN_FUTURES_API_KEY, apiKey_b},
      {CCAPI_BINANCE_COIN_FUTURES_API_SECRET, apiSecret_b},
    };
  }

  if (exchange_b.rfind("deribit", 0) == 0) {
    eventHandler.credential_b = {
      {CCAPI_DERIBIT_CLIENT_ID, apiKey_b},
      {CCAPI_DERIBIT_CLIENT_SECRET, apiSecret_b},
    };
  }

  if (exchange_b.rfind("okx", 0) == 0) {
    eventHandler.credential_b = {
      {CCAPI_OKX_API_KEY, apiKey_b},
      {CCAPI_OKX_API_SECRET, apiSecret_b},
      {CCAPI_OKX_API_PASSPHRASE, apiPassphrase_b},
      {CCAPI_OKX_API_X_SIMULATED_TRADING, apiSimulated_b},
    };
  }

// 
// PRODUCT A
//
  if (exchange_a == "kraken") {
    /*
#ifndef CCAPI_APP_IS_BACKTEST
    Request request_a(Request::Operation::GENERIC_PUBLIC_REQUEST, "kraken", "", "Get Instrument Symbol For Websocket -> product A");
    request_a.appendParam({
        {"HTTP_METHOD", "GET"},
        {"HTTP_PATH", "/0/public/AssetPairs"},
        {"HTTP_QUERY_STRING", "pair=" + instrumentRest_a},
    });
    Queue<Event> eventQueue;
    session.sendRequest(request_a, &eventQueue);
    std::vector<Event> eventList = eventQueue.purge();
    for (const auto& event : eventList) {
      if (event.getType() == Event::Type::RESPONSE) {
        rj::Document document;
        document.Parse(event.getMessageList().at(0).getElementList().at(0).getValue("HTTP_BODY").c_str());
        eventHandler.instrumentWebsocket_a = document["result"][instrumentRest_a.c_str()]["wsname"].GetString();
        break;
      }
    }
#else
*/
    eventHandler.instrumentWebsocket_a = instrumentWebsocket_a;
    
    // legacy
    //eventHandler.instrumentWebsocket = UtilSystem::getEnvAsString("INSTRUMENT_WEBSOCKET");
//#endif

  } else if (exchange_a.rfind("binance", 0) == 0  || exchange_a.rfind("binance-coin-futures", 0) == 0) {
    eventHandler.instrumentWebsocket_a = UtilString::toLower(instrumentRest_a);
  }
  std::set<std::string> needSecondCredentialExchangeSet{"gemini", "kraken"};
  if (needSecondCredentialExchangeSet.find(eventHandler.exchange_a) != needSecondCredentialExchangeSet.end()) {
    const auto& exchangeUpper = UtilString::toUpper(eventHandler.exchange_a);
    
    // BEWARE -> CONFIGURE INPUT PARAMETERS...
    //eventHandler.credential_2_a = {
    //    {exchangeUpper + "_API_KEY", apiKey_a},
    //    {exchangeUpper + "_API_SECRET", apiSecret_a},
        // legacy
        //{exchangeUpper + "_API_KEY", UtilSystem::getEnvAsString(exchangeUpper + "_API_KEY")},
        //{exchangeUpper + "_API_SECRET", UtilSystem::getEnvAsString(exchangeUpper + "_API_SECRET")},
    //};
  }

  // we don't want that behavior...cancel order independently not bulk cancel...
  //std::set<std::string> useCancelOrderToCancelOpenOrdersExchangeSet{"gemini", "kraken", "bitfinex", "okx"};
  std::set<std::string> useCancelOrderToCancelOpenOrdersExchangeSet{"none"};
  if (useCancelOrderToCancelOpenOrdersExchangeSet.find(eventHandler.exchange_a) != useCancelOrderToCancelOpenOrdersExchangeSet.end()) {
    eventHandler.useCancelOrderToCancelOpenOrders_a = true;
  }

  /*
  std::set<std::string> useWebsocketToExecuteOrderExchangeSet{"bitfinex", "okx"};
  if (useWebsocketToExecuteOrderExchangeSet.find(eventHandler.exchange_a) != useWebsocketToExecuteOrderExchangeSet.end()) {
    eventHandler.useWebsocketToExecuteOrder_a = true;
  }
  */

  Request request_a(Request::Operation::GET_INSTRUMENT, eventHandler.exchange_a, eventHandler.instrumentRest_a, "GET_INSTRUMENT#prodA");
  /*
  if (exchange_a == "okx") {
    // Type of instrument. Valid value can be FUTURES, OPTION, SWAP or SPOT. for okex see. https://www.okx.com/docs-v5/en/#block-trading-rest-api-get-quote-products
    std::string instrType = instrumentType_a=="SPOT"?"SPOT":"SWAP";
    request_a.appendParam({
        {"instType", instrType},
        // legacy
        //{"instType", "SPOT"},
    });
  }
*/

  // END PRODUCT A

// 
// PRODUCT B
//
  if (exchange_b == "kraken") {
/*
#ifndef CCAPI_APP_IS_BACKTEST
    Request request_b(Request::Operation::GENERIC_PUBLIC_REQUEST, "kraken", "", "Get Instrument Symbol For Websocket -> product B");
    request_b.appendParam({
        {"HTTP_METHOD", "GET"},
        {"HTTP_PATH", "/0/public/AssetPairs"},
        {"HTTP_QUERY_STRING", "pair=" + instrumentRest_b},
    });
    Queue<Event> eventQueue;
    session.sendRequest(request_b, &eventQueue);
    std::vector<Event> eventList = eventQueue.purge();
    for (const auto& event : eventList) {
      if (event.getType() == Event::Type::RESPONSE) {
        rj::Document document;
        document.Parse(event.getMessageList().at(0).getElementList().at(0).getValue("HTTP_BODY").c_str());
        eventHandler.instrumentWebsocket_b = document["result"][instrumentRest_b.c_str()]["wsname"].GetString();
        break;
      }
    }
#else
*/
    eventHandler.instrumentWebsocket_b = instrumentWebsocket_b;

    // legacy
    //eventHandler.instrumentWebsocket = UtilSystem::getEnvAsString("INSTRUMENT_WEBSOCKET");
//#endif

  } else if (exchange_b.rfind("binance", 0) == 0  || exchange_b.rfind("binance-coin-futures", 0) == 0) {
    eventHandler.instrumentWebsocket_b = UtilString::toLower(instrumentRest_b);
  }
  // already set
  //std::set<std::string> needSecondCredentialExchangeSet{"gemini", "kraken"};
  if (needSecondCredentialExchangeSet.find(eventHandler.exchange_b) != needSecondCredentialExchangeSet.end()) {
    const auto& exchangeUpper = UtilString::toUpper(eventHandler.exchange_b);
    //eventHandler.credential_2_b = {
    //    {exchangeUpper + "_API_KEY", apiKey_b},
    //    {exchangeUpper + "_API_SECRET", apiSecret_b},
        // legacy
        //{exchangeUpper + "_API_KEY", UtilSystem::getEnvAsString(exchangeUpper + "_API_KEY")},
        //{exchangeUpper + "_API_SECRET", UtilSystem::getEnvAsString(exchangeUpper + "_API_SECRET")},
    //};
  }
  // already set
  //std::set<std::string> useCancelOrderToCancelOpenOrdersExchangeSet{"gemini", "kraken", "bitfinex", "okx"};
  if (useCancelOrderToCancelOpenOrdersExchangeSet.find(eventHandler.exchange_b) != useCancelOrderToCancelOpenOrdersExchangeSet.end()) {
    eventHandler.useCancelOrderToCancelOpenOrders_b = true;
  }
  // already set
  /*
  std::set<std::string> useWebsocketToExecuteOrderExchangeSet{"bitfinex", "okx"};
  if (useWebsocketToExecuteOrderExchangeSet.find(eventHandler.exchange_b) != useWebsocketToExecuteOrderExchangeSet.end()) {
    eventHandler.useWebsocketToExecuteOrder_b = true;
  }
*/

  Request request_b(Request::Operation::GET_INSTRUMENT, eventHandler.exchange_b, eventHandler.instrumentRest_b, "GET_INSTRUMENT#prodB");

  /*
  if (exchange_b == "okx") {
    // Type of instrument. Valid value can be FUTURES, OPTION, SWAP or SPOT. for okex see. https://www.okx.com/docs-v5/en/#block-trading-rest-api-get-quote-products
    std::string instrType = instrumentType_b=="SPOT"?"SPOT":"SWAP";
    request_b.appendParam({
        {"instType", instrType},
        // legacy
        //{"instType", "SPOT"},
    });
  }
  */

  // END PRODUCT B
  eventHandler.useCancelOrderToCancelOpenOrders_a = false;
  eventHandler.useCancelOrderToCancelOpenOrders_b = false;

  // 
  // PRODUCT A
  //
  //if (eventHandler.tradingMode == EventHandlerBase::TradingMode::BACKTEST && 
    if(!eventHandler.baseAsset_a.empty() && !eventHandler.quoteAsset_a.empty() &&
      !eventHandler.orderPriceIncrement_a.empty() && !eventHandler.orderQuantityIncrement_a.empty()) {
    Event virtualEvent;
    Message message;
    message.setTime(eventHandler.startTimeTp);
    message.setTimeReceived(eventHandler.startTimeTp);
    message.setCorrelationIdList({request_a.getCorrelationId()});
    std::vector<Element> elementList;
    virtualEvent.setType(Event::Type::RESPONSE);
    message.setType(Message::Type::GET_INSTRUMENT);
    Element element;
    element.insert("BASE_ASSET", eventHandler.baseAsset_a);
    element.insert("QUOTE_ASSET", eventHandler.quoteAsset_a);
    element.insert("PRICE_INCREMENT", eventHandler.orderPriceIncrement_a);
    element.insert("QUANTITY_INCREMENT", eventHandler.orderQuantityIncrement_a);
    elementList.emplace_back(std::move(element));
    message.setElementList(elementList);
    virtualEvent.setMessageList({message});
    eventHandler.processEvent(virtualEvent, &session);
  } else {
    session.sendRequest(request_a);
  }
  // END PRODUCT A

  // 
  // PRODUCT B
  //
  //if (eventHandler.tradingMode == EventHandlerBase::TradingMode::BACKTEST && 
  if(!eventHandler.baseAsset_b.empty() && !eventHandler.quoteAsset_b.empty() &&
      !eventHandler.orderPriceIncrement_b.empty() && !eventHandler.orderQuantityIncrement_b.empty()) {
    Event virtualEvent;
    Message message;
    message.setTime(eventHandler.startTimeTp);
    message.setTimeReceived(eventHandler.startTimeTp);
    message.setCorrelationIdList({request_b.getCorrelationId()});
    std::vector<Element> elementList;
    virtualEvent.setType(Event::Type::RESPONSE);
    message.setType(Message::Type::GET_INSTRUMENT);
    Element element;
    element.insert("BASE_ASSET", eventHandler.baseAsset_b);
    element.insert("QUOTE_ASSET", eventHandler.quoteAsset_b);
    element.insert("PRICE_INCREMENT", eventHandler.orderPriceIncrement_b);
    element.insert("QUANTITY_INCREMENT", eventHandler.orderQuantityIncrement_b);
    elementList.emplace_back(std::move(element));
    message.setElementList(elementList);
    virtualEvent.setMessageList({message});
    eventHandler.processEvent(virtualEvent, &session);
  } else {
    session.sendRequest(request_b);
  }
  // END PRODUCT A

  promisePtr->get_future().wait();
  eventHandler.onClose(&session);
  session.stop();

  return EXIT_SUCCESS;
}
