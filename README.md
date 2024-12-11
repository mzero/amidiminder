# midiminder & midiwala
**midiminder:** an ALSA utility to keep your MIDI devices connected \
**midiwala:** a tool for managing connections

### The problem

  * Using `aconnect` to reconnect your devices and your software gets old quick.
  * If you power cycle a synth... you've got to `aconnect` it again.
  * Bandmate "helps" by unplugging a USB cord to untangle it and plugs it in again:
    Now your controller is disconnected.

### The solution

`midiminder` takes care of this:

* It tracks what connections were made between ports, and automatically
  reestablishes those connections when ports reattach to the system.
* You can specify a profile of things to connect and then tell `midiminder` to
  reconfigure your system to that profile in one command.
* The whole desired connection state is persistent, even across system reboots,
  so that the configuration is quickly restored once devices power back up and
  applications are restarted.

### A bonus

`midiwala` provides an interactive terminal interface for looking at, and
modifying the connections on your system. Try it... you won't regret it!

  ```console
  $ midiwala
  ┌─ Ports ───────────────────────────────────────
  │
  │    MicroMonsta 2      : MIDI 1     [ 32:0] <->
  │    Midi Through       : Port-0     [ 14:0] <->
  │    Midihub MH-1Z109TZ : MIDI 1     [ 36:0] <->
  │    Midihub MH-1Z109TZ : MIDI 2     [ 36:1] <->
  │    Midihub MH-1Z109TZ : MIDI 3     [ 36:2] <->
  │    Midihub MH-1Z109TZ : MIDI 4     [ 36:3] <->
  │    Pure Data          : Midi-In 1  [130:0] <--
  │    Pure Data          : Midi-Out 1 [130:1] -->

  ┌─ Connections ─────────────────────────────────────────────────────────────────────
  │
  │    Midihub MH-1Z109TZ:MIDI 1 [36:0]+ --> Pure Data:Midi-In 1 [130:0]+
  │    Pure Data:Midi-Out 1 [130:1]+ --> MicroMonsta 2:MIDI 1 [32:0]+

    >> Q)uit, C)onnect, D)isconnect, U)ndo
  ```

It also includes all the command line functionality of `aconnect` (list, connect,
disconnect), but with very readable output, and improved syntax for specifying
ports.

# Next Steps

**Installing:**
1. Go to the [Releases](https://github.com/mzero/amidiminder/releases) page
2. Download the appropriate `.deb` file for your system
3. Install it with `apt`:
  ```console
  $ sudo apt install ./amidiminder*.deb
  ```

**User Guide:** see `USER-GUIDE.md`

**Building:** See `BUILDING.md`

---

### Authors

Mark Lentczner - https://github.com/mzero \
John Horigan - https://github.com/MtnViewJohn

### Thanks

Blokas Labs https://github.com/BlokasLabs \
for their work on Patchbox OS, great HW, and pushing us to make this tool
even better.

### Open source code used

https://github.com/CLIUtils/CLI11
CLI11 2.4.2 Copyright (c) 2017-2024 University of Cincinnati, developed by Henry
Schreiner under NSF AWARD 1414736. All rights reserved.
