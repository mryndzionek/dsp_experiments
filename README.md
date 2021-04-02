# dsp_experiments

Placeholder repo for DSP, SDR and ML experiments in C.

Concurrency handled via libdill's channels in conjunction with libwebsocket's ring buffer.

## Main dependencies

  - [libdill](http://libdill.org/)
  - [liquid-dsp](https://github.com/jgaeddert/liquid-dsp)
  - [RtAudio](https://www.music.mcgill.ca/~gary/rtaudio/)
  - [SoapySDR](https://github.com/pothosware/SoapySDR)
  - [TensorFlow Lite](https://www.tensorflow.org/lite/)

For more details regarding dependencies and build process look at [GitHub workflow file](.github/workflows/build.yml).

## Applications

### wbfm_demod

Simple wide band FM radio with mono and stereo demodulation.
Expects a file `stations.txt` with station frequencies.
If the file does not exist, it will perform a scan and create one.

### flex_tx

Transmitting a [flexframe](https://liquidsdr.org/doc/flexframe/) via computer speaker.

### flex_rx

Receiving a [flexframe](https://liquidsdr.org/doc/flexframe/) via computer microphone.

### wav2mel

Turns 16kHz sampled 1s wave files into a Mel scale spectrogram in a form of a PNG.

### keywords

Recognizes 17 keywords spoken into a microphone.

## TODO

  - [ ] eliminate temporary buffer on stack in `link_run`
  - [ ] add more processing blocks
