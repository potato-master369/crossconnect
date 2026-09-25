#!/bin/sh
# Requires openVPN.
openvpn --dev null --remote 127.0.0.1 1194 --secret none --management /var/run/openvpn-mgmt.sock unix --management-hold
