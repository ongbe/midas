#include <utils/FileUtils.h>
#include "CtpProcess.h"
#include "helper/CtpVisualHelper.h"
#include "utils/CollectionUtils.h"

void CtpProcess::init_admin() {
    admin_handler().register_callback("meters", boost::bind(&CtpProcess::admin_meters, this, _1, _2),
                                      "display statistical information of connections", "meters");
    admin_handler().register_callback("request", boost::bind(&CtpProcess::admin_request, this, _1, _2),
                                      "request (instrument|product|exchange|account|position)",
                                      "request data from ctp server");
    admin_handler().register_callback("query", boost::bind(&CtpProcess::admin_query, this, _1, _2),
                                      "query (book|image|instrument|product|exchange|account|position)",
                                      "query data in local memory");
    admin_handler().register_callback("dump", boost::bind(&CtpProcess::admin_dump, this, _1, _2),
                                      "dump (instrument|product|exchange|account|position|candle)", "dump data");
    admin_handler().register_callback("buy", boost::bind(&CtpProcess::admin_buy, this, _1, _2),
                                      "buy instrument price size", "buy in limit order manner");
    admin_handler().register_callback("sell", boost::bind(&CtpProcess::admin_sell, this, _1, _2),
                                      "sell instrument price size", "sell in limit order manner");
    admin_handler().register_callback("close", boost::bind(&CtpProcess::admin_close, this, _1, _2),
                                      "close instrument size", "close instrument size");
    admin_handler().register_callback("flush", boost::bind(&CtpProcess::admin_flush, this, _1, _2), "flush",
                                      "flush log to disk");
    admin_handler().register_callback("save2db", boost::bind(&CtpProcess::admin_save2db, this, _1, _2),
                                      "save2db (instrument|candle)", "save data into mysql");
}

string CtpProcess::admin_request(const string& cmd, const TAdminCallbackArgs& args) {
    string param1, param2;
    if (args.size() > 0) param1 = args[0];
    if (args.size() > 1) param2 = args[1];

    if (param1 == "instrument") {
        manager->query_instrument(param2);
    } else if (param1 == "product") {
        manager->query_product(param2);
    } else if (param1 == "exchange") {
        manager->query_exchange(param2);
    } else if (param1 == "account") {
        manager->query_trading_account();
    } else if (param1 == "position") {
        data->positions.clear();
        manager->query_position(param2);
    } else if (param1 == "") {
        return "unknown parameter";
    }
    return "request sent";
}

string CtpProcess::admin_query(const string& cmd, const TAdminCallbackArgs& args) {
    string param1, param2;
    if (args.size() > 0) param1 = args[0];
    if (args.size() > 1) param2 = args[1];
    ostringstream oss;
    if (param1 == "book") {
        data->stream(oss, param2, false);
    } else if (param1 == "image") {
        data->stream(oss, param2, true);
    } else if (param1 == "instrument") {
        dump2stream(oss, data->instrumentInfo, param2);
    } else if (param1 == "product") {
        dump2stream(oss, data->products, param2);
    } else if (param1 == "exchange") {
        dump2stream(oss, data->exchanges, param2);
    } else if (param1 == "account") {
        dump2stream(oss, data->accounts, param2);
    } else if (param1 == "position") {
        oss << data->positions << '\n';
    } else if (param1 == "") {
        oss << "unknown parameter";
    }
    return oss.str();
}

string CtpProcess::admin_dump(const string& cmd, const TAdminCallbackArgs& args) {
    string param1, param2;
    if (args.size() > 0) param1 = args[0];
    if (args.size() > 1) param2 = args[1];

    if (param1 == "instrument" || param1 == "" || param1 == "all")
        dump2file(data->instrumentInfo, data->dataDirectory + "/instrument.dump");
    if (param1 == "product" || param1 == "" || param1 == "all")
        dump2file(data->products, data->dataDirectory + "/product.dump");
    if (param1 == "exchange" || param1 == "" || param1 == "all")
        dump2file(data->exchanges, data->dataDirectory + "/exchange.dump");
    if (param1 == "account" || param1 == "" || param1 == "all")
        dump2file(data->accounts, data->dataDirectory + "/account.dump");
    if (param1 == "position" || param1 == "" || param1 == "all")
        dump2file(data->positions, data->dataDirectory + "/position.dump");

    if (param1 == "candle") {
        auto itr = data->instruments.find(param2);
        if (itr != data->instruments.end()) {
            dump2file<CtpInstrument>(*(itr->second), data->dataDirectory + "/" + param2 + ".dump");
        } else {
            return "instrument cannot found.";
        }
    }
    return "dump finished";
}

string CtpProcess::admin_buy(const string& cmd, const TAdminCallbackArgs& args) const {
    ostringstream oss;
    if (args.size() < 3)
        oss << "missing parameter: buy instrument price size";
    else {
        string instrument = args[0];
        double price = boost::lexical_cast<double>(args[1]);
        int size = boost::lexical_cast<int>(args[2]);
        oss << "parameter: " << instrument << " " << price << " " << size << '\n';
        int ret = manager->request_buy_limit(instrument, size, price);
        if (ret == 0) {
            oss << "buy order sent success for " << instrument << " " << price << " " << size << '\n';
        } else {
            oss << "buy order sent failed with error code " << ret << '\n';
        }
    }
    return oss.str();
}

string CtpProcess::admin_sell(const string& cmd, const TAdminCallbackArgs& args) const {
    ostringstream oss;
    if (args.size() < 3)
        oss << "missing parameter: sell instrument price size";
    else {
        string instrument = args[0];
        double price = boost::lexical_cast<double>(args[1]);
        int size = boost::lexical_cast<int>(args[2]);
        oss << "parameter: " << instrument << " " << price << " " << size << '\n';
        int ret = manager->request_sell_limit(instrument, size, price);
        if (ret == 0) {
            oss << "sell order sent success for " << instrument << " " << price << " " << size << '\n';
        } else {
            oss << "sell order sent failed with error code " << ret << '\n';
        }
    }
    return oss.str();
}

string CtpProcess::admin_close(const string& cmd, const TAdminCallbackArgs& args) const {
    ostringstream oss;
    if (args.size() == 0)
        oss << "missing parameter\n";
    else {
        string content = args[0];
        oss << "parameter: " << content << '\n';
    }
    return oss.str();
}

string CtpProcess::admin_meters(const string& cmd, const TAdminCallbackArgs& args) const {
    ostringstream oss;
    oss << setw(24) << "MD login time" << setw(24) << "MD logout time" << setw(24) << "Trade login time" << setw(24)
        << "Trade logout time\n"
        << setw(24) << ntime2string(data->mdLogInTime) << setw(24) << ntime2string(data->mdLogOutTime) << setw(24)
        << ntime2string(data->tradeLogInTime) << setw(24) << ntime2string(data->tradeLogOutTime) << "\n";
    if (disruptorPtr) disruptorPtr->stats(oss);
    if (consumerPtr) consumerPtr->stats(oss);
    return oss.str();
}

string CtpProcess::admin_flush(const string& cmd, const TAdminCallbackArgs& args) {
    consumerPtr->flush();
    MIDAS_LOG_FLUSH();
    return "flush command issued";
}

string CtpProcess::admin_save2db(const string& cmd, const TAdminCallbackArgs& args) {
    ostringstream oss;
    if (args.empty())
        oss << "missing parameter: save2db (instrument|candle)";
    else if (args[0] == "instrument") {
        std::thread([this] { save_instruments(); }).detach();
        oss << "save instrument job submitted\n";
    } else if (args[0] == "candle") {
        std::thread([this] { save_candles(); }).detach();
        oss << "save candle job submitted\n";
    } else {
        oss << "unrecognized parameter for save2db\n";
    }
    return oss.str();
}

void CtpProcess::save_instruments() {
    MIDAS_LOG_INFO("start to save instrument to database");
    vector<std::shared_ptr<CThostFtdcInstrumentField>> data2save;
    map_values(data->instrumentInfo, data2save);
    int ret = DaoManager::instance().instrumentInfoDao->save_instruments(data2save);
    MIDAS_LOG_INFO("finish to save " << ret << " entries instruments into database");
}
void CtpProcess::save_candles() {
    MIDAS_LOG_INFO("start to save candle to database");

    int ret = 0;
    for (const auto& item : data->instruments) {
        ret += DaoManager::instance().candleDao->save_candles(item.second->id, item.second->candles15.data,
                                                              item.second->candles15.historicDataCount);
    }
    MIDAS_LOG_INFO("finish to save " << ret << " entries candles into database");
}
