### LPC encoder Python script

This is the Python script which can be used to generate LPC speech
data to the [LPC decoder](../lpc_decoder/main.c) application.
Uses [Praat](https://www.fon.hum.uva.nl/praat/), so make sure it's installed.
To add new words/sounds modify the `data` list in the `generate.py` script.
Strings will be converted to speech via `Praat`. WAV files will be resampled.
