#include "engine.hpp"
#include "reference.hpp"
#include <chrono>
#include <iostream>
#include <random>
using namespace exchange;
using Clock=std::chrono::steady_clock;
struct Event { bool cancel; Request request; };
std::vector<Event> workload() {
    std::vector<Event> events;Id id=1;std::mt19937 rng(2026);
    for(int i=0;i<4000;++i) {
        Side s=i%2?Side::Buy:Side::Sell;
        events.push_back({false,{id++,s,Type::GTC,s==Side::Buy?9900-static_cast<Value>(rng()%100):10100+static_cast<Value>(rng()%100),10}});
    }
    for(int i=0;i<12000;++i) {
        if(i%4==0) events.push_back({true,{1+rng()%(id-1),Side::Buy,Type::GTC,0,0}});
        else {
            Side s=rng()%2?Side::Buy:Side::Sell;Type t=i%3==0?Type::IOC:Type::GTC;
            Value p=t==Type::IOC?(s==Side::Buy?10200:9800):(s==Side::Buy?9900-static_cast<Value>(rng()%100):10100+static_cast<Value>(rng()%100));
            events.push_back({false,{id++,s,t,p,1+static_cast<Value>(rng()%20)}});
        }
    }
    return events;
}
template<class Model> void measure(const char* label,const std::vector<Event>& events) {
    // Untimed warm-up using identical workload but a separate instance.
    { Model warm;for(const auto& e:events) {if(e.cancel) warm.cancel(e.request.id);else warm.submit(e.request);} }
    std::vector<double> ns;ns.reserve(events.size()*3);
    double elapsed=0;std::uint64_t checksum=0;
    for(int run=0;run<3;++run) {
        Model model;const auto batch=Clock::now();
        for(const auto& event:events) {
            const auto start=Clock::now();
            const Result r=event.cancel?model.cancel(event.request.id):model.submit(event.request);
            const auto end=Clock::now();
            ns.push_back(std::chrono::duration<double,std::nano>(end-start).count());
            checksum+=static_cast<std::uint64_t>(r.filled+r.resting+r.cancelled)+r.trades.size();
        }
        elapsed+=std::chrono::duration<double>(Clock::now()-batch).count();
    }
    std::sort(ns.begin(),ns.end());
    auto p=[&](double q){return ns[static_cast<std::size_t>(q*static_cast<double>(ns.size()-1))];};
    std::cout<<"{\"engine\":\""<<label<<"\",\"events_per_run\":"<<events.size()<<",\"runs\":3,\"events_per_second\":"
        <<static_cast<double>(ns.size())/elapsed<<",\"p50_ns\":"<<p(.5)<<",\"p95_ns\":"<<p(.95)<<",\"p99_ns\":"<<p(.99)
        <<",\"checksum\":"<<checksum<<"}";
}
int main() {
    const auto events=workload();
    std::cout<<"{\"seed\":2026,\"clock\":\"steady_clock\",\"compiler\":\""<<__VERSION__<<"\",\"results\":[";
    measure<Engine>("price-level FIFO",events);std::cout<<',';measure<Reference>("vector reference",events);std::cout<<"]}\n";
}
