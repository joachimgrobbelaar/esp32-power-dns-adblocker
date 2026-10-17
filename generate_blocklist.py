#!/usr/bin/env python3
"""
Blocklist Compiler for ESP32 High-Capacity DNS Sinkhole
Fetches domains from HaGeZi wildcard lists (Light, PopupAds, Native TikTok),
adds mobile reel SDKs (Pangle, Unity, AppLovin, Mintegral, IronSource),
paywall blockers (Tinypass, Piano.io), and anti-adblock scripts.
Generates binary blocklist.bin with FNV-1a 64-bit sorted hashes.
"""

import urllib.request
import struct
import os

def fnv1a_64(s):
    h = 14695981039346656037
    for c in s.lower().strip().encode('utf-8'):
        h ^= c
        h = (h * 1099511628211) & 0xFFFFFFFFFFFFFFFF
    return h

urls = [
    'https://raw.githubusercontent.com/hagezi/dns-blocklists/main/wildcard/light-onlydomains.txt',
    'https://raw.githubusercontent.com/hagezi/dns-blocklists/main/wildcard/popupads-onlydomains.txt',
    'https://raw.githubusercontent.com/hagezi/dns-blocklists/main/wildcard/native.tiktok-onlydomains.txt',
]

domains = set()

# High-priority manual additions: video ads, reel SDKs, popups, paywalls, analytics
custom_domains = [
    # Video & Reel In-App Ad Networks
    "ads.tiktok.com", "analytics.tiktok.com", "business-api.tiktok.com", "log.tiktokv.com",
    "ib.tiktokv.com", "mon.tiktokv.com", "ad.oceanengine.com", "pglstatp-toutiao.com",
    "pangle.io", "pangolin-sdk-toutiao.com", "ads.unity3d.com", "auction.unityads.unity3d.com",
    "webview.unityads.unity3d.com", "a.applovin.com", "ms.applovin.com", "d.applovin.com",
    "rt.applovin.com", "net.mintegral.com", "pgl.mintegral.com", "init.supersonicads.com",
    "outcome.ironsrc.com", "ads.api.vungle.com", "config.vungle.com", "config.inmobi.com",
    "ads.inmobi.com", "live.chartboost.com", "da.chartboost.com", "ads.liftoff.io",
    "engine.fyber.com", "events3alt.adcolony.com", "ads.adcolony.com",
    # Popups / View-Blockers / Anti-Adblock
    "adblock-analytics.com", "blockadblock.com", "detectadblock.com", "getadmiral.com",
    "admiral.com", "ad-score.com", "adblocker-detector.com",
    # Paywalls & Membership Enforcers
    "tinypass.com", "piano.io", "api.tinypass.com", "experience.tinypass.com",
    "qiota.com", "evolok.net", "poool.fr",
    # Major Web Trackers & Ad Exchanges
    "doubleclick.net", "googlesyndication.com", "googleadservices.com", "google-analytics.com",
    "adservice.google.com", "adnxs.com", "criteo.com", "criteo.net", "taboola.com",
    "outbrain.com", "scorecardresearch.com", "hotjar.com", "fullstory.com", "mouseflow.com",
    "crazyegg.com", "onesignal.com", "braze.com", "branch.io", "adjust.com", "appsflyer.com",
    "singular.net", "kochava.com", "facebook.com/tr", "connect.facebook.net"
]

for d in custom_domains:
    domains.add(d.strip().lower())

for url in urls:
    print(f"Fetching {url}...")
    try:
        req = urllib.request.Request(url, headers={'User-Agent': 'Mozilla/5.0'})
        with urllib.request.urlopen(req) as resp:
            for line in resp:
                line = line.decode('utf-8', errors='ignore').strip()
                if not line or line.startswith('#'):
                    continue
                domains.add(line.lower())
    except Exception as e:
        print(f"Error fetching {url}: {e}")

print(f"Total unique domains collected: {len(domains)}")

hashes = set()
for d in domains:
    hashes.add(fnv1a_64(d))

sorted_hashes = sorted(list(hashes))
print(f"Total unique sorted hashes: {len(sorted_hashes)}")

out_path = "blocklist.bin"
with open(out_path, "wb") as f:
    f.write(struct.pack("<III", 0x424C4B4C, 1, len(sorted_hashes)))
    for h in sorted_hashes:
        f.write(struct.pack("<Q", h))

fsize = os.path.getsize(out_path)
print(f"Generated {out_path}: {fsize} bytes ({fsize / 1024:.1f} KB, {fsize / (1024*1024):.2f} MB)")
