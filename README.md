
**Note:** This is a fork of pyspng which can be found here: https://github.com/nurpax/pyspng/. See below for a list of differences.

**Pyspng** is a small library to for efficiently loading PNG files to numpy arrays.
Pyspng does not offer any image manipulation functionality.

Pyspng was originally written to speed up loading uncompressed (PNG compression level 0),
making the PNG file format more suitable to be used in machine learning datasets.  Pyspng
uses the native [libspng](https://github.com/randy408/libspng) library for fast PNG
decoding.  Synthetic benchmarks indicate pyspng to be roughly 2-3x faster in
loading uncompressed PNGs than the Python Pillow library.

## Scripting Example

```python
import numpy as np
import pyspng
from pyspng import ProgressiveMode

# DECODING
with open('test.png', 'rb') as fin:
    nparr = pyspng.load(fin.read())

# ENCODING
binary = pyspng.encode(
    nparr,
    # Options: NONE (0), PROGRESSIVE (1), INTERLACED (2)
    progressive=ProgressiveMode.PROGRESSIVE, 
    compress_level=6
)
with open('test.png', 'wb') as fout:
    fout.write(binary)
```

## CLI Example

There is a CLI included with this distribution.

```bash
# turn a numpy file into a highly compressed progressive PNG
pyspng example.npy --level 9 --progressive # -> example.png

# create a highly compressed progressive interlaced PNG
pyspng example.npy --level 9 --interlaced # -> example.png

# convert a PNG into a numpy file example.npy
pyspng -d example.png 

# read header
pyspng --header example.png
```

## Installation

```bash
pip install pyspng-seunglab
```

Binary wheels are built for Linux, MacOS, and Windows. This library is intended to be a drop-in replacement for pyspng, so simultaneous installations are not possible. If this is inconvinient, we can adjust this.

## Differences from pyspng

1. Compiles on MacOS
2. Upgrades spng to 0.7.2
3. Fixes a bug for decoding grayscale with alpha
4. Adds an encoder function (new in libspng)
5. Replaces zlib with miniz-2.2.0 for simplicity.
6. Adds CLI for compressing/decompressing npy files.
7. Adds function for examining PNG headers.

## License

pyspng and pyspng-seunglab are provided under a BSD-style license that can be found in the LICENSE file. By using, distributing, or contributing to this project, you agree to the terms and conditions of this license.
