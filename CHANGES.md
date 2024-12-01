# amidiminder Changes by Release

This file summarizes the changes made with each release of the code.

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

