-- README FOR PLUJAIN-MULTIPMIDI --



Plujain-MultipMidi is a very personnal LV2 plugin, there are very very few chances it can be useful for you.

This plugin contains 3 MIDI inputs and 1 MIDI output, you can run it with non-mixer-xt.

Its goal is to enable midi notes coming from seq_in and impact_in, muting the last same note if it is too close in the time. A velocity factor is applied with the velocity of the note on received in kick_in. While note is on in kick_in, all notes are muted.

There are no particular dependencies except these ones needed to build an LV2 plugin.

To build and install just type: <br>
`$ make` <br>
`$ [sudo] make install`

