#include <pybind11/pybind11.h>
#include <pybind11/numpy.h>
#include <string>
#include <cstdint>

#include "spng.h"

namespace py = pybind11;

#define STRINGIFY(x) #x
#define MACRO_STRINGIFY(x) STRINGIFY(x)

py::bytes encode_image(py::array image) {
    std::unique_ptr<spng_ctx, void(*)(spng_ctx*)> ctx(spng_ctx_new(SPNG_CTX_ENCODER), spng_ctx_free);

    spng_set_option(ctx.get(), SPNG_ENCODE_TO_BUFFER, 1);

    uint8_t bit_depth = image.dtype().itemsize() * 8;
    uint8_t color_type = SPNG_COLOR_TYPE_GRAYSCALE;

    if (image.ndim() == 3) {
        switch (image.shape(2)) {
            case 1: color_type = SPNG_COLOR_TYPE_GRAYSCALE; break;
            case 2: color_type = SPNG_COLOR_TYPE_GRAYSCALE_ALPHA; break;
            case 3: color_type = SPNG_COLOR_TYPE_TRUECOLOR; break;
            case 4: color_type = SPNG_COLOR_TYPE_TRUECOLOR_ALPHA; break;
            default: throw new std::runtime_error("pyspng: Too many channels in image.");
        }
    }

    struct spng_ihdr ihdr = {
        .height = static_cast<uint32_t>(image.shape(1)),
        .width = static_cast<uint32_t>(image.shape(0)),
        .bit_depth = bit_depth,
        .color_type = color_type
    };
    spng_set_ihdr(ctx.get(), &ihdr);

    /* SPNG_FMT_PNG is a special value that matches the format in ihdr,
       SPNG_ENCODE_FINALIZE will finalize the PNG with the end-of-file marker */
    spng_encode_image(ctx.get(), image.data(), image.nbytes(), SPNG_FMT_PNG, SPNG_ENCODE_FINALIZE);

    size_t png_size = 0;
    int error = 0;
    /* PNG is written to an internal buffer by default */
    // std::unique_ptr<unsigned char *> pngbuffer;
    // pngbuffer = std::move(static_cast<unsigned char*>(
    //     spng_get_png_buffer(ctx.get(), &png_size, &error)
    // ));
    char *pngbuffer = static_cast<char*>(
        spng_get_png_buffer(ctx.get(), &png_size, &error)
    );

    if (error) {
        free(pngbuffer);
        std::string errstr(spng_strerror(error));
        throw new std::runtime_error(errstr);
    }

    std::string outbytes(pngbuffer, png_size);
    free(pngbuffer);
    return py::bytes(outbytes);
}

py::array decode_image_bytes(py::bytes png_bits, spng_format fmt) {
    std::unique_ptr<spng_ctx, void(*)(spng_ctx*)> ctx(spng_ctx_new(0),  spng_ctx_free);

    // Ignore and don't calculate chunk CRC's
    spng_set_crc_action(ctx.get(), SPNG_CRC_USE, SPNG_CRC_USE);

    // Set memory usage limits for storing standard and unknown chunks,
    // this is important when reading arbitrary files!
    size_t limit = 1024 * 1024 * 64;
    spng_set_chunk_limits(ctx.get(), limit, limit);

    // Set source PNG
    std::string bits = png_bits;
    spng_set_png_buffer(ctx.get(), bits.data(), bits.length());

    struct spng_ihdr ihdr;
    int res;
    if ((res = spng_get_ihdr(ctx.get(), &ihdr)) != SPNG_OK) {
        throw std::runtime_error("pyspng: could not decode ihdr: " + std::string(spng_strerror(res)));
    }

    // Decide spng_format based on ihdr.
    //
    // Note: this is most likely buggy/incomplete for 16-bit formats.
    if (fmt == 0) {
        switch (ihdr.color_type) {
            case SPNG_COLOR_TYPE_GRAYSCALE:
                // TODO no G16 output format? using alpha
                fmt = ihdr.bit_depth <= 8 ? SPNG_FMT_G8 : SPNG_FMT_GA16;
                break;
            case SPNG_COLOR_TYPE_TRUECOLOR:
                // TODO no RGB16 output format? using alpha
                fmt = ihdr.bit_depth <= 8 ? SPNG_FMT_RGB8 : SPNG_FMT_RGBA16;
                break;
            case SPNG_COLOR_TYPE_INDEXED:
                fmt = SPNG_FMT_RGB8;
                break;
            case SPNG_COLOR_TYPE_GRAYSCALE_ALPHA:
                fmt = ihdr.bit_depth <= 8 ? SPNG_FMT_GA8 : SPNG_FMT_GA16;
                break;
            case SPNG_COLOR_TYPE_TRUECOLOR_ALPHA:
                fmt = ihdr.bit_depth <= 8 ? SPNG_FMT_RGBA8 : SPNG_FMT_RGBA16;
                break;
        }
    }

    int nc;
    int cs;
    switch (fmt) {
        case SPNG_FMT_RGBA8:    nc = 4; cs = 1; break;
        case SPNG_FMT_RGBA16:   nc = 4; cs = 2; break;
        case SPNG_FMT_RGB8:     nc = 3; cs = 1; break;
        case SPNG_FMT_GA8:      nc = 2; cs = 1; break;
        case SPNG_FMT_GA16:     nc = 2; cs = 2; break;
        case SPNG_FMT_G8:       nc = 1; cs = 1; break;
        //case SPNG_FMT_G16:      nc = 1; cs = 2; break;
        default:
            throw std::runtime_error("pyspng: invalid output fmt");
    }
    int w = ihdr.width;
    int h = ihdr.height;
    size_t out_size;

    if ((res = spng_decoded_image_size(ctx.get(), fmt, &out_size)) != SPNG_OK) {
        throw std::runtime_error("pyspng: could not decode image size: " + std::string(spng_strerror(res)));
    }

    void* data = (void*)malloc(out_size);
    if ((res = spng_decode_image(ctx.get(), data, out_size, fmt, 0)) != SPNG_OK) {
        free(data);
        throw std::runtime_error("pyspng: could not decode image: " + std::string(spng_strerror(res)));
    }

    py::capsule free_when_done(data, [](void *f) {
        free(f);
    });

    return py::array(
        cs == 1 ? py::dtype("uint8") : py::dtype("uint16"),
        {h, w, nc},              // shape
        {w*nc*cs, nc*cs, cs},    // index strides in bytes
        (uint8_t*)data,          // the data pointer
        free_when_done           // numpy array references this parent
    );
}

PYBIND11_MODULE(_pyspng_c, m) {
    m.doc() = R"pbdoc(
        .. currentmodule:: _pyspng_c

        .. autosummary::
           :toctree: _generate

           spng_format
           spng_encode_image
           spng_decode_image_bytes
    )pbdoc";

    py::enum_<spng_format>(m, "spng_format")
        .value("SPNG_FMT_AUTO",   (spng_format)0) // Note: not a libspng enum value
        .value("SPNG_FMT_RGBA8",  SPNG_FMT_RGBA8)
        .value("SPNG_FMT_RGBA16", SPNG_FMT_RGBA16)
        .value("SPNG_FMT_RGB8",   SPNG_FMT_RGB8)
        .value("SPNG_FMT_GA8",    SPNG_FMT_GA8)
        .value("SPNG_FMT_GA16",   SPNG_FMT_GA16)
        .value("SPNG_FMT_G8",     SPNG_FMT_G8)
        .export_values();

    m.def("spng_encode_image", &encode_image, py::arg("image"), R"pbdoc(
        Encode a Numpy array into a PNG bytestream.

        Note:
            If present, the third index is used to represent the channel.
            Number of channels correspond to:
                1: Grayscale
                2: Grayscale + Alpha Channel
                3: RGB
                4: RGBA

            The maximum width and heights are 2^31-1.

        Args:
            image (numpy.ndarray): A 2D image potentially with multiple channels.

        Returns:
            bytes: A valid PNG bytestream.
    )pbdoc");

    m.def("spng_decode_image_bytes", &decode_image_bytes, py::arg("data"), py::arg("fmt"), R"pbdoc(
        Decode PNG bytes into a numpy array.

        Note:
            Single-channel data is returned with shape [h,w,1] instead of [h,w] for
            simplicity.  Use `arr[:,:,0]` to drop the extra dimension if you want
            PIL.Image compatible shapes.

        Args:
            data (bytes): PNG file contents in memory.
            fmt: Output format.  SPNG_FMT_AUTO will auto-detect the output format based on PNG contents.

        Returns:
            numpy.ndarray: Image pixel data in shape (height, width, nc).

    )pbdoc");
#ifdef VERSION_INFO
    m.attr("__version__") = MACRO_STRINGIFY(VERSION_INFO);
#else
    m.attr("__version__") = "dev";
#endif
}
