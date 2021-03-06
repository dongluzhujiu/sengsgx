#!/bin/bash
sudo ip tuntap add mode tun user `whoami` one_queue name tunFA || exit 1
# Setting MTU of tunnel to fitting size will prevent IP fragmentation at outer layer (DTLS layer);
# Alternatively set it to about 65kB to shift most IP fragmentation from inner to outer layer;
# Be careful with value in mid-range, bcs. they might cause inner+outer IP fragmentation;
# If setting MTU of tunnel device fitting, remember to update it if using jumbo frames / non-1500-mtu link layer;
sudo ip link set tunFA mtu 1432 || exit 1
sudo ip link set tunFA up || exit 1

# TODO: adapt based on your defined enclave IP subnetworks
sudo ip addr add 192.168.28.1/24 dev tunFA || exit 1
# used in sqlite3 demo database for the 2nd enclave network
sudo ip addr add 172.16.28.1/24 dev tunFA || exit 1

# TODO: in the following rules, change "eno1" to your interface name(s)

# Allow forwarding of packets from/to Enclaves (important if FORWARD default is DROP)
#sudo iptables -A FORWARD -i tunFA -o eno1 --src 192.168.28.0/24 -j ACCEPT
#sudo iptables -A FORWARD -i tunFA -o eno1 --src 172.16.28.0/24 -j ACCEPT
#sudo iptables -A FORWARD -i eno1 -o tunFA --dst 192.168.28.0/24 -j ACCEPT
#sudo iptables -A FORWARD -i eno1 -o tunFA --dst 172.16.28.0/24 -j ACCEPT

# Enable NAT for outgoing Enclave packets, e.g., to Google DNS server
#sudo iptables -t nat -A POSTROUTING --src 192.168.28.0/24 -o eno1 -j MASQUERADE
#sudo iptables -t nat -A POSTROUTING --src 172.16.28.0/24 -o eno1 -j MASQUERADE
