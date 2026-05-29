# speedtest-cli

A lightweight, open-source network utility in C for testing network connectivity and gathering provider information.

## Features

- **Ping Tests**: Measure network latency to popular hosts.
- **Provider Lookups**: Retrieve IP/Network information from various providers (ipapi, ipinfo, cloudflare, fastly, httpbin).
- **JSON Output**: Supports machine-readable JSON output for easy integration.
- **Fast & Minimal**: Written in C, using `libcurl` and `cJSON`.

## Build Requirements

- `gcc`
- `make`
- `libcurl`
- `cJSON`

## Build and Run

```bash
# Build
make all

# Run (default to all providers)
make run

# Run specific provider
./bin/speedtest --provider=ipinfo

# Ping only
./bin/speedtest --ping-only
```

## Usage

```bash
Usage:
  ./bin/speedtest [options]

Options:
  --provider=<slug>   Provider to query (default: all)
  --list              List available providers and exit
  --ping-only         Only run TCP-connect ping test
  --json              Output results as JSON
  --count=<n>         Ping probes per host (default: 4)
  --api-key=<key>     API key for providers
  --help              Show this help
```

## License

This project is licensed under the MIT License.
