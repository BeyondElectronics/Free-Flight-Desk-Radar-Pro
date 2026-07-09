




# ✈️ DeskRadar ProUp

> Real-time aircraft tracking for your desktop — powered by OpenSky Network.
> Based on a project from MakerWorld.com, FlightDeskRadar Pro by KORNect: 
https://makerworld.com/en/models/2893782-flightdeskradar-pro#profileId-3233701

---

## 📋 Table of Contents
- [DEVICE PIN: 1234 ]
- [Overview](#overview)
- [Features](#features)
- [Hardware Requirements](#hardware-requirements)
- [Software Requirements](#software-requirements)
- [First-Time Setup](#first-time-setup)
  - [WiFi Configuration](#wifi-configuration)
  - [OpenSky API Key](#opensky-api-key)
  - [Coordinate Configuration](#coordinate-configuration)
- [Settings & Customization](#settings--customization)
  - [Plane Count](#plane-count)
  - [Tracking Range](#tracking-range)
  - [Grid Overlay](#grid-overlay)
- [Usage](#usage)
- [Troubleshooting](#troubleshooting)
- [License](#license)

---

## Overview

**DeskRadar Pro** is a desktop aircraft tracking device that displays live flight data in real time using the free [OpenSky Network](https://opensky-network.org/) API. It renders nearby planes on a customizable map grid centered on your configured coordinates, making it ideal for aviation enthusiasts, hobbyists, and desk setups.

---

## Features

| Feature | Description |
|---|---|
| 🛩️ Active Plane Tracking | Live aircraft data via the free OpenSky Network API |
| 🔢 Custom Plane Count | Display between **5 and 20** planes simultaneously |
| 📡 Adjustable Range | Track aircraft within **50 km to 300 km** of your location |
| 🟦 Square Grid Toggle | Enable or disable the map grid overlay with a toggle switch |
| 🔲 Grid Transparency | Fine-tune grid opacity using a dedicated transparency slider |
| 📍 Custom Coordinates | Set any latitude/longitude as your tracking center point |
| 📶 WiFi Connected | Connects to your home WiFi network for live data |
| 🔑 API Key Support | Placeholder slot for your OpenSky API key on first-time setup |

---

## Hardware Requirements

| Component | Details |
|---|---|
| **Main Board / Display** | ESP32-2432S028R ("Cheap Yellow Display" / CYD) |
| **Processor** | ESP32 dual-core, 240 MHz |
| **Display** | 2.8" TFT LCD, 240×320 resolution, ILI9341 driver |
| **Connectivity** | Built-in WiFi (2.4 GHz) & Bluetooth |
| **Touch** | Resistive touchscreen (XPT2046 controller) |
| **Power** | USB-C power cable and adapter |
| **Network** | WiFi-enabled home network (2.4 GHz) |
| **Mounting** | Stable surface or compatible 3D-printed enclosure |

---

## Software Requirements

- DeskRadar ProUp firmware (pre-installed)
- OpenSky Network account *(free — for API key generation)*
- Compatible mobile or desktop configuration app *(for initial setup)*

---

## First-Time Setup

### WiFi Configuration

1. Power on your DeskRadar Pro device.
2. On first boot, the device will broadcast a temporary setup hotspot named **`DeskRadar-Setup`**.
3. Connect your phone or computer to this hotspot.
4. Open a browser and navigate to `192.168.4.1`.
5. Select your home WiFi network from the list and enter your WiFi password.
6. The device will reboot and connect to your home network automatically.

> **Note:** DeskRadar Pro supports WPA2/WPA3 secured networks. Guest networks with client isolation may prevent API communication.

---

### OpenSky API Key

DeskRadar ProUp uses the **free OpenSky Network API** for live aircraft data. An API key is optional but recommended for higher request limits.

**To generate your free API key:**

1. Visit [https://opensky-network.org/](https://opensky-network.org/) and create a free account.
2. Log in and navigate to **My Profile → API Credentials**.
3. Copy your **username** and **password** — these serve as your API credentials.
4. In the DeskRadar Pro settings menu, locate **API Configuration**.
5. Enter your OpenSky credentials in the provided fields:

```
OpenSky Username:  [ YOUR_USERNAME_HERE ]
OpenSky Password:  [ YOUR_PASSWORD_HERE ]
```

> **Tip:** Without credentials, DeskRadar Pro will use the anonymous OpenSky endpoint, which has lower rate limits (≈ 400 requests/day). Registered accounts receive significantly higher limits.

---

### Coordinate Configuration

Set your tracking center point using latitude and longitude coordinates.

1. Open **Settings → Location → Custom Coordinates**.
2. Enter your desired coordinates:

```
Latitude:   [ e.g., 42.2211 ]
Longitude:  [ e.g., -88.3162 ]
```

3. Tap **Save & Apply**. The map will re-center on your new location immediately.

> **Tip:** Use [Google Maps](https://maps.google.com) or [latlong.net](https://www.latlong.net/) to look up precise coordinates for your location.

---

## Settings & Customization

### Plane Count

Control how many aircraft are displayed at once.

- **Range:** `5` to `20` planes
- **Default:** `10`
- **Location:** Settings → Display → Max Planes

Planes are prioritized by proximity to your center coordinates. When more aircraft are in range than your set limit, the closest ones are shown first.

---

### Tracking Range

Adjust the radius around your center point that DeskRadar Pro monitors.

- **Range:** `50 km` to `300 km`
- **Default:** `150 km`
- **Location:** Settings → Radar → Detection Range

> Larger ranges increase the number of aircraft visible but may reach API rate limits faster on anonymous connections.

---

### Grid Overlay

DeskRadar ProUp includes a square map grid overlay for spatial reference.

#### Toggle Switch

- **Location:** Settings → Display → Grid Overlay → Enable/Disable
- Flip the toggle switch to instantly show or hide the grid.

#### Transparency Slider

- **Location:** Settings → Display → Grid Overlay → Opacity
- Slide left for a more transparent grid, right for a fully opaque grid.
- **Range:** `0%` (invisible) to `100%` (fully visible)
- **Default:** `40%`

---

## Usage

Once setup is complete, DeskRadar Pro operates automatically:

1. **Power on** the device — it connects to WiFi and begins fetching live data.
2. The display renders all aircraft within your configured range on the grid.
3. Each blip shows:
   - ✈️ Flight callsign / ICAO identifier
   - 📍 Current position on grid
   - 🔼 Altitude (meters or feet, configurable)
   - 🧭 Heading direction indicator
4. Data refreshes automatically every **15–30 seconds** depending on your API tier.

---

## Troubleshooting

| Issue | Possible Cause | Solution |
|---|---|---|
| No planes displayed | Out of API rate limit or no flights in range | Wait 60 seconds or reduce tracking range |
| Device not connecting to WiFi | Incorrect password or unsupported network | Re-run WiFi setup via the setup hotspot |
| Stale / frozen data | Lost internet connection | Check WiFi connection; reboot device |
| Grid not appearing | Grid toggle is off | Enable via Settings → Display → Grid Overlay |
| Coordinates not saving | Input format error | Use decimal format (e.g., `42.2211`, `-88.3162`) |
| API key not accepted | Incorrect credentials | Double-check username/password on opensky-network.org |

---

## License

DeskRadar Pro is a personal/hobbyist project. All flight data is sourced from the [OpenSky Network](https://opensky-network.org/), which is made available under the [OpenSky Network Terms of Use](https://opensky-network.org/about/terms-of-use).

---

*README last updated: July 2026 · DeskRadar Pro*
