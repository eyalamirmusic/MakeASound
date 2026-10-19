# Plugin scanning

How a host finds out what plugins are installed, without letting one of them take
it down. The code is in `Lib/MakeASound/Hosting/` (target `MakeASoundHosting`);
`Apps/PluginScanner` is a console front end to it.

## The shape of it

Scanning a plugin means loading it, and loading a plugin runs code nobody here
wrote. Some of it crashes, some hangs, some prints megabytes to stdout. So exactly
one thing happens out of process: **loading one plugin**. Everything around it
runs in the host's own process, as ordinary objects with callbacks:

| piece | what it does | where |
| --- | --- | --- |
| `FormatManager` | the formats this host knows (VST3; AU on macOS), how a target is claimed, stamped and scanned | host |
| `ScanSession` | one sweep: discover, plan against the cache, scan what changed, save | host |
| `ScanCoordinator` | the worker pool: N children at a time, a timeout each, every failure turned into a result | host |
| `PluginCache` | the database, its blacklist, and the JSON file | host |
| `runScanWorkerIfRequested` | the worker: load one target, print one line, exit | child |

Nothing has a thread of its own. `poll()` advances a sweep; a console program calls
it in a loop, a GUI calls `setAutoPoll(true)` and an eacp timer calls it on the
message thread. Every callback fires on the thread that polls.

```cpp
auto formats = MakeASound::Hosting::FormatManager();
formats.addDefaultFormats();

auto options = MakeASound::Hosting::ScanSessionOptions();
options.cachePath = MakeASound::Hosting::defaultPluginCachePath("My Host");

auto session = MakeASound::Hosting::ScanSession(formats, options);
session.onTargetFinished = [](int done, int total, const ModuleScan& scan) { ... };
session.onFinished = [](const ScanSessionSummary& summary) { ... };
session.setAutoPoll(true);
session.start();
```

`ScanMode::InProcess` runs the same scans on a background thread of the host, with
no isolation at all. It exists for tests and for stepping through a format in a
debugger.

## Two worker topologies

**The host relaunches itself** (the default, `defaultWorkerCommand()`). Make
`runScanWorkerIfRequested` the first thing `main` does after the event loop is up.
Both it and `requestedScanTarget` read the command line eacp stored, so `main`
hands argc/argv over once and never again (`Apps/PluginHost/Main.cpp`):

```cpp
int runScanWorker()
{
    auto formats = FormatManager();
    formats.addDefaultFormats();

    return runScanWorkerIfRequested(formats).value_or(0);
}

int main(int argc, char** argv)
{
    eacp::Apps::setCommandLineArgs(argc, argv);

    if (requestedScanTarget())
        return eacp::Apps::runConsole(runScanWorker);

    return eacp::Apps::run<MyApp>(); // ...the app as usual
}
```

The `(argc, argv)` overloads of both remain for a `main` that does not store its
command line with eacp.

One binary, nothing extra to ship. The worker needs a running event loop: VST3
module initialisation expects one, and an AUv3 answers `AudioComponentInstantiate`
through it.

**A separate scanner executable** (`workerCommandFor(path)`). Ship
`Apps/PluginScanner`, or your own build of it, next to the app and point the
session at it. Worth it when the host is large or slow to start, when it should not
be relaunched dozens of times, or when the scanner must be updated separately. The
scanner answers the same flag, so the host's side does not change. The two must
ship together: a host only reads its own protocol (below).

## The protocol

The host runs `<executable> <argumentPrefix...> <target>`, where the prefix ends in
`--makeasound-scan`. A target is a `.vst3` path, or an Audio Unit identifier
`type:subtype:manufacturer` written as three four-character codes
(`aufx:dely:appl`). The format that claims the target's shape scans it.

The worker answers with **one line on stdout**: the marker `MAKEASOUND_SCAN:`
followed by the `ModuleScan` as compact JSON, on a line of its own and flushed. The
host takes the **last** such line in the output and ignores everything around it,
so a plugin's own printing is harmless. The marker is one named constant,
`scanResultPrefix`. There is no IPC channel: a worker that crashes simply leaves no
line.

Exit codes (`ScanWorkerExit`):

| code | meaning |
| --- | --- |
| 0 | answered. A plugin that refused to load is an answer: a `ModuleScan` with `error` set |
| 2 | `--makeasound-scan` with no target |
| 3 | nothing is installed under that name |
| 4 | no format in this build claims the target |

Any non-zero exit, a worker still running at the timeout (default 30 s, then
killed), and output with no decodable result line all become a result with
`crashed = true`, carrying the target's stamp and the last 400 characters of the
worker's stderr in `error`. The pool defaults to the hardware concurrency, clamped
to 1..8.

## Stamps and the blacklist

Every entry carries a stamp, and the cache compares it with the target's current
one:

- **VST3:** the newest mtime and the total size of the binaries in the bundle's
  architecture folders (`Contents/MacOS`, `Contents/<arch>-win`), not the bundle
  folder, whose mtime does not move when the binary is replaced, and not its
  resources, which can be gigabytes. A bare `.vst3` file stamps as itself.
- **Audio Unit:** the component's version, with a size of zero. A component has no
  file to stat.

A target whose stamp matches is not loaded again, **whether its entry succeeded or
failed**: that is the blacklist. A crashed or failed plugin stays out until it
changes (it is updated, reinstalled or rebuilt). `ScanSessionOptions::forceRescan`
(`--force`) deletes the database before scanning anything, so an interrupted forced
sweep leaves no stale file.

Stamping goes through the format, so a cache that holds Audio Units must know the
formats: a `ScanSession` does this itself, and a host that only reads a database
calls `useFormatStamps(cache, formats)`. Stamped as a file, every cached Audio Unit
would look stale forever and a crashed one would never stay blacklisted.

A sweep drops entries it did not find, **only for the formats it swept**: a
VST3-only sweep (`ScanSessionOptions::formats`, `--format vst3`) leaves cached
Audio Units alone.

## The database

`PluginCacheDocument` is written as JSON (Miro, two-space indent, sorted keys):

```json
{
  "modules": [
    {
      "classes": [
        {
          "category": "Audio Module Class",
          "classId": "4541504D4761696E5653543300000000",
          "format": "VST3",
          "hasEditor": true,
          "hasMidiInput": true,
          "name": "Gain",
          "numAudioInputs": 2,
          "numAudioOutputs": 2,
          "path": "/Library/Audio/Plug-Ins/VST3/Gain.vst3",
          "subCategories": "Fx",
          "vendor": "TamberPlug",
          "version": "1.0.0"
        }
      ],
      "crashed": false,
      "error": "",
      "fileSize": 5081712,
      "format": "VST3",
      "lastModified": 1791931959,
      "path": "/Library/Audio/Plug-Ins/VST3/Gain.vst3"
    }
  ],
  "searchPaths": ["/Library/Audio/Plug-Ins/VST3"],
  "version": 1
}
```

- `format` is the `PluginFormat` enumerator's name, `"VST3"` or `"AU"`.
- `classId` is the VST3 class UID as 32 hex digits, or for an Audio Unit the same
  identifier as `path`. Identity is `path` plus `classId`, never a position.
- `lastModified` counts seconds from the platform's filesystem clock, not the Unix
  epoch: compare stamps, never show one as a date.
- An entry with no `format` (an older file) reads as VST3.

**The schema is a compatibility promise.** It is Tamber Plug's, field for field: a
database Plug's scanner wrote loads here and is written back byte for byte
(`Tests/HostingFixtures.h` pins it), and Plug reads one written here. Fields are
only ever added, with a default that reads old files correctly; `version` changes
only if a field changes meaning. A missing or corrupt file reads as empty, so the
worst case is a full rescan.

Where it lives is the host's choice. `defaultPluginCachePath(appName, company)` is
`Plugins.json` in the app's own application-data folder
(`~/Library/Application Support/<company>/<app>`, `%APPDATA%\<company>\<app>`),
and the no-argument form uses this app's names. Hosts that share a scanner and a
database, a vendor's family of apps say, pass one path to all of them.

## Shipping a scanner

- **Sign it like the app.** A scanner binary is code your users run; a notarized app
  must not carry an unsigned helper.
- **macOS hardened runtime:** a process with the hardened runtime refuses to load
  libraries not signed by the same team. A scanner (and a host that loads plugins in
  process) needs the `com.apple.security.cs.disable-library-validation`
  entitlement, or every third-party plugin fails to load. Sandboxed apps need their
  plugin folders and the AU registry reachable as well.
- **No Dock tile:** a bare macOS executable has no bundle to say `LSUIElement`, so
  `Apps/PluginScanner` embeds an Info.plist in `__TEXT,__info_plist`. Otherwise
  every worker flashes a Dock icon.
- **No console windows on Windows:** workers are spawned with `noWindow`, so a
  console-subsystem scanner launched from a GUI host shows nothing. A worker also
  turns off Windows' fault dialogs and `abort()`'s message box before it loads the
  plugin, so a crash ends the worker at once instead of waiting out the timeout
  behind a prompt.
- **Seal the exports:** `makeasound_enable_hosting(<target>)` links the library and
  stops an executable exporting its C++ weak symbols on macOS, so a loaded plugin
  never binds to the host's copies of inline functions (see
  [Hosting.md](Hosting.md)).

## Writing a custom front end

`Apps/PluginScanner/Main.cpp` is the whole recipe, in under two hundred lines: build a
`FormatManager`, answer `runScanWorkerIfRequested` first, parse the command line
into `ScanSessionOptions`, print what `onStarted`, `onTargetFinished` and
`onFinished` report, and pump `poll()`.

The command line is `addScanOptions(parser, options)` (`Scan/ScanArgs.h`), which
declares the scanner's options on an `eacp::Args::Parser`, each bound straight onto
a `ScanSessionOptions` field: `--output` (`cachePath`), `--folder`
(`searchFolders`, repeatable), `--no-default-folders`, `--format VST3|AU`
(repeatable), `--force`, `--jobs` (at least 1) and `--timeout` in milliseconds (at
least 0). The parser handles `--help` and reports a bad argument as exit code 2;
the worker check still runs before it, so a worker's command line never reaches
it. The caller includes `<eacp/Core/App/Args.h>` and links `eacp-core`. A GUI does the same with `setAutoPoll(true)`, reads
`session.cache()` as results land to fill its list, and calls `cancel()` from a
button: what was scanned is still saved, and `onFinished` fires with `cancelled`
set. Keep stdout empty outside worker mode; it is the protocol's.
