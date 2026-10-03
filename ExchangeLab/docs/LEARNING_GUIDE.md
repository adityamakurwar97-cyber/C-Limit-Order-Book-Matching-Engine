# ExchangeLab ko samajhne ka sequence

Code ko ek saath yaad mat karo. Har stage par example predict karo, phir program se verify karo.

## 1. Pehle tumhara old engine

Order has ID, side, price, quantity, sequence. Best bid = highest buying price. Best ask = lowest selling price. Buy limit >= sell limit ho toh trade possible. Tick abstract price unit hai: currency mapping humne define nahi ki.

Exercise: buyer limit 102, seller limit 103. Trade kyun nahi hota?

## 2. Price level kya hai?

Same price par saare orders ek queue mein. Example: 10000 par seller ID 90 first, ID 2 second. Buyer aaya toh ID 90 execute hoga. Smaller ID ka priority se relation nahi.

Map prices ko ordered rakhta hai. List orders ka arrival order rakhti hai. Dono alag problems solve karte hain. Read engine.hpp: Level, Bids, Asks.

## 3. Iterator aur reference

Reference actual object ka alias hai; copy nahi. Iterator container ke element ko refer karta hai. ID index mein list iterator store hota hai. List mein ek order remove karne par doosre orders ke iterators valid rehte hain. Removed order ka iterator use nahi karna.

Engine copy disabled hai because internal iterators original object's lists ko point karte hain. Shallow/default copying would be unsafe.

## 4. Trade accounting

Buyer wants 7, seller has 3. min(7,3)=3 trade, buyer 4 remains, seller 0. Both sides subtract same quantity. Trade maker ki existing price par hota hai, midpoint par nahi.

Read match(). Check incoming remainder, maker remainder, level total, volume, trade history. Queue empty ho toh price level bhi remove hota hai.

## 5. Instructions

GTC: unmatched quantity waiting book mein.
IOC: jo abhi fill ho sake fill, baaki cancel.
FOK: all requested quantity eligible prices par available ho tabhi execute; nahi toh zero trades.
MARKET: price limit nahi, existing opposite orders consume; book empty hone par leftover cancel.

Exercise: asks 100 x 2, 101 x 3. BUY 4 units with limit 100:
GTC -> 2 fill + 2 rest. IOC -> 2 fill + 2 cancel. FOK -> 0 fill + 4 cancel.

## 6. Cancellation

ID hash table tells where order lives. Find corresponding price level, subtract remaining quantity from aggregate, remove list node and index record. Historical trades reverse nahi hote. Cancelled ID reuse reject hota hai until reset.

## 7. Replay

A journal stores commands in exact arrival order. Same commands and same empty starting state produce same trades. Wall-clock timing does not determine queue priority. Pausing changes viewing speed, not execution order.

Export a session, load it in Replay, step to end and compare. Python tests check exact final JSON book equality.

## 8. Correctness before performance

Reference model simpler vector engine hai. Same randomized events dono engines ko dete hain. Output equal nahi hua toh bug hai; speed se pehle correctness fix karo. Invariant means condition that must stay true, e.g. active quantity > 0.

Read tests/reference.hpp then tests/test_engine.cpp. Practice adding more same-price FIFO cases and cancellation of the last order in a level.

## 9. Benchmarks

Throughput = measured events / elapsed seconds. p99 = 99 percent observed events is duration ke andar processed hue. One slow event or OS interruption tail affect kar sakta hai. UI/HTTP time engine processing se different hai.

Read docs/BENCHMARKS.md before quoting numbers. Current harness times allocations too; no custom memory pool or CPU pinning.

## 10. Browser connection

Browser sends command -> Python forwards -> C++ matches -> Python returns JSON -> browser redraws. Front end quantities decide nahi karta. API serialization slower ho sakti hai than core match; benchmark excludes it.

## Notes template

- Concept in my own words:
- Problem it solves:
- Relevant file/function:
- Manual example before/after:
- One failure case:
- Trade-off of our implementation:

## Interview readiness questions

1. Why integer ticks instead of double?
2. Why a linked list within each price level?
3. What invalidates a stored iterator?
4. Why is cancellation O(log P) here, not unconditional O(1)?
5. Why does FOK need a precheck?
6. Why can failed FOK consume an ID?
7. Which quantities must remain conserved?
8. Why aren't benchmark numbers portable across machines?
9. What would be needed for crash recovery?
10. What part would you optimize next, and what measurement supports it?
