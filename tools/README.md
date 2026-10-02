# tools/ - exploration and probe scripts

Throwaway helpers used while working out the interfaces. They are kept for
reference; none of them is part of the build or the tests.

## MumbleLink probes (PowerShell)

Guild Wars 2 publishes character/position state to the Win32 named section
`\BaseNamedObjects\MumbleLink`. These read it (or the process that owns it):

| Script | What it does |
|--------|--------------|
| `find_cwd.ps1` | Reads a process's working directory out of its PEB (`NtQueryInformationProcess`). |
| `find_mumble.ps1` | Finds the `Gw2-64` process and its working directory. |
| `mumble_probe.ps1` | Opens `MumbleLink` and prints the decoded fields. |
| `scan_mumble.ps1` | Reads a named section and scans it for known byte patterns. |
| `watch_mumble.ps1` | Polls `MumbleLink` and prints the fields as they change. |

## GW2 API probes (Python)

| Script | What it does |
|--------|--------------|
| `probe_api.py` | Hits `/v2/...` endpoints to see what the public API returns. |
| `probe_api2.py` | Same, for a second set of endpoints. |
| `probe_api3.py` | Same, for a third set. |
| `probe_key.py` | Checks the API key in `.env` and what it is allowed to read. |

The Python probes read the API key from the repo-root `.env` (gitignored).
