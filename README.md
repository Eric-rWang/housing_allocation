# Housing Allocation Problem

We presents an implementation of the Top
Trading Cycle (TTC) algorithm, a method for achieving Pareto
optimal and core-stable allocations in the housing allocation
problem, where each agent has strict preferences over indivisible
resources. Using socket programming in C++, we simulate a
distributed environment where each agent communicates asyn-
chronously to exchange preferences and execute trades. For
efficient cycle detection, we employ a modified, deterministic
version of the Las Vegas algorithm based on a cycle-finding
technique for functional graphs. This approach preserves the
TTC algorithm’s guarantees while optimizing communication
and computation. We evaluate the performance of our implemen-
tation and demonstrate the effectiveness of deterministic cycle
detection in distributed resource allocation
