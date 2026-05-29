# speedtest-cli

A lightweight command-line network speed and latency tester written in C,
using **libcurl** for HTTP and **cJSON** for response parsing.

## Features

- Query multiple open-source/public APIs to measure latency and gather
  network geo-info
- TCP-connect ping (no root/ICMP required)
- HTTP TTFB and total transfer timing via libcurl's timing API
- Choose a single provider or run all at once
- Optional JSON output for scripting/piping
- Ping-only mode

## Providers

| Slug          | Service           | Data returned                      |
|---------------|-------------------|------------------------------------|
| `ipapi`       | ip-api.com        | IP, city, region, country, ISP/org |
| `ipinfo`      | ipinfo.io         | IP, city, region, country, org     |
| `cloudflare`  | Cloudflare trace  | IP, country, PoP colo, org         |
| `fastly`      | Fastly edge API   | IP, PoP city/country, ASN          |
| `httpbin`     | httpbin.org       | IP echo, connection quality        |
| `all`         | All of the above  | (default)                          |

All providers also report:
- **TCP-connect latency** (average of N probes to port 443/80)
- **TTFB** (time-to-first-byte via libcurl)
- **Total HTTP transfer time** and HTTP status code

## Dependencies

```
libcurl-dev   (tested with libcurl 8.x)
libcjson-dev  (tested with cJSON 1.7.x)
gcc + make
```

On Ubuntu/Debian:
```sh
sudo apt install libcurl4-openssl-dev libcjson-dev build-essential
```

On Fedora/RHEL:
```sh
sudo dnf install libcurl-devel cjson-devel gcc make
```

On macOS (Homebrew):
```sh
brew install curl cjson
```

## Build

```sh
make           # optimised build -> ./speedtest
make debug     # with ASan/UBSan
make install   # copies to /usr/local/bin (needs sudo)
make clean
```

## Usage

```
speedtest [options]

Options:
  --provider=<slug>   Provider to query  (default: all)
  --list              List providers and exit
  --ping-only         Only run TCP-connect ping, skip HTTP fetch
  --json              Output results as JSON (good for piping)
  --count=<n>         Ping probes per host, 1-20 (default: 4)
  --help              Show help
```

### Examples

```sh
# Run all providers (default)
./speedtest

# Single provider
./speedtest --provider=cloudflare

# Quick latency check across all providers
./speedtest --ping-only --count=3

# Machine-readable output
./speedtest --provider=ipinfo --json

# Pipe JSON into jq
./speedtest --provider=ipapi --json | jq '.ping_ms'
```

## Project layout

```
speedtest/
  include/
    speedtest.h       -- shared types and function declarations
    curl_helper.h     -- libcurl GET helper (header-only)
  src/
    ping.c            -- TCP-connect latency measurement
    connection.c      -- HTTP TTFB / timing via libcurl
    json_parse.c      -- per-provider response parsers (cJSON)
    providers.c       -- provider table, orchestration, pretty-print
    main.c            -- CLI argument parsing, entry point
  Makefile
  README.md
```

## Adding a new provider

1. Add a new `PROVIDER_*` constant to `include/speedtest.h`.
2. Write a `SpeedResult parse_myprovider(const char *body)` function in
   `src/json_parse.c` and declare it in the header.
3. Add a `ProviderDef` entry to the `PROVIDERS[]` table in
   `src/providers.c`.
4. `make` — that's it.

## Notes

- **No root required.** Ping uses TCP-connect to port 443/80 rather than
  raw ICMP sockets.
- **No keys required.** All providers used are free and unauthenticated.
  Some (ipinfo.io) have rate limits on the free tier.
- The Fastly endpoint (`api.fastly.com/public-ip-list`) is a public
  informational endpoint, not an official speed-test service.
