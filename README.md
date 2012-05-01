NanodeRF_EmonWeb
================

Firmware for the NanodeRF to work with emonWeb. emonWeb is a Ruby and Rails port of emonCMS.

Differences
===========

It is a modified NanodeRF (three CT sensors,  RTC relay and GLCD temperature) sketch from [Open Energy Monitor](http://openenergymonitor.org) to get it working with emonWeb instead of emonCMS

* It uses a POST request instead of a GET to relay the data
* Data in plain parameter format instead of semi json
* And of course a different host and path.

Credentials
===========

Authors: Trystan Lea and Glyn Hudson
Adjusted for emonWeb: Frank Oxener
Part of the: openenergymonitor.org project
Licenced under GNU GPL V3
See [http://openenergymonitor.org/emon/license](http://openenergymonitor.org/emon/license)