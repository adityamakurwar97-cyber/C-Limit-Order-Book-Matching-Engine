#include "protocol.hpp"
#include "reference.hpp"
#include <iostream>
#include <random>
using namespace exchange;
void require(bool ok, const char* text) { if (!ok) throw std::runtime_error(text); }
void equal(const Result& a, const Result& b) {
    require(a.ok==b.ok && a.status==b.status && a.id==b.id && a.filled==b.filled &&
            a.resting==b.resting && a.cancelled==b.cancelled && a.trades.size()==b.trades.size(), "Result mismatch");
    for (std::size_t i=0;i<a.trades.size();++i) {
        const auto& x=a.trades[i];const auto& y=b.trades[i];
        require(x.sequence==y.sequence && x.buy==y.buy && x.sell==y.sell && x.maker==y.maker &&
                x.taker==y.taker && x.price==y.price && x.quantity==y.quantity,"Trade mismatch");
    }
}
void bookEqual(std::vector<Order> a,std::vector<Order> b) {
    auto comparator=[](const Order& x,const Order& y){return x.id<y.id;};
    std::sort(a.begin(),a.end(),comparator);std::sort(b.begin(),b.end(),comparator);
    require(a.size()==b.size(),"Book size mismatch");
    for(std::size_t i=0;i<a.size();++i)
        require(a[i].id==b[i].id && a[i].price==b[i].price && a[i].side==b[i].side &&
                a[i].remaining==b[i].remaining && a[i].sequence==b[i].sequence,"Book mismatch");
}
int main() {
    try {
        int groups=0;
        auto test=[&](const char* name,auto f){f();++groups;std::cout<<"PASS "<<name<<'\n';};
        test("empty, validation and strict parser",[]{
            Engine e;require(!e.cancel(1).ok,"unknown cancel");
            for(const auto& s:{"BUY 0 100 1 GTC","BUY 1 0 1 GTC","BUY 1 100 0 GTC",
                "BUY -1 100 1 GTC","BUY 1 100x 1 GTC","BUY 1 100 1 BAD",
                "BUY 1 100 1 MARKET","BUY 18446744073709551616 100 1 GTC"})
                require(!execute(e,s).ok,"invalid accepted");
            require(e.activeCount()==0,"invalid mutation");e.validate();
        });
        test("price priority, FIFO, maker price and multiple fills",[]{
            Engine e;
            execute(e,"SELL 10 101 2 GTC");execute(e,"SELL 9 100 3 GTC");execute(e,"SELL 2 100 4 GTC");
            auto r=execute(e,"BUY 3 105 8 GTC");
            require(r.trades.size()==3 && r.trades[0].maker==9 && r.trades[1].maker==2 &&
                r.trades[2].maker==10 && r.trades[0].price==100 && r.trades[2].price==101,"priority");
            require(e.depth(Side::Sell)[0].quantity==1,"remaining");e.validate();
        });
        test("buy priority and partial fill preserves FIFO",[]{
            Engine e;execute(e,"BUY 8 105 4 GTC");execute(e,"BUY 2 105 2 GTC");execute(e,"BUY 3 104 3 GTC");
            auto r=execute(e,"SELL 4 100 2 GTC");require(r.trades[0].maker==8 && r.trades[0].price==105,"buy price");
            r=execute(e,"SELL 5 100 3 GTC");require(r.trades.size()==2 && r.trades[0].maker==8 && r.trades[1].maker==2,"FIFO");e.validate();
        });
        test("GTC remainder, cancellation within FIFO and ID reuse",[]{
            Engine e;execute(e,"SELL 1 100 1 GTC");auto r=execute(e,"BUY 2 101 4 GTC");
            require(r.filled==1 && r.resting==3,"GTC remainder");
            execute(e,"BUY 3 101 2 GTC");execute(e,"BUY 4 101 2 GTC");
            require(e.cancel(3).cancelled==2,"middle cancel");
            r=execute(e,"SELL 5 101 4 GTC");require(r.trades[1].maker==4,"FIFO after cancel");
            require(!execute(e,"BUY 3 100 1 GTC").ok && !execute(e,"BUY 1 100 1 GTC").ok,"ID reuse");e.validate();
        });
        test("IOC partial and unmarketable expiry",[]{
            Engine e;execute(e,"SELL 1 100 2 GTC");auto r=execute(e,"BUY 2 100 5 IOC");
            require(r.filled==2 && r.cancelled==3 && r.resting==0 && e.activeCount()==0,"IOC partial");
            execute(e,"SELL 3 102 2 GTC");r=execute(e,"BUY 4 100 2 IOC");
            require(r.filled==0 && r.cancelled==2,"IOC no match");e.validate();
        });
        test("FOK atomic liquidity precheck and exact full fill",[]{
            Engine e;execute(e,"SELL 1 100 2 GTC");execute(e,"SELL 2 101 3 GTC");
            auto before=e.orders();auto r=execute(e,"BUY 3 100 3 FOK");
            require(r.cancelled==3 && r.trades.empty(),"FOK should cancel");bookEqual(e.orders(),before);
            r=execute(e,"BUY 4 101 5 FOK");require(r.filled==5 && r.cancelled==0 && e.activeCount()==0,"FOK full");e.validate();
        });
        test("market consumes levels, cancels remainder and empty book",[]{
            Engine e;auto r=execute(e,"BUY 1 0 2 MARKET");require(r.cancelled==2,"empty market");
            execute(e,"BUY 2 100 2 GTC");execute(e,"BUY 3 99 1 GTC");
            r=execute(e,"SELL 4 0 5 MARKET");require(r.filled==3 && r.cancelled==2 && r.trades[1].price==99,"market sweep");e.validate();
        });
        test("sell FOK and full depletion of a multi-order level",[]{
            Engine e;execute(e,"BUY 1 101 20 GTC");execute(e,"BUY 2 101 10 GTC");execute(e,"BUY 3 100 20 GTC");
            auto r=execute(e,"SELL 4 101 31 FOK");require(r.filled==0 && r.cancelled==31,"sell FOK limit");
            r=execute(e,"SELL 5 100 50 FOK");require(r.filled==50 && r.trades.size()==3 && e.activeCount()==0,"sell FOK sweep");e.validate();
        });
        test("reset and supported numeric bounds",[]{
            Engine e;require(execute(e,"BUY 9007199254740991 1000000000 1000000000 GTC").ok,"max bounds");
            require(!execute(e,"BUY 9007199254740992 1 1 GTC").ok,"browser ID bound");
            require(!execute(e,"BUY 1 1000000001 1 GTC").ok,"price bound");
            e.clear();require(e.activeCount()==0 && e.tradeCount()==0,"reset");require(execute(e,"BUY 1 100 1 GTC").ok,"reset IDs");e.validate();
        });
        test("deterministic replay produces identical snapshots",[]{
            Engine a,b;
            for(const auto& s:{"SELL 1 100 2 GTC","SELL 2 101 3 GTC","BUY 3 101 4 IOC","CANCEL 2"}) {
                auto x=execute(a,s),y=execute(b,s);require(json(a,x)==json(b,y),"replay differs");
            }
        });
        test("20,000 randomized events vs independent vector reference",[]{
            for(unsigned seed=1;seed<=4;++seed) {
                std::mt19937 rng(seed);Engine e;Reference ref;Id next=1;
                for(int step=0;step<5000;++step) {
                    Result a,b;
                    if(rng()%5==0) {Id id=1+rng()%(next+5);a=e.cancel(id);b=ref.cancel(id);}
                    else {
                        Id id=(rng()%20==0 && next>1)?1+rng()%(next-1):next++;
                        Type type=static_cast<Type>(rng()%4);
                        Request r{id,rng()%2?Side::Buy:Side::Sell,type,type==Type::Market?0:95+static_cast<Value>(rng()%11),1+static_cast<Value>(rng()%30)};
                        a=e.submit(r);b=ref.submit(r);
                        if(a.ok) require(a.filled+a.resting+a.cancelled==r.quantity,"quantity conservation");
                    }
                    equal(a,b);bookEqual(e.orders(),ref.orders());e.validate();
                }
            }
        });
        std::cout<<groups<<" groups passed; 20,000 differential events checked.\n";
    } catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
