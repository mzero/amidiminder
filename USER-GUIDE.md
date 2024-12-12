# Using midiminder & midiwala

There are three components to this package:

* `midiminder.service` -  a systemd service
  keeping track of connections and ports.
* `midiminder` command for managing profiles and the service
* `midiwala` command for managing connections, similar to `aconnect`

**Contents**
- [Getting started](#getting-started)
- [The service](#the-service)
- [Intermission](#intermission)
- [Profiles](#profiles)
  - [An example](#an-example)
  - [Sample profiles](#sample-profiles)
- [More Info](#more-info)
- [Migrating from older versions](#migrating-from-older-versions)

## Getting started

If you've just installed the package... you're ready to go! There is no
configuration needed.

Get a listing of what is going on in your system:

  ```console
  $ midiwala list
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
      MicroMonsta 2:MIDI 1 [32:0]+ --> Pure Data:Midi-In 1 [130:0]+
      Pure Data:Midi-Out 1 [130:1]+ --> MicroMonsta 2:MIDI 1 [32:0]+
  ```

You can get an interactive view of the same information, with the ability
to make and break connections:

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
  │    MicroMonsta 2:MIDI 1 [32:0]+ --> Pure Data:Midi-In 1 [130:0]+
  │    Pure Data:Midi-Out 1 [130:1]+ --> MicroMonsta 2:MIDI 1 [32:0]+

  >> Q)uit, C)onnect, D)isconnect
  ```

It's easy to use, just follow the prompts at the bottom of the screen.

> *Note: You need to be using a terminal window that supports XTERM style controls.*
> *Don't worry, pretty much everything these days does, so this should just work.*

At this point, you can just connect and disconnect ports as you need with
`midiwala`.

> **Pro tip:** *If you leave a terminal window open, running `midiwala`, it will*
> *dynamically update as you plug in devices, start software, and make*
> *connections.*

## The service
First thing is to make sure the service is running. You can check with:

  ```console
  $ midiminder status
  Daemon is running.
      1 profile rules.
      1 observed rules.
      8 active ports.
      2 active connections
  ```

You can make connections as you need, and
`midiminder` will remember them. If the devices get unplugged, then later
replugged, the service will reconnect them up for you.  This will even work
across reboots.

If it isn't running, then you should check with `systemctl` and possibly
enable and start it if needed:

  ```console
  $ systemctl status midiminder.service
  midiminder.service - ALSA MIDI minder daemon
      Loaded: loaded (/lib/systemd/system/midiminder.service; enabled; preset: enabled)
      Active: inactive (dead) since Mon 2024-12-09 13:18:27 PST; 5s ago
  ...

  $ sudo systemctl enable midiminder.service # if needed
  $ sudo systemctl start midiminder.service # if inactive
  ```

## Intermission

You don't need to learn anything more to use `midiwala` and have the
`midiminder` service keep track of connections, and re-make them for you
as your equipment gets plugged and unplugged, and on reboot.

Read on to learn how you can set up whole configurations, and set them
up in a single command.

## Profiles

The service remembers rules for what should be connected to what. It watches
the system and stores what it sees connecting (and disconnecting) in the
*observed rules*. You don't really have to think about these, but you can
see them if you like:

  ```console
  $ midiminder save -  # the dash means to save to stdout, not a file
  # Profile rules:

  # Observed rules:
  "Pure Data":"Midi-Out 1" --> "MicroMonsta 2":"MIDI 1"
  "MicroMonsta 2":"MIDI 1" --> "Pure Data":"Midi-In 1"
  ```

The service also follows a second set of rules that you can supply, called the
*profile rules*.  You write a set of rules in a file, then load them into
the service. Once loaded, they'll stay in force until you load another set,
even across system reboots.

### An example

Say your system has a **Launchpad** controller, a synth call **MicroMonsta**,
and you are running the software **Pure Data**:

  ```console
  $ midiwala list
  Ports:
      Launchpad Pro MK3 : LPProMK3 DAW  [ 32:2] <->
      Launchpad Pro MK3 : LPProMK3 DIN  [ 32:1] <->
      Launchpad Pro MK3 : LPProMK3 MIDI [ 32:0] <->
      MicroMonsta 2     : MIDI 1        [ 24:0] <->
      Midi Through      : Port-0        [ 14:0] <->
      Pure Data         : Midi-In 1     [130:0] <--
      Pure Data         : Midi-Out 1    [130:1] -->
  Connections:
      -- no connections --
  ```
Edit a file, say **live-pd-performance.rules**, to have:

  ```
  Pure Data <-- Launchpad
  Pure Data --> Monsta
  ```

If you load this profile into `midiminder`, then it will automatically
connect your devices this way when they are present:

  ```console
  $ midiminder load live-pd-performance.rules

  $ midiwala list
  Ports:
      Launchpad Pro MK3 : LPProMK3 DAW  [ 32:2] <->
      Launchpad Pro MK3 : LPProMK3 DIN  [ 32:1] <->
      Launchpad Pro MK3 : LPProMK3 MIDI [ 32:0] <->
      MicroMonsta 2     : MIDI 1        [ 24:0] <->
      Midi Through      : Port-0        [ 14:0] <->
      Pure Data         : Midi-In 1     [130:0] <--
      Pure Data         : Midi-Out 1    [130:1] -->
  Connections:
      Launchpad Pro MK3:LPProMK3 MIDI [32:0]+ --> Pure Data:Midi-In 1 [130:0]+
      Pure Data:Midi-Out 1 [130:1]+ --> MicroMonsta 2:MIDI 1 [24:0]+
  ```

Notice that the rules are simple:

- You can use any sensible portion of the device name you like
- The rules can go in either direction, left to right or right to left
- If you don't specify a port, the first sensible port on
  a device will be used

----

If you want to also hook up the DIN port of the Launchpad, you could type

  ```console
  $ midiwala connect Launchpad:DIN Pure
  Connected Launchpad Pro MK3:LPProMK3 DIN [32:1] --> Pure Data:Midi-In 1 [130:0]+
  ```

Notice:

  - On the command line, the connection is always left to right
  - Any sensible part of the port name is fine
  - Because it's a command line, we only typed `Pure`, if we wanted to use
    the full name we'd have to have quoted it: `"Pure Data"`.
  - It'll only be able to connect if the devices are present
  - You could have used the interactive mode of `midiwala` and connected it
    that way with even less typing.

You can look at the server's rules now:

  ```console
  $ midiminder save -
  # Profile rules:
  Pure Data <-- Launchpad
  Pure Data --> Monsta

  # Observed rules:
  "Launchpad Pro MK3":"LPProMK3 DIN" --> "Pure Data":"Midi-In 1"
  ```

The connection made by hand (via `midiwala`) was observed and added to the
rules.  This will work if even if you used other software (such as `aconnect`
or a DAW) to make the connection.

If you reload the profile, or tell `midiminder` to reset, then
the observed rules are dropped, and the rules of the loaded profile are used
to make connections.

We could add this rule to our file:

  ```
  Pure Data <-- Launchpad
  Pure Data <-- Launchpad:DIN
  Pure Data --> Monsta
  ```

And load the file again:

  ```console
  $ midiminder load live-pd-performance.rules

  $ midiwala list --connections
  Connections:
      Launchpad Pro MK3:LPProMK3 DIN [32:1] --> Pure Data:Midi-In 1 [130:0]+
      Launchpad Pro MK3:LPProMK3 MIDI [32:0]+ --> Pure Data:Midi-In 1 [130:0]+
      Pure Data:Midi-Out 1 [130:1]+ --> MicroMonsta 2:MIDI 1 [24:0]+

  $ midiminder save -
  # Profile rules:
  Pure Data <-- Launchpad
  Pure Data <-- Launchpad:DIN
  Pure Data --> Monsta
  ```
### Sample profiles

There are a few sample profiles installed with the system that you look at,
and start with to modify and make your own. The files reside in:

     /usr/share/doc/midiminder/examples/

* **example.rules** - examples of different kinds of rules and their syntax
* **generic.rules** - connects all hardware ports to all software ports
* **looper.rules** - an example of a profile for a Mark's live performance setup


## More Info

The man pages have more detailed information:

* midiwala.1
* midiminder.1
* midiminder-profile.5
* midiminder-daemon.8


## Migrating from older versions

Prior to 0.80 release, `amidiminder` (as the service was named then) simply
read the profile at the fixed location `/etc/amidiminder.rules`. If you have
custom rules there, you should:

1. Move that file someplace within your home directory.
2. Edit it if needed (see below)
3. Load it with `midiminder load <your-file>`

There is one breaking rule syntax change. Specifying a port by ALSA port id
has changed:

Consider this set of ports:

  ```console
  $ midiwala list --ports
  Ports:
      Midi Through       : Port-0     [ 14:0] <->
      Midihub MH-1Z109TZ : MIDI 1     [ 36:0] <->
      Midihub MH-1Z109TZ : MIDI 2     [ 36:1] <->
      Midihub MH-1Z109TZ : MIDI 3     [ 36:2] <->
      Midihub MH-1Z109TZ : MIDI 4     [ 36:3] <->
      Pure Data          : Midi-In 1  [130:0] <--
      Pure Data          : Midi-Out 1 [130:1] -->
  ```


**Old syntax:** *(prior to 0.90)*

> `Midihub:2` — meant the port with id 2, which is the *third* port

**New syntax:** *(0.90 and later)*

> `Midihub:2` - means the port with '2' in it's name, which for Midihub will be the second port \
> `Midihub:=2` - means the port with id 2

If you had rules based on id numbers, add the `=` between the `:` and the number.

On the command line with `midiwala`, you can use id numbers for client and
port, just like `aconnect`. In this case, you don't use `=`:

    midiwala connect 36:1 130:0

As ALSA client id numbers are not stable, they aren't allowed in rule files.
