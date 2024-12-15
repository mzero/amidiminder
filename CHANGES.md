# midiminder Changes by Release

This file summarizes the changes made with each release of the code.

-----------

# 1.0 - 2024-12-15

* New names:
  * **midiminder**
  * **midiwala**

* Fixes:
  * Documentation improvements
  * Small improvemetns in display ports, and empty lists
  * Can now parse exact match rules for ports that have colons in them
  * Handle unparseable state files gracefully



# 0.90 - 2024-12-07

* Split into two commands:
  * amidiminder - the deamon, and user commands to manage profiles
  * amidiview - interactive viewer, and command line replacement for aconnect

* Interactive port and connection viewer
  * terminal graphic user interface
  * commands to connect and disconnect ports, with undo
  * dynamically updates view with system changes

* amidiview list command
  * shows all ports and connections in an easy to read format
  * has many display options

* amidiview connect/disconnect commands
  * uses simple port name syntax, inluding defaulting ports if just a client
    name is used.
  * can use ALSA ids if you really want
  * matches aconnect usage

-----------

# 0.80 - 2024-12-01

* Profile files
  * no longer a single file in /etc
  * loaded by user commands, and persisted by daemon
  * observed rules also persisted by daemon
  * persisted state survives daemon restarts and system reboots

* Command line overhaul:
  * subcommands for running the daemon, and for controlling it
  * command line help & version info
  * verbosity controls

* Default port logic - which port to use when rule has just a client name
  This is much improved over the old, complex, wildcard rule logic, and now
  almost always does exactly what you want.

* Proper packaging
  * deb file packaging
  * systemd service overhaul
    * use runtime and state directories as supplied by systemd
    * security hardening
    * proper reload support
  * man pages
  * example rules files in /usr/share/doc/amidiminder/examples


-----------

# 0.70 - 2020-12-15
**early adopter testing**
This is the first complete and usable release of amidiminder.

