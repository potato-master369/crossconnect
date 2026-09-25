# openvpn-crossconnect - OpenVPN Connect for linux

All the other options out there are straight ass, so I made this myself. Doesn't need systemd and only requires GTK4, polkit, libnotifyd and (of course) openVPN. This is a WIP.

## Build

```
make
```

## Usage

Open the CrossConnect program, and press the button to load your .ovpn config, then press "Start OpenVPN" to start it. Once you're done, press "Disconnect" to disconnect.

## To-do

* User configuration
* Saving last used path for ovpn config
