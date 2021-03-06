#include <pybind11/pybind11.h>
#include <pybind11/numpy.h>
#include <string>
#include <cstdint>

#include "spng.h"

namespace py = pybind11;

#define STRINGIFY(x) #x
#define MACRO_STRINGIFY(x) STRINGIFY(x)

enum ProgressiveMode {
    PROGRESSIVE_MODE_NONE = 0,
    PROGRESSIVE_MODE_PROGRESSIVE = 1,
    PROGRESSIVE_MODE_INTERLACED = 2
};

template <typename T>
void encode_progressive_image(
    const std::unique_ptr<spng_ctx, void(*)(spng_ctx*)> &ctx,
    const py::array &image,
    const bool interlaced
) {
    spng_encode_image(
        ctx.get(), image.data(), image.nbytes(), 
        SPNG_FMT_PNG, SPNG_ENCODE_PROGRESSIVE
    );

    int error;
    size_t width = image.shape(1);
    size_t height = image.shape(0);
    size_t num_channels = 1;

    if (image.ndim() > 2) {
        num_channels = image.shape(2);
    }

    struct spng_row_info row_info;
    const T* imgptr = static_cast<const T*>(image.data());

    if (interlaced) {
        do {
            error = spng_get_row_info(ctx.get(), &row_info);
            if (error) {
                break;
            }

            const T *row = imgptr + width * num_channels * row_info.row_num;
            error = spng_encode_row(
                ctx.get(), 
                static_cast<const void *>(row), 
                width * num_channels * sizeof(T)
            );
        } while (!error);
    }
    else {
        for (size_t y = 0; y < height; y++) {
            const T *row = imgptr + width * num_channels * y;
            error = spng_encode_row(
                ctx.get(), 
                static_cast<const void *>(row), 
                width * num_channels * sizeof(T)
            );

            if (error) { 
                break;
            }
        }
    }

    if (error == SPNG_EOI) {
        spng_encode_chunks(ctx.get());
    }
    else {
        std::string errstr(spng_strerror(error));
        throw new std::runtime_error(errstr);
    }
}

py::bytes encode_image(
    const py::array &image, 
    const int progressive = PROGRESSIVE_MODE_NONE,
    const int compress_level = 6
) {
    if (progressive < 0 || progressive > 2) {
        throw new std::runtime_error("pyspng: Unsupported progressive mode option: " + std::to_string(progressive));
    }

    std::unique_ptr<spng_ctx, void(*)(spng_ctx*)> ctx(spng_ctx_new(SPNG_CTX_ENCODER), spng_ctx_free);

    spng_set_option(ctx.get(), SPNG_ENCODE_TO_BUFFER, 1);
    spng_set_option(ctx.get(), SPNG_IMG_COMPRESSION_LEVEL, compress_level);

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

    uint8_t interlace_method = (progressive == PROGRESSIVE_MODE_INTERLACED)
        ? SPNG_INTERLACE_ADAM7 
        : SPNG_INTERLACE_NONE;

    struct spng_ihdr ihdr = {
        static_cast<uint32_t>(image.shape(1)), // .width
        static_cast<uint32_t>(image.shape(0)), // .height
        bit_depth, // .bit_depth
        color_type, // .color_type
        0, // .compression_method
        0, // .filter_method
        static_cast<uint8_t>(interlace_method) // .interlace_method
    };
    spng_set_ihdr(ctx.get(), &ihdr);

    /* SPNG_FMT_PNG is a special value that matches the format in ihdr,
       SPNG_ENCODE_FINALIZE will finalize the PNG with the end-of-file marker */
    if (progressive == PROGRESSIVE_MODE_NONE) {
        spng_encode_image(ctx.get(), image.data(), image.nbytes(), SPNG_FMT_PNG, SPNG_ENCODE_FINALIZE);
    }
    else if (progressive == PROGRESSIVE_MODE_PROGRESSIVE || progressive == PROGRESSIVE_MODE_INTERLACED) {
        if (bit_depth == 16) {
            encode_progressive_image<uint16_t>(ctx, image, (progressive == PROGRESSIVE_MODE_INTERLACED));    
        }
        else {
            encode_progressive_image<uint8_t>(ctx, image, (progressive == PROGRESSIVE_MODE_INTERLACED));
        }
    }
    else {
        throw new std::runtime_error("This should never happen.");
    }

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

py::dict read_header(py::bytes png_bits) {
    std::unique_ptr<spng_ctx, void(*)(spng_ctx*)> ctx(spng_ctx_new(0),  spng_ctx_free);

    // Set source PNG
    std::string bits = png_bits;
    spng_set_png_buffer(ctx.get(), bits.data(), bits.length());

    struct spng_ihdr ihdr;
    int res;
    if ((res = spng_get_ihdr(ctx.get(), &ihdr)) != SPNG_OK) {
        throw std::runtime_error("pyspng: could not decode ihdr: " + std::string(spng_strerror(res)));
    }

    py::dict header;
    header["width"] = ihdr.width;
    header["height"] = ihdr.height;
    header["bit_depth"] = ihdr.bit_depth;
    header["color_type"] = ihdr.color_type;
    header["compression_method"] = ihdr.compression_method;
    header["filter_method"] = ihdr.filter_method;
    header["interlace_method"] = ihdr.interlace_method;
    
    return header;
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
    spng_format render_fmt = fmt;
    if (fmt == 0) {
        switch (ihdr.color_type) {
            case SPNG_COLOR_TYPE_GRAYSCALE:
                // TODO no G16 output format? using alpha
                render_fmt = ihdr.bit_depth <= 8 ? SPNG_FMT_G8 : SPNG_FMT_GA16;
                break;
            case SPNG_COLOR_TYPE_TRUECOLOR:
                // TODO no RGB16 output format? using alpha
                render_fmt = ihdr.bit_depth <= 8 ? SPNG_FMT_RGB8 : SPNG_FMT_RGBA16;
                break;
            case SPNG_COLOR_TYPE_INDEXED:
                render_fmt = SPNG_FMT_RGB8;
                break;
            case SPNG_COLOR_TYPE_GRAYSCALE_ALPHA:
                render_fmt = ihdr.bit_depth <= 8 ? SPNG_FMT_GA8 : SPNG_FMT_GA16;
                break;
            case SPNG_COLOR_TYPE_TRUECOLOR_ALPHA:
                render_fmt = ihdr.bit_depth <= 8 ? SPNG_FMT_RGBA8 : SPNG_FMT_RGBA16;
                break;
        }
    }

    int nc; // num columns
    int cs; // channel stride
    switch (render_fmt) {
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

    // issue in libspng prevents direct rendering of GA8 and GA16
    // see: https://github.com/randy408/libspng/issues/207
    if (fmt == 0 && (render_fmt == SPNG_FMT_GA8 || render_fmt == SPNG_FMT_GA16)) {
        render_fmt = SPNG_FMT_PNG;
    }

    if ((res = spng_decoded_image_size(ctx.get(), render_fmt, &out_size)) != SPNG_OK) {
        throw std::runtime_error("pyspng: could not decode image size: " + std::string(spng_strerror(res)));
    }

    void* data = (void*)malloc(out_size);
    if ((res = spng_decode_image(ctx.get(), data, out_size, render_fmt, 0)) != SPNG_OK) {
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
           spng_read_header
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

    m.def("spng_read_header", &read_header, py::arg("data"), R"pbdoc(
        Read the header of the PNG file and return it as a dict with
        keys to integer values.

        Returns {
            width, height, bit_depth,
            color_type, compression_method,
            filter_method, interlace_method
        }
    )pbdoc");

    m.def("spng_encode_image", 
        &encode_image, py::arg("image"), py::arg("progressive"), 
        py::arg("compress_level"), R"pbdoc(
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
            progressive (int): 
                0: off, regular PNG
                1: on, progressive PNG
                2: on, interlaced progressive PNG

                Also see ProgressiveMode enum.
            compress_level (int): 0-9 input to zlib/miniz
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
