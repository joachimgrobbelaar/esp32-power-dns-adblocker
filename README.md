# ESP32 AdBlock NAT Router, GameSir Bluetooth HID Bridge, Desktop Launcher & Cross-Network Hub

A high-performance dual-mode Wi-Fi NAT router, 103,681-domain Flash-mapped DNS sinkhole ad-blocker, Dual-Mode Bluetooth (BLE + Classic BR/EDR) HID Host for **GameSir Controllers** & **Bluetooth Mice**, Desktop/TV App Launcher, Wireless Mouse/Keyboard Touchpad, and Cross-Network All-Devices Hub running on an ESP32 DevKit V4.

## Key Features

1. **GameSir Controller & Bluetooth Mouse Hardware Receiver (`esp32_pc_adblock.ino` + `nexus-power-daemon.py`)**:
   - Provides native Dual-Mode Bluetooth (`BLE` + `Classic BR/EDR`) on the ESP32 for hosts without built-in Bluetooth adapters.
   - Auto-scans and auto-pairs **GameSir** controllers (`GameSir`, `T4`, `G8`, `Nova`, `X2`, `G7`, `Gamepad`) and Bluetooth Wireless Mice (`Mouse`, `MX`, `Logi`).
   - Streams HID reports over USB Serial (`/dev/ttyUSB0` at `115200` baud via `@HID:` frames) and HTTP to Linux kernel `evdev.UInput` virtual devices (`ESP32-GameSir-Bluetooth-Controller` & `ESP32-Wireless-Mouse`).
   - Includes **Stick-to-Mouse Mode** (left analog stick controls desktop cursor, A/B buttons left/right click) plus an interactive Web Virtual GameSir Pad.

2. **Start / Restart Portal + Full Desktop & TV Hub App Launcher**:
   - **Power Management**: Hardware Wake-on-LAN (`WoL`) magic packet broadcast (`30:9C:23:9A:B5:FA`), Restart/Reboot, Sleep/Suspend, and Power Off.
   - **Desktop & TV App Launcher**: 1-click launch for `TV Hub`, `Apple TV+`, `YouTube TV`, `YT Music`, `Disney+`, `Prime Video`, `Cast Phone Screen` (Redmi 13C `scrcpy`), `P10 Smart TV`, `Redmi Cast Remote`, `Screen Magnifier`, `Fix HDMI Audio`, and `Jellyfin`.
   - **Wireless Mouse & Keyboard**: Multi-touch trackpad with drag, tap-to-click, 2-finger right-click, vertical scrollbar, full keyboard text input, arrow keys, and media transport controls.

3. **All-Network Devices & Android Cross-Subnet Hub (`192.168.4.x` ↔ `192.168.0.x`)**:
   - Bi-directional routing and lwIP NAPT port-forwarding (`8080`, `8999`, `2121`, `5555`, `8096`) between `AdBlock_WiFi` (`192.168.4.0/24`) and the main LAN (`192.168.0.0/24`).
   - Automatic ARP/ping + DHCP discovery of all LAN and `AdBlock_WiFi` devices (including Android phones like Redmi 13C and P10 tablets).
   - 1-click **Android Screen Mirror (`scrcpy`)**, **Wireless ADB Connect (`:5555`)**, and direct **FTP (`ftp://192.168.0.50:2121`)** / **HTTP Remote (`http://192.168.0.50:8080`)** access across both networks.

4. **High-Capacity Flash-Mapped DNS Sinkhole (103,681 Domains)**:
   - Compiles blocklists from **HaGeZi** (`light`, `popupads`, `native.tiktok`), mobile reel SDKs (Pangle, Unity, AppLovin, Mintegral, IronSource), video pre-rolls, popups, and paywall enforcers (Piano.io, Tinypass).
   - Domain hashes are stored in the ESP32 SPIFFS partition (`0x290000`) and mapped directly to CPU memory space using `esp_partition_mmap()` (`< 1 microsecond` binary search with **0 bytes of RAM**).
