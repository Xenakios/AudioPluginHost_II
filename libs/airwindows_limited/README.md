This is a limited selection of AirWindows plugins for use in the Xenakios
granular synth and other projects, with some changes in the source code. Notably,
a virtual reset method was added into the base class, to allow implementing clearing of
reverb/delay tails. There is also an experimental implementation of Galactic3 that outputs
Ambisonic encoded audio.

The number of plugins is limited because the full AirWindows plugins suite from AirWindows Consolidated 
is very large and slow to build. Many of the plugins also wouldn't really be suitable for use as
per grain effects.

# Galactic3 Ambisonic notes :

The audio input is still handled as stereo/2 channels. The ambisonic encoding happens by encoding
the 8 reverb output taps from the algorithm into 8 spherical positions. The ambisonic order can be
set via an extra parameter to 1st, 2nd or 3rd order, so there will be 4, 9 or 16 output channels used,
and the code using the plugin must support the required number of output channels, which should not overlap the
input channels.
