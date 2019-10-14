## Tunnel
**make TARGET=cooja connect-router-cooja inside** /examples/rpl-border-router

**sudo ip -6 addr add fd00:0:0:5000::1/64 dev eth0**

## Ideas
1. Minimum RSSI is -94dBm (when the nodes are put at the last meter of the communication range).
2. My strategy is to start with Transmission power of 31 (max), receive the RSSI from the cluster head and decrease the Transmission power if 
the RSSI value was above a certain threshold (at least > -70dBm). https://www.metageek.com/training/resources/understanding-rssi.html