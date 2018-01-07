#ifndef MIDAS_BI_MA_STRATEGY_H
#define MIDAS_BI_MA_STRATEGY_H

#include "StrategyBase.h"
#include "utils/math/DescriptiveStatistics.h"

class BiMaStrategy : public StrategyBase {
public:
    int slowPeriod{60};
    int fastPeriod{10};
    vector<double> slowMa;
    vector<double> fastMa;
    DescriptiveStatistics dsSlow;
    DescriptiveStatistics dsFast;

public:
    BiMaStrategy(const Candles& candles_) : StrategyBase(candles_) {}

    void calculate(size_t index) override {
        dsSlow.add_value(candles.data[index].close);
        dsFast.add_value(candles.data[index].close);
        slowMa[index] = dsSlow.get_mean();
        fastMa[index] = dsFast.get_mean();
    }

    void init() override {
        size_t size = candles.data.size();
        dsSlow.clear();
        dsSlow.set_window_size(slowPeriod);
        dsFast.clear();
        dsFast.set_window_size(fastPeriod);
        slowMa.resize(size);
        fastMa.resize(size);
        signals.resize(size);
    }

    string getCsvHeader() override { return "slow,fast"; }

    string getCsvLine(size_t index) override {
        ostringstream oss;
        oss << slowMa[index] << "," << fastMa[index];
        return oss.str();
    }
};

#endif
