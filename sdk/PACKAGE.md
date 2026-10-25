# Aeon game packages

A game ships as a build directory with an `aeon.json` at its root. `aeon-pack` validates it,
hashes every file, packages it, and (at Aeon) signs and registers it. Consoles verify the
signature against Aeon's key before trusting anything in the manifest, and verify each
payload's sha256 against the signed manifest before extracting it.

## aeon.json (manifest version 2)

```json
{
  "aeon_manifest": 2,
  "sku": "AEON-RALLY-STD",
  "title": "Aeon Rally",
  "version": "1.0.0",
  "developer": "Velocity Games",
  "platform": "linux-x86_64",
  "runtime": { "type": "native" },
  "exec": "AeonRally.x86_64",
  "args": ["--fullscreen"],
  "env": { "SDL_VIDEODRIVER": "wayland" },
  "workdir": ".",
  "saves": ["~/.local/share/AeonRally"],
  "requires": { "disk_mb": 1200 },
  "content": [
    { "id": "base" },
    { "id": "season-pass", "sku": "AEON-RALLY-SP1", "dir": "dlc/season-pass" }
  ]
}
```

| Field | Required | Meaning |
|---|---|---|
| `aeon_manifest` | yes | `2` |
| `sku` | yes | Store SKU. Uppercase letters, digits, dashes. One SKU = one entitlement. |
| `title`, `developer` | yes | Shown in the Vault and Store. |
| `version` | yes | `1.2.3`. Consoles offer an update when the registered version is newer than the installed one. |
| `platform` | yes | `linux-x86_64` or `windows-x86_64`. |
| `runtime` | yes | `{type: "native"}` for Linux builds; `{type: "proton", version: "default" \| "<Proton build>"}` for Windows builds. Aeon pins the default Proton in the OS; name a build only if your game needs a specific one. |
| `exec` | yes | Launch target, relative to `workdir`. Native: an executable file. Proton: usually an `.exe`. |
| `args`, `env`, `workdir` | no | Launch arguments, environment, working directory (relative). |
| `saves` | recommended | Directories the game writes saves to. `~` is the player's home; for Proton, `{prefix}` is the Wine prefix root. These sync to the player's cloud saves. |
| `requires.disk_mb` | no | Installed size estimate shown before download. |
| `content` | no | First entry must be `base`. Further entries are DLC packs: `dir` inside the build, `sku` for the entitlement that unlocks it. Each pack is a separate payload. |

`aeon-pack build` adds `files[]` (path, sha256, size for every file in the base content),
payload names and hashes per `content` entry, and `built`. Studios never write those by hand.

## Workflow

```
aeon-pack init     ./build            # once: template aeon.json
aeon-pack validate ./build            # every time: plain-English errors
aeon-pack build    ./build --out ./out
aeon-pack install  ./build wiz@console   # a development console: in its Vault a second later
# hand ./out/<SKU>/ to Aeon
aeon-pack publish  ./out/<SKU> --r2   # Aeon: sign + upload + register
```

`install` needs only `ssh` and `tar` on both ends. It copies the build to `~/aeon/games/<SKU>/` on the console,
writes the `manifest.json` the console's launcher reads (the same file a store install produces, with the launch
line composed from `exec`, `args`, `env`, `workdir` and `runtime`) and asks a developer-mode shell to rescan;
`--clean` empties the directory first.

## What the console does with it

- `GET /me/library/<SKU>/manifest`: refused unless the player owns the SKU.
- Verifies the manifest signature (ed25519, Aeon's release key) before parsing it.
- Downloads the base payload from a presigned URL valid for one hour, checks its sha256.
- Installs to `~/aeon/games/<SKU>/`; updates extract beside the old install and swap atomically.
- Composes the launch line from `workdir`, `env`, `runtime`, `exec`, `args`. Proton builds run through
  `umu-run` with `GAMEID=umu-<SKU>` (and `PROTONPATH` when a build is named).
- Keeps `saves` and `files` in the local manifest for cloud-save sync and verify/repair.

## Rules of thumb

- Keep everything the game needs inside the build directory. Nothing outside it is packaged.
- Don't ship `.git`, symbolic links to outside the build, or absolute paths.
- Bump `version` for every build you want consoles to pick up.
- Version-1 manifests (`title/version/exec/payload/sha256`) still install, but get none of the above.

## Achievements and leaderboards

Definitions live next to `aeon.json` and ship with the build. `aeon-pack validate` checks them with the
same rules the server applies, and `aeon-pack publish` sends them along with the manifest. A push is an
upsert: anything already defined on the server that is missing from your file is kept and reported, never
deleted — removing an achievement is a separate, explicit step, because it disappears from every player
who earned it.

### achievements.json

```json
[
  {"key": "first_race",  "name": "First Race",  "description": "Finish a race.",         "points": 10},
  {"key": "speed_demon", "name": "Speed Demon", "description": "Win at over 200 km/h.",  "points": 50, "hidden": true},
  {"key": "collector",   "name": "Collector",   "description": "Own every car.",         "points": 75, "max_progress": 12}
]
```

| field          |                                                                                                   |
|----------------|---------------------------------------------------------------------------------------------------|
| `key`          | lowercase `[a-z0-9_]`, 1–64 chars; what the game passes to `aeon_achievement_unlock()`. Never rename a shipped key — unlocks are stored against it. |
| `name`         | 1–80 chars, shown on the player's profile                                                         |
| `description`  | up to 400 chars                                                                                   |
| `points`       | 0–1000                                                                                            |
| `hidden`       | `true` shows it as a hidden achievement until unlocked (default `false`)                          |
| `max_progress` | optional; for achievements reported through `aeon_achievement_progress()` — unlocks on reaching it |

Up to 200 per title.

### leaderboards.json

```json
[
  {"category": "lap_time", "name": "Best Lap",   "sort": "asc", "format": "time_ms"},
  {"category": "score",    "name": "High Score"}
]
```

| field      |                                                                                        |
|------------|----------------------------------------------------------------------------------------|
| `category` | lowercase `[a-z0-9_]`, 1–64 chars; what the game passes to `aeon_leaderboard_submit()` |
| `name`     | 1–80 chars                                                                             |
| `sort`     | `desc` = higher is better (default); `asc` = lower is better, for times                |
| `format`   | `number` (default), `time_ms`, `distance_m` — how the shell renders the value          |

Up to 50 per title. A player keeps one best entry per category, and "best" follows `sort`. Submitting to a
category you never defined still works and ranks higher-is-better.

### Pushing definitions without a build

Signed in as the developer account that owns the title:

    GET    /studio/dev/titles/{sku}/achievements
    PUT    /studio/dev/titles/{sku}/achievements          body: the achievements.json array
    DELETE /studio/dev/titles/{sku}/achievements/{key}    409 with a count if players hold it; add ?confirm=yes
    GET    /studio/dev/titles/{sku}/leaderboards
    PUT    /studio/dev/titles/{sku}/leaderboards          body: the leaderboards.json array
    DELETE /studio/dev/titles/{sku}/leaderboards/{category}   scores are kept

The first push creates the title's store record (unpublished) if the listing doesn't exist yet, so
achievements can be set up well before launch.
