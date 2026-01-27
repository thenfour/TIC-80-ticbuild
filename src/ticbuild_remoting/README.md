# remoting support for ticbuild

[ticbuild](https://github.com/thenfour/ticbuild) is a build system for TIC-80 which
supports watching cart dependencies for live updates.

In order to make that work, we need to add some functionality to TIC-80 for remote control.

We add a command line arg:

`tic80.exe --remoting-port=9977`

While TIC-80 is running, it will listen on this port for remote commands. Always
binds to `127.0.0.1`. Single connection supported for simplicity.

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
- Commands to be queued and executed at a deterministic safe point in the
  TIC-80 system loop (e.g., between frames if the cart is running)

# code structure

changes to existing "official" TIC-80 code to be surgical and minimal. put our own
sources under `/src/ticbuild_remoting`.
