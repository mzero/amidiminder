# Building midiminder & midiwala

Prerequisites:
  * g++, 8 or later
  * make
  * libasound2-dev
  * libfmt-dev
  * debhelper *(if building the .deb)*

  ```sh
  sudo apt install g++ make libasound2-dev libfmt-dev
  ```

Clone this repo and run `make`:

  ```sh
  git clone https://github.com/mzero/midiminder.git
  cd midiminder
  make      # to build executables
  make deb  # to build the .deb file
  ```

Outputs:

 - build executables: `build/midiminder` & `build/midiwala`.
 - deb package, which is placed in the directory above.


## Install

Installing the built deb package will install a systemd service that runs
`midiminder` at startup.

  ```sh
  sudo apt install ../midiminder_*.deb
  ```

That's it. — It's installed. — It's running — You're done!

## Configuration

None!

## Testing

### Testing the rules parser

  ```console
  $ make
  ...
  $ ./build/midiminder check rules/test.rules
  Parsed 36 rule(s).
  ```

Look at the file `test/test.rules` to see how you can add test cases.

### Testing the observed rules logic

This must be done with separate environment so as not to overwrite the system's
idea of profile and observed rules.  There is a make target that creates this
environment for you:

  ```console
  $ make test-shell
  mkdir -p /tmp/midiminder-test/runtime
  mkdir -p /tmp/midiminder-test/state
  $SHELL --rcfile ./build/test-env || true
  $ ./build/midiminder connection-logic-test # hidden command!
  STATE_DIRECTORY=/tmp/midiminder-test/state
  RUNTIME_DIRECTORY=/tmp/midiminder-test/runtime
  --1-- connect empty/empty
  # profile rules
      --- empty ---
  # observed rules
      --- empty ---
  ** simulating connection 150:0 --> 200:0
  Observed connection: Controller:out [150:0]+ --> Synthesizer:in [200:0]+
      adding observed rule "Controller":"out" --> "Synthesizer":"in"
  # profile rules
      --- empty ---
  # observed rules
      "Controller":"out" --> "Synthesizer":"in"
  PASSED

  ...

  *** ALL PASSED ***
  This concludes the tests. Exiting.
  ```

### Trying the daemon

If you are working on the daemon and want to try out your code, you need to
both stop any system running version, and run it in a separate environment:

  ```console
  $ sudo systemctl stop midiminder.service
  $ make test-shell
  mkdir -p /tmp/midiminder-test/runtime
  mkdir -p /tmp/midiminder-test/state
  $SHELL --rcfile ./build/test-env || true

  (midiminder test): ./build/midiminder daemon
  STATE_DIRECTORY=/tmp/midiminder-test/state
  RUNTIME_DIRECTORY=/tmp/midiminder-test/runtime
  Rules file /tmp/midiminder-test/state/profile.rules read, 0 rules.
  Rules file /tmp/midiminder-test/state/observed.rules read, 0 rules.
  Reviewing port: Midi Through:Port-0 [14:0]+
  Reviewing port: MicroMonsta 2:MIDI 1 [32:0]+
  Reviewing port: Pure Data:Midi-In 1 [130:0]+
  Reviewing port: Pure Data:Midi-Out 1 [130:1]+
  ...
  ```

Then, in a different shell, you can try the commands to control it:

  ```console
  $ make test-shell
  mkdir -p /tmp/midiminder-test/runtime
  mkdir -p /tmp/midiminder-test/state
  $SHELL --rcfile ./build/test-env || true

  (midiminder test): ./build/midiminder status
  Daemon is running.
    0 profile rules.
    0 observed rules.
    4 active ports.
    0 active connections

  (midiminder test): ./build/midiminder load rules/generic.rules

  (midiminder test): ./build/midiwala list
  Ports:
      MicroMonsta 2      : MIDI 1     [ 32:0] <->
      Midi Through       : Port-0     [ 14:0] <->
      Pure Data          : Midi-In 1  [130:0] <--
      Pure Data          : Midi-Out 1 [130:1] -->
  Connections:
      MicroMonsta 2:MIDI 1 [32:0]+ --> Pure Data:Midi-In 1 [130:0]+
      Pure Data:Midi-Out 1 [130:1]+ --> MicroMonsta 2:MIDI 1 [32:0]+

  (midiminder test):
  ```