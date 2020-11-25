# amidiminder
ALSA utility to keep your MIDI devices connected

### The problem

* You connect some USB based MIDI devices to your computer, and then use `aconnect` or other software to connect the ports between devices, and from devices to software. It all plays great.

* If a USB device disconnects (you trip over the cord, you need to power cycle it, you decide to tidy the wiring and pull the wrong thing....) - when you reconnect the device, you'll need to make the connections again. This is awkward if you are in the middle of a live set.

### The solution

Just leave this program running:

```sh
$ ./build/amidiminder &  # just leave it in the background...

# make some connections
$ aconnect nanoKEY2:0 Circuit:0
$ aconnect nanoKONTROL:0 Circuit:0
$ aconect Circuit:0 pisound:0
adding connection nanoKEY2:nanoKEY2 MIDI 1 [32:0] ==>> Circuit:Circuit MIDI 1 [24:0]
adding connection nanoKONTROL:nanoKONTROL MIDI 1 [28:0] ==>> Circuit:Circuit MIDI 1 [24:0]
adding connection Circuit:Circuit MIDI 1 [24:0] ==>> pisound:pisound MIDI PS-3DJNWEF [20:0]

# oops, unplugged the Circuit...
connection deactivated: nanoKEY2:nanoKEY2 MIDI 1 [32:0] ==>> Circuit:Circuit MIDI 1 [--]
connection deactivated: nanoKONTROL:nanoKONTROL MIDI 1 [28:0] ==>> Circuit:Circuit MIDI 1 [--]
connection deactivated: Circuit:Circuit MIDI 1 [--] ==>> pisound:pisound MIDI PS-3DJNWEF [20:0]

# plugged it back in again!
connection re-activated: nanoKEY2:nanoKEY2 MIDI 1 [32:0] ==>> Circuit:Circuit MIDI 1 [24:0]
connection re-activated: nanoKONTROL:nanoKONTROL MIDI 1 [28:0] ==>> Circuit:Circuit MIDI 1 [24:0]
connection re-activated: Circuit:Circuit MIDI 1 [24:0] ==>> pisound:pisound MIDI PS-3DJNWEF [20:0]
```

### Details

Pretty straight forward...
* Scans connections that exist as it starts up, so you can launch it after you make the connections or before
* Remembers devices and ports by name, so if you plug things back in a different order (so they get different client numbers), it'll still work as you'd expect.

### Building

Get this repo and run `make`.

That's it.

 - build executable is in `build/amidiminder`.
 - deb package is in `build/amidiminder.deb`.

If you install the deb package, it'll install a systemd sevice as well, and start it.

### Install

Run

  sudo dpkg -i build/amidiminder.deb

That's it.
It's installed.
It's running.
You're done!



