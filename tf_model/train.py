import os
from functools import partial

os.environ['TF_CPP_MIN_LOG_LEVEL'] = '3'
import tensorflow as tf

from tensorflow.python.keras.backend import log
from tensorflow.keras import models
from tensorflow.keras import layers

import pathlib

import logging

import matplotlib.pyplot as plt
import numpy as np
import seaborn as sns

SAMPLE_RATE = 16000
TRANSPARENT = False


def decode_audio(audio_filename):
    audio, _ = tf.audio.decode_wav(audio_filename)
    return tf.squeeze(audio, axis=-1)


def get_label(file_path):
    parts = tf.strings.split(file_path, os.path.sep)

    # Note: You'll use indexing here instead of tuple unpacking to enable this
    # to work in a TensorFlow graph.
    return parts[-2]


def get_waveform_and_label(file_path):
    label = get_label(file_path)
    audio_binary = tf.io.read_file(file_path)
    waveform = decode_audio(audio_binary)
    return waveform, label


def get_spectrogram(waveform):
    # Padding for files with less than 16000 samples
    zero_padding = tf.random.normal(
        [SAMPLE_RATE] - tf.shape(waveform), stddev=0.001, dtype=tf.float32)

    # Concatenate audio with padding so that all audio clips will be of the
    # same length
    waveform = tf.cast(waveform, tf.float32)
    equal_length = tf.concat([waveform, zero_padding], 0)
    spectrogram = tf.signal.stft(
        equal_length, frame_length=1024, frame_step=256,
        fft_length=1024)

    spectrogram = tf.abs(spectrogram)**2

    return spectrogram


def get_mel_matrix(spectrogram):
    num_spectrogram_bins = spectrogram.shape[-1]
    lower_edge_hertz, upper_edge_hertz, num_mel_bins = 20.0, 7600.0, 80
    return tf.signal.linear_to_mel_weight_matrix(
        num_mel_bins, num_spectrogram_bins, SAMPLE_RATE, lower_edge_hertz,
        upper_edge_hertz)


def get_mel_spectrogram(mel_matrix, spectrograms):
    mel_spectrograms = tf.tensordot(spectrograms, mel_matrix, 1)
    log_mel_spectrograms = tf.math.log(
        mel_spectrograms + tf.keras.backend.epsilon())
    #mfccs = tf.signal.mfccs_from_log_mel_spectrograms(log_mel_spectrograms)
    return log_mel_spectrograms


def plot_spectrogram(spectrogram, ax):
  # Convert to frequencies to log scale and transpose so that the time is
  # represented in the x-axis (columns).
  log_spec = np.log(spectrogram.T)
  X = np.linspace(0, SAMPLE_RATE, log_spec.shape[1])
  Y = range(log_spec.shape[0])
  ax.pcolormesh(X, Y, log_spec, shading='auto')


def plot_mel(mel_spectrogram, ax):
  C = np.swapaxes(mel_spectrogram, 0, 1)
  X = np.linspace(0, SAMPLE_RATE, C.shape[1])
  Y = range(C.shape[0])
  ax.pcolormesh(X, Y, C, shading='auto')


def _get_mel_and_label_id(mel_matrix, audio, label):
  spectrogram = get_spectrogram(audio)
  mel = get_mel_spectrogram(mel_matrix, spectrogram)
  #mel = get_mel_spectrogram_essentia(audio)
  mel = tf.expand_dims(mel, -1)
  label_id = tf.argmax(label == labels)
  return mel, label_id


def preprocess_dataset(files):
  files_ds = tf.data.Dataset.from_tensor_slices(files)
  output_ds = files_ds.map(get_waveform_and_label, num_parallel_calls=AUTOTUNE)
  output_ds = output_ds.map(
      get_mel_and_label_id,  num_parallel_calls=AUTOTUNE)
  return output_ds


logging.basicConfig(level=logging.INFO,
                    format='%(asctime)s.%(msecs)03d %(levelname)-8s %(message)s',
                    datefmt='%m-%d %H:%M:%S',
                    filemode='w')

logging.info('Tensorflow version: %s', tf.__version__)
assert(tf.__version__ == '2.7.0')

logging.info(tf.config.list_physical_devices())
#tf.config.set_visible_devices([], 'GPU') # to force CPU

# Set seed for experiment reproducibility
seed = 42
tf.random.set_seed(seed)
np.random.seed(seed)

data_dir = pathlib.Path('data')
if not data_dir.exists():
  tf.keras.utils.get_file(
      'speech_commands.tar.gz',
      origin="http://download.tensorflow.org/data/speech_commands_v0.02.tar.gz",
      extract=True,
      cache_dir='.', cache_subdir='data')

#labels = np.array(tf.io.gfile.listdir(str(data_dir)))
labels = ['down', 'go', 'left', 'no', 'off',
          'on', 'right', 'stop', 'up', 'yes']
logging.info('Labels: %s', labels)

filenames = tf.io.gfile.glob(
    iter({'data/' + l + '/*' for l in labels}))
filenames = tf.random.shuffle(filenames)
num_samples = len(filenames)
logging.info('Number of total samples: %d', num_samples)
for l in labels:
    logging.info('Number of samples per \'{}\': %d'.format(l),
                 len(tf.io.gfile.listdir(str(data_dir/l))))
logging.info('Example file tensor: %s', filenames[0])

files_count = len(filenames)
train_count = int(files_count * 0.8)
val_count = int(files_count * 0.1)
test_count = files_count - train_count - val_count

train_files = filenames[:train_count]
val_files = filenames[train_count: train_count + val_count]
test_files = filenames[-val_count:]

logging.info('Training set size: %d', len(train_files))
logging.info('Validation set size: %d', len(val_files))
logging.info('Test set size: %d', len(test_files))

AUTOTUNE = tf.data.experimental.AUTOTUNE
files_ds = tf.data.Dataset.from_tensor_slices(train_files)
waveform_ds = files_ds.map(get_waveform_and_label, num_parallel_calls=AUTOTUNE)

for waveform, label in waveform_ds.take(1):
    label = label.numpy().decode('utf-8')
    spectrogram = get_spectrogram(waveform)
    mel_matrix = get_mel_matrix(spectrogram)
    #np.savetxt('mel_matrix.txt', mel_matrix.numpy(), delimiter=",")
    mel_spectrogram = get_mel_spectrogram(mel_matrix, spectrogram)
    # mel_spectrogram = get_mel_spectrogram_essentia(waveform)AUTOTUNE = tf.data.AUTOTUNE
files_ds = tf.data.Dataset.from_tensor_slices(train_files)
waveform_ds = files_ds.map(get_waveform_and_label, num_parallel_calls=AUTOTUNE)

for waveform, label in waveform_ds.take(1):
    label = label.numpy().decode('utf-8')
    spectrogram = get_spectrogram(waveform)
    mel_spectrogram = get_mel_spectrogram(mel_matrix, spectrogram)

get_mel_and_label_id = partial(_get_mel_and_label_id, mel_matrix)

logging.info('Label: %s', label)
logging.info('Waveform shape: %s', waveform.shape)
logging.info('Spectrogram shape: %s', spectrogram.shape)
logging.info('Mel spectrogram shape: %s', mel_spectrogram.shape)

fig, ax = plt.subplots(1, figsize=(5, 5))
ax.pcolormesh(mel_matrix)
ax.set_title('Mel matrix')
plt.savefig('figures/mel_mat.png', transparent=TRANSPARENT)

fig, axes = plt.subplots(3, figsize=(15, 11))
timescale = np.arange(waveform.shape[0])
axes[0].plot(timescale, waveform.numpy())
axes[0].set_title('Waveform')
for ax in axes:
  ax.set_xlim([0, SAMPLE_RATE])
plot_spectrogram(spectrogram.numpy(), axes[1])
axes[1].set_title('Spectrogram')
plot_mel(mel_spectrogram.numpy(), axes[2])
axes[2].set_title('Mel')
plt.savefig('figures/wav_spect_mel.png', transparent=TRANSPARENT)

mel_ds = waveform_ds.map(get_mel_and_label_id, num_parallel_calls=AUTOTUNE)
rows = 4
cols = 4
n = rows * cols
fig, axes = plt.subplots(rows, cols, figsize=(10, 10))
for i, (spectrogram, label_id) in enumerate(mel_ds.take(n)):
  r = i // cols
  c = i % cols
  ax = axes[r][c]
  plot_mel(np.squeeze(spectrogram.numpy()), ax)
  ax.set_title(labels[label_id.numpy()])
  ax.axis('off')

plt.savefig('figures/mel_spectrograms.png', transparent=TRANSPARENT)

train_ds = mel_ds

logging.info('Preprocessing data...')
val_ds = preprocess_dataset(val_files)
test_ds = preprocess_dataset(test_files)

batch_size = 64
train_ds = train_ds.batch(batch_size)
val_ds = val_ds.batch(batch_size)

logging.info('Caching data...')
train_ds = train_ds.cache().prefetch(AUTOTUNE)
val_ds = val_ds.cache().prefetch(AUTOTUNE)

for spectrogram, _ in mel_ds.take(1):
  input_shape = spectrogram.shape
logging.info('Input shape: %s', input_shape)
num_labels = len(labels)

norm_layer = layers.Normalization()
logging.info('Adapting normalization layer...')
norm_layer.adapt(train_ds.map(lambda x, _: x))

model = models.Sequential([
    layers.Input(shape=input_shape),
    layers.Resizing(32, 32),
    norm_layer,
    layers.Conv2D(32, 3, activation='relu'),
    layers.Conv2D(64, 3, activation='relu'),
    layers.MaxPooling2D(),
    layers.Dropout(0.25),
    layers.Flatten(),
    layers.Dense(128, activation='relu'),
    layers.Dropout(0.5),
    layers.Dense(num_labels),
])

model.summary()

model.compile(
    optimizer=tf.keras.optimizers.Adam(),
    loss=tf.keras.losses.SparseCategoricalCrossentropy(from_logits=True),
    metrics=['accuracy'],
)

EPOCHS = 30
history = model.fit(
    train_ds,
    validation_data=val_ds,
    epochs=EPOCHS,
    callbacks=tf.keras.callbacks.EarlyStopping(verbose=1, patience=2),
)

model.save('model.h5')
converter = tf.lite.TFLiteConverter.from_keras_model(model)
tflite_model = converter.convert()
with open('model.tflite', 'wb') as f:
  f.write(tflite_model)

acc = history.history['accuracy']
val_acc = history.history['val_accuracy']
loss = history.history['loss']
val_loss = history.history['val_loss']

epochs = range(len(acc))

plt.figure()
plt.ylim(0, 1)
plt.plot(epochs, acc, 'bo', label='Training acc')
plt.plot(epochs, val_acc, 'b', label='Validation acc')
plt.title('Training and validation accuracy')
plt.legend()
plt.savefig('figures/train_val_acc.png', transparent=TRANSPARENT)

plt.figure()
plt.plot(epochs, loss, 'bo', label='Training loss')
plt.plot(epochs, val_loss, 'b', label='Validation loss')
plt.title('Training and validation loss')
plt.legend()
plt.savefig('figures/train_val_loss.png', transparent=TRANSPARENT)

test_audio = []
test_labels = []

for audio, label in test_ds:
  test_audio.append(audio.numpy())
  test_labels.append(label.numpy())

test_audio = np.array(test_audio)
test_labels = np.array(test_labels)

y_pred = np.argmax(model.predict(test_audio), axis=1)
y_true = test_labels

test_acc = sum(y_pred == y_true) / len(y_true)
logging.info(f'Test set accuracy: {test_acc:.1%}')

confusion_mtx = tf.math.confusion_matrix(y_true, y_pred)
plt.figure(figsize=(10, 8))
sns.heatmap(confusion_mtx, xticklabels=labels, yticklabels=labels,
            annot=True, fmt='g')
plt.xlabel('Prediction')
plt.ylabel('Label')
plt.savefig('figures/confusion_mat.png', transparent=TRANSPARENT)
