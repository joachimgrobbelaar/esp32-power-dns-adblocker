# ESP32 AdBlock NAT Router & Remote PC Power Master

A high-performance dual-mode Wi-Fi router, DNS sinkhole ad-blocker, and remote PC power management station running on an ESP32 DevKit V4.

## Key Features

1. **Zero-Config Wi-Fi Protection (Dual AP + STA with lwIP NAPT Router)**:
   - Connects to home Wi-Fi (`NM45_2.4GHz`) on STA interface.
   - Broadcasts high-speed local Access Point: `AdBlock_WiFi` (Password: `adblock123`, Gateway: `192.168.4.1`).
   - Built-in hardware lwIP NAPT (Network Address Port Translation) routes client traffic with zero setup needed on phones, tablets, smart TVs, or PCs.
   - All client DNS queries automatically hit the ESP32 DNS interceptor on UDP port 53.

2. **High-Capacity Flash-Mapped DNS Sinkhole (103,681 Domains)**:
   - Compiles blocklists from **HaGeZi** (`light`, `popupads`, `native.tiktok`), mobile reel SDKs (Pangle, Unity, AppLovin, Mintegral, IronSource), video pre-rolls, popups, and paywall enforcers (Piano.io, Tinypass).
   - Domain hashes are stored in the ESP32 SPIFFS partition (`0x290000`) and mapped directly to CPU memory space using `esp_partition_mmap()`.
   - Binary search over 103,681 64-bit FNV-1a hashes executes in **< 1 microsecond** using **zero RAM**!
   - Full suffix/wildcard subdomain matching (`*.doubleclick.net`, `*.tiktokv.com`, etc.).
   - Returns `0.0.0.0` (IPv4) and `::` (IPv6) for instant connection drop without retry delays.

3. **Remote PC Power Control (Wake-on-LAN + HTTP Daemon Shutdown)**:
   - Hardware WoL magic packet broadcasting to target PC (`30:9C:23:9A:B5:FA`) on UDP ports 9 & 7.
   - Graceful remote shutdown via HTTP daemon (`http://192.168.0.50:8999/api/poweroff`).
   - Real-time online/offline status monitoring via heartbeat endpoint.

4. **Web UI & REST Dashboard**:
   - Access from LAN (`http://192.168.0.201`), from AdBlock Wi-Fi (`http://192.168.4.1`), or via mDNS (`http://esp32.local`).
   - Real-time stats: query counters, blocked percentage, connected Wi-Fi clients.
   - Interactive domain lookup testing tool directly in the browser.
