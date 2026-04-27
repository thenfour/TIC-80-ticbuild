# Features in this fork

- Remoting server
- Frame timing and remoting display in window title and HUD (access via <kbd>ALT+0</kbd>)

# Perf HUD color scheme and thresholds

HUD colors are configured with these optional command line args:

- `--hud-palette-outline`
- `--hud-palette-text`
- `--hud-palette-ok`
- `--hud-palette-warning`
- `--hud-palette-alert`

Each value can be:

- `auto` (use built-in target color matching against current frame palette)
- a palette index (`0` to `15`)
- a hex color token without `#`, in either `rgb` or `rrggbb` form (`f80`, `ff8800`)

`auto` default targets are:

- outline: black
- text: white
- ok: green
- warning: orange/yellow
- alert: red

Threshold command line args:

- `--thresh-fps-warn` (fps)
- `--thresh-fps-alert` (fps)
- `--thresh-mem-warn` (kb)
- `--thresh-mem-alert` (kb)
- `--thresh-cycles-warn` (kcycles)
- `--thresh-cycles-alert` (kcycles)

Default thresholds:

- FPS: warning `<57`, alert `<53`
- MEM: warning `>102400` kb, alert `>256000` kb
- cycles (TIC/SCN+BDR): warning `>1800` kcycles, alert `>2400` kcycles

Threshold state is checked as alert first, then warning, then OK. For FPS (lower is
worse), comparisons are inverted.

Graph coloring is evaluated per rendered x-column and value text is colored by the
same severity. Custom perf metrics always use OK color.

# remoting support for ticbuild

[ticbuild](https://github.com/thenfour/ticbuild) is a build system for TIC-80 which
supports watching cart dependencies for live updates.

In order to make that work, we need to add some functionality to TIC-80 for remote control.

We add a command line arg:

`tic80.exe --remoting-port=9977`

While TIC-80 is running, it will listen on this port for remote commands. Always
binds to `127.0.0.1`. Up to 10 clients supported.

# Protocol

- Line-based human readable (terminal-friendly)
- dead-simple, no optional args or multiple datatypes if possible.
- requests
  - each line in the form `<id> <command> <args...>`
    - `id` is an id used to pair responses with requests
    - example: `1 sync 24`
    - example: `1 poke 0x8fff <24 ff c0>`
      - this `<xx ...>` syntax allows representing binary data in hex byte form
      - multiple args are separated by whitespace.
    - example: `1 eval "trace(\"hello from remote\")"`
      - quotes wrap an arg that contains whitespace. Escape char is `\`
        - `\\` = `\`
        - `\"` = `"`
    - commands are not case-sensitive. `sync` and `SYNC` and `SyNc` are equivalent.
    - whitespace is forgiving. `1   sync    24` (or tabs) is the same as `1 sync 24`.
    - trailing whitespace is trimmed/ignored
    - non-ASCII chars are considered an error.
    - named args not supported (yet)
  - commands supported:
    - `hello` - returns a description of the system (TIC-80 remoting v1)
    - `load <cart_path.tic> <run:1|0>`, e.g. `load "c:\\xyz.tic" 1`.
      If the run flag is `0`, the cart is just loaded. If `1`, the cart is
      launched after successful load.
    - `ping` - returns data `PONG`
    - `sync <flags>` - returns nothing (syncs cart & runtime memory; see tic80 docs)
    - `poke <addr> <data>` - returns nothing
    - `peek <addr> <size>`
      - returns the binary result, e.g., `<c0 a7 ff 00>`.
      - size is required even if it's only 1.
    - `restart`
    - `quit`
    - `eval <code>` - no return possible (`tic_script.eval` has `void` return type).
      you could just make the script do something visible, like `poke()`.
      - TODO: enable trace output to remote? or support return data?
    - `evalexpr <expression>` - returns the result of the given single expression.
      It's effectively your expression with `return` prepended, allowing syntax like `1+3`
      or `width * size`
      without having to type `return width * size`. That can cause issues if you need
      to execute a lot of code, but always workaroundable with something like,
      `evelexpr "(function() ... end)()"`.
    - `listglobals` - returns a single-line, comma-separated list of eval-able
      global symbols (identifier keys from the Lua global environment).
    - `typeschema <symbol>` - returns a type schema of the specified global symbol. see below.
    - `getfps` - gets current FPS
    - `cartpath` - returns the full path to the currently open cartridge.
      empty string if there's no open cart.
    - `fs` - returns the current filesystem local path (the one you can control via command line `--fs=...`)
    - `perf` - returns current live performance metrics. see below for response
    - `metadata <key>` - returns the value for the metadata value in code.
      See: https://github.com/nesbox/TIC-80/wiki/Cartridge-Metadata.
  - datatypes
    - numbers
      - Only integers for the moment. No fancy `1e3` forms, just:
      - decimal: `1` `0` `24` `1000`
      - hex `0xff`
    - strings
      - always require double quotes, ASCII-only, escape char is `\`.
    - binary, enclosed in `<` and `>`.
      - example: `<ff 22 00>`
      - string syntax: always hexadecimal.
      - whitespace is ignored so `<ff2200>` or `<f f220 0>` are equivalent to `<ff 22 00>`
- response
  - datatypes follow same convention as requests
  - `<id> <status> <data...>`
    - `id` is the same ID as the request. no checking is done on this, you can
      send the same id always and the server doesn't care.
    - `status` is either `OK` or `ERR`
    - data is defined by the command, but is similar to the request args.
      - `1 ping` => `1 OK PONG`
      - `44 sync 24` => `44 OK`
      - `xx` => `0 ERR "error description here"`
  - (not currently needed; theoretical) events: `@ <eventtype> <data...>`
    - server can send event messages to the client using similar format, but the
      message id is `@`. Datatype semantics remain. Examples:
      - `@ trace "hello from tic80"`
      - (this is the only one supported so far)
- Commands to be queued and executed at a deterministic safe point in the
  TIC-80 system loop (e.g., between frames if the cart is running)

# `perf` command

returns a single-line, comma-separated, `key=value` pairs.

## keys

- `client_count` integer; number of clients connected to remoting
- `fps` current capped FPS from the rolling window tracker, floating-point (e.g. `59.95`, `60`)
- `fps_uncapped` estimated uncapped FPS derived from `total_ms`, integral.
- `tic_ms` time spent in TIC, in milliseconds quantized to `0.1ms`.
- `scn_ms`
- `bdr_ms`
- `total_ms` total time spent in TIC+SCN+BDR, in milliseconds quantized to `0.1ms`.
- `tic_cycles` number of Lua VM cycles spent in TIC. integral.
- `scn_cycles`
- `bdr_cycles`
- `total_cycles`
- `lua_gc_mem` - lua's gc memory usage, in bytes (integral)

Timing values are measured as fixed-point `ms10` internally, so all `*_ms` values
are rounded to the nearest `0.1ms` before being exposed. `fps_uncapped` is
derived from that quantized `total_ms` value, so it is approximate and becomes
coarser at very high frame rates.

# Discovery Protocol

When the remoting server is listening, we will make the server discoverable by
placing a json file on the filesystem.

If the file already exists, it shall be overwritten.

The file is to be deleted when the server stops listening.

The global file will be placed in `%LOCALAPPDATA%\TIC-80\remoting\sessions\` and
the file is to be named `tic80-remote.<pid>.json`. Its contents will look like,

```json
{
  "pid": 1234,
  "host": "127.0.0.1",
  "port": 51000,
  "startedAt": "2026-01-31T19:50:26.859Z",
  "remotingVersion": "v1"
}
```

- `remotingVersion` is the same as in the `hello` command.

We also must support a new command line arg, for specifying where to output the
remote session file.

`tic80.exe --remote-session-location=c:\my\folder`

will write the discovery file as `c:\my\folder\tic80-remote.<pid>.json`

The global discovery location (under `%LOCALAPPDATA%`) is written unless disabled
with the `--global-disco=OFF|ON` command line arg. It is `ON` by default. The
user-specified `--remote-session-location` file is always written regardless of
the global flag.

example:

`tic80.exe --remote-session-location=c:\my\folder --global-disco=OFF`

writes the remote session file to `c:\my\folder`

# `typeschema <symbol>`

Returns the type schema of the specified global symbol. For simple values it's
the same as Lua's keyword `type(x)`. So it can return:

- `string`
- `number`
- `function`
- `boolean`
- `nil`

However for tables

- `table`

# code structure

changes to existing "official" TIC-80 code to be surgical and minimal. put our own
sources under `/src/ticbuild_remoting`.
