# amidiminder & amidiview
**amidiminder:** an ALSA utility to keep your MIDI devices connected
**amidiview:** a tool for managing connections

### The problem

  * Using `aconnect` to reconnect your devices and your software gets old quick.
  * If you power cycle a synth... you've got to `aconnect` it again.
  * Bandmate "helps" by unplugging a USB cord to untanlge and plugs it in again:
    Now you're controller is disconnected.

### The solution

`amidiminder` is a program that runs as a service. It will:

  1. Reads a rules file of connections you'd like made.

  2. When the MIDI devices & software - `amidimider` will
     automatically connect them according to the rules.

  3. It also Watches any connections you make with `aconnect` or that your
     software makes, and remembers them too.

  4. If any MIDI port goes away (power cycle, pulled USB cord, etc..), ALSA
     will silentlly remove the connections...
     ... But when the MIDI port comes back - `amidimider`'s got your back and
     will connect them right back up again just as they were.

### A bonus

`amidiview` provides an interactive terminal interface for looking at, and
modifying the connections on your system.  Try it... you won't regret it!

```sh
& amidiview
```

Further, it includes all the functionality of `aconnect` (list, connect,
disconnect), but allowing you to use `amidiminder`'s easy syntax for
specifying ports. Often you can just use a part of the device name:

```sh
& amidiview list
Ports:
    MicroMonsta 2      : MIDI 1     [ 32:0] <->
    Midi Through       : Port-0     [ 14:0] <->
    Midihub MH-1Z109TZ : MIDI 1     [ 36:0] <->
    Midihub MH-1Z109TZ : MIDI 2     [ 36:1] <->
    Midihub MH-1Z109TZ : MIDI 3     [ 36:2] <->
    Midihub MH-1Z109TZ : MIDI 4     [ 36:3] <->
    Pure Data          : Midi-In 1  [130:0] <--
    Pure Data          : Midi-Out 1 [130:1] -->
Connections:

& amidiview connect Pure Monsta
Connected Pure Data:Midi-Out 1 [130:1]+ --> MicroMonsta 2:MIDI 1 [32:0]+

& amidiview connect hub:2 Pure
Connected Midihub MH-1Z109TZ:MIDI 2 [36:1] --> Pure Data:Midi-In 1 [130:0]+

& amidiview list
Ports:
    MicroMonsta 2      : MIDI 1     [ 32:0] <->
    Midi Through       : Port-0     [ 14:0] <->
    Midihub MH-1Z109TZ : MIDI 1     [ 36:0] <->
    Midihub MH-1Z109TZ : MIDI 2     [ 36:1] <->
    Midihub MH-1Z109TZ : MIDI 3     [ 36:2] <->
    Midihub MH-1Z109TZ : MIDI 4     [ 36:3] <->
    Pure Data          : Midi-In 1  [130:0] <--
    Pure Data          : Midi-Out 1 [130:1] -->
Connections:
    Midihub MH-1Z109TZ:MIDI 2 [36:1] --> Pure Data:Midi-In 1 [130:0]+
    Pure Data:Midi-Out 1 [130:1]+ --> MicroMonsta 2:MIDI 1 [32:0]+
```


## Building

Prerequisites:
  * g++, 8 or later
  * make
  * libasound2-dev
  * libfmt-dev

```sh
sudo apt install g++ make libasound2-dev libfmt-dev
```

Clone this repo and run `make`:

```sh
git clone https://github.com/mzero/amidiminder.git
cd amidiminder
make
make deb
```

Outputs:

 - build executables: `build/amidiminder` & `build/amidiview`.
 - deb package, which is placed in the directory above.


## Install

Installing the built deb package will install a systemd service that runs
`amidiminder` at startup.

```sh
sudo apt install ../amidiminder_*.deb
```

That's it. — It's installed. — It's running — You're done!

## Configuration


---


## Credits & Thanks

### Related work

[amidiauto](https://github.com/BlokasLabs/amidiauto) by Blokas Labs.

### Open source code used

https://github.com/CLIUtils/CLI11
CLI11 2.4.2 Copyright (c) 2017-2024 University of Cincinnati, developed by Henry
Schreiner under NSF AWARD 1414736. All rights reserved.
