# amidiminder
ALSA utility to keep your MIDI devices connected

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


## Building

Prerequisites:
  * g++, 6 or later
  * make
  * libasound2-dev

```sh
sudo apt install g++ make libasound2-dev
```

Clone this repo and run `make`:

```sh
git clone https://github.com/mzero/amidiminder.git
cd amidiminder
make
```

Outputs:

 - build executable is in `build/amidiminder`.
 - deb package is in `build/amidiminder.deb`.


## Install

Installing the built deb package will install a systemd service that runs
`amidiminder` at startup.

```sh
sudo dpkg -i build/amidiminder.deb
```

That's it. — It's installed. — It's running — You're done!

## Configuration

`amidiminder` reads a rules file from `/etc/amidiminder.rules`. If you edit
that file, you can first check it is legal:

```sh
amidiminder -C  # checks the rules and then quits
```

Then, restart the service:

```sh
sudo systemctl restart amidiminder
```

### Rules Format

Example:
```
nanoKEY2 --> Circuit
  # connects the first port for each device as shown

bicycle <-- Launchpad Pro MK3
bicycle <-- Launchpad Pro MK3:2
bicycle:synths --> Circuit
  # the port direction can go which ever way is convienent for you
  # ports can be specified by number or by name
```

---


## Credits & Thanks

### Related work

[amidiauto](https://github.com/BlokasLabs/amidiauto) by Blokas Labs.

### Open source code used

https://github.com/CLIUtils/CLI11
CLI11 1.8 Copyright (c) 2017-2019 University of Cincinnati, developed by Henry
Schreiner under NSF AWARD 1414736. All rights reserved.
