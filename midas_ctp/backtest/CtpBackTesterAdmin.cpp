#include <utils/FileUtils.h>
#include <utils/RegExpHelper.h>
#include "CtpBackTester.h"
#include "train/ParameterTrainer.h"

void CtpBackTester::init_admin() {
    admin_handler().register_callback("meters", boost::bind(&CtpBackTester::admin_meters, this, _1, _2),
                                      "display statistical information", "meters");
    admin_handler().register_callback("dump", boost::bind(&CtpBackTester::admin_dump, this, _1, _2),
                                      "dump (candle) instrument", "dump data");
    admin_handler().register_callback("csv", boost::bind(&CtpBackTester::admin_csv_dump, this, _1, _2),
                                      "csv (candle|strategy) (instrument) CandleScale", "dump csv data");
    admin_handler().register_callback("train", boost::bind(&CtpBackTester::admin_train, this, _1, _2),
                                      "train strategy (1 10 1 | 0.8 1.2 0.1)", "train strategy");
    admin_handler().register_callback("calculate", boost::bind(&CtpBackTester::admin_calculate, this, _1, _2),
                                      "calculate strategy", "calculate strategy");
}

string CtpBackTester::admin_csv_dump(const string& cmd, const TAdminCallbackArgs& args) {
    string instrument, type, scale;
    if (args.size() > 0) type = args[0];
    if (args.size() > 1) instrument = args[1];
    if (args.size() > 2) scale = args[2];

    string dumpFile;
    auto itr = data->instruments.find(instrument);
    if (itr != data->instruments.end()) {
        if (type == "candle") {
            if (scale.empty() || scale == "15") {
                dumpFile = dump2file<Candles>(itr->second->candles15, resultDirectory + "/" + instrument + ".15.csv");
            } else if (scale == "30") {
                dumpFile = dump2file<Candles>(itr->second->candles30, resultDirectory + "/" + instrument + ".30.csv");
            } else {
                return "CandleScale not support.";
            }
        } else if (type == "strategy") {
            if (itr->second->isMasterContract) {
                ostringstream oss;
                itr->second->strategy->csv_stream(oss);
                dumpFile = dump2file<std::string>(oss.str(), resultDirectory + "/" + instrument + ".strategy.csv");
            } else {
                return "not a master contract!";
            }
        }
    } else {
        return "instrument cannot found.";
    }
    return dumpFile + " dump finished";
}

string CtpBackTester::admin_dump(const string& cmd, const TAdminCallbackArgs& args) {
    string type, instrument;
    if (args.size() > 0) type = args[0];
    if (args.size() > 1) instrument = args[1];

    string dumpFile;
    if (type == "candle") {
        auto itr = data->instruments.find(instrument);
        if (itr != data->instruments.end()) {
            dumpFile = dump2file<CtpInstrument>(*(itr->second), resultDirectory + "/" + instrument + ".dump");
        } else {
            return "instrument cannot found.";
        }
    }
    return dumpFile + " dump finished";
}

string CtpBackTester::admin_meters(const string& cmd, const TAdminCallbackArgs& args) const {
    ostringstream oss;
    oss << "instruments:\n";
    for (const auto& item : data->instruments) {
        oss << item.first << " ";
    }
    oss << "\n";
    return oss.str();
}

/**
 * train strategy 1 10 1
 * train strategy 0.8 1.2 0.1
 */
string CtpBackTester::admin_train(const string& cmd, const TAdminCallbackArgs& args) {
    if (args.size() == 4) {
        std::unique_ptr<ParameterTrainer> trainer;
        string strategyName = args[0];

        if (midas::is_int(args[1])) {
            trainer = make_unique<ParameterTrainer>(std::atoi(args[1].c_str()), std::atoi(args[2].c_str()),
                                                    std::atoi(args[3].c_str()));
        } else if (midas::is_double(args[1])) {
            trainer = make_unique<ParameterTrainer>(std::atof(args[1].c_str()), std::atof(args[2].c_str()),
                                                    std::atof(args[3].c_str()));
        } else {
            return "invalid parameter!";
        }

        trainer->init_simulator(data, strategyName);
        trainer->process();
    }
    return "invalid parameter!";
}

string CtpBackTester::admin_calculate(const string& cmd, const TAdminCallbackArgs& args) {
    if (args.size() == 1) {
        string strategyName = args[0];
        calculate(string2strategy(strategyName));
    }
    return "invalid parameter!";
}
