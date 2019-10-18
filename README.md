## Tunnel
**sudo ip -6 addr add fd00:0:0:5000::1/64 dev eth0**
**make TARGET=cooja connect-router-cooja inside** /Border\ router

## Ideas
1. Minimum RSSI is -94dBm (when the nodes are put at the last meter of the communication range).
2. My strategy is to start with Transmission power of 31 (max), receive the RSSI from the cluster head and decrease the Transmission power if the RSSI value was above a certain threshold (at least > -70dBm).
 References: https://www.metageek.com/training/resources/understanding-rssi.html

## Next steps
- [X] Add client CH selection based on the RSSI of the received multicast message sent by the CH. In this way, the client can add the nearest CH which is the one with the greater RSSI.
- [ ] Calculate the PRR.
- [X] Change randomly the CHs selection.
- [ ] Implement an aggregation algorithm at cluster head.
- [X] Fix optimal power function.
- [X] CH selection does not work at first iteration.
