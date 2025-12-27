//
// Created by 2401Lucas on 2025-10-30.
//

#include "TextureLoader.h"
#include <fstream>
#include <algorithm>
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

TextureData TextureLoader::LoadFromFile(const std::string &path) {
    // Detect format from extension
    size_t dotPos = path.find_last_of('.');
    if (dotPos == std::string::npos) {
        throw std::runtime_error("Unknown texture format (no extension): " + path);
    }

    std::string extension = path.substr(dotPos + 1);
    std::transform(extension.begin(), extension.end(), extension.begin(), ::tolower);

    if (extension == "png") {
        return LoadPNG(path);
    } else if (extension == "jpg" || extension == "jpeg") {
        return LoadJPG(path);
    } else if (extension == "tga") {
        return LoadTGA(path);
    } else if (extension == "dds") {
        return LoadDDS(path);
    } else if (extension == "hdr") {
        return LoadHDR(path);
    } else {
        throw std::runtime_error("Unsupported texture format: " + extension);
    }
}

// Simplified TGA loader (uncompressed only)
TextureData TextureLoader::LoadTGA(const std::string &path) {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        throw std::runtime_error("Failed to open TGA file: " + path);
    }

    // Read TGA header
    uint8_t header[18];
    file.read(reinterpret_cast<char *>(header), 18);

    uint8_t idLength = header[0];
    uint8_t colorMapType = header[1];
    uint8_t imageType = header[2];

    uint16_t width = header[12] | (header[13] << 8);
    uint16_t height = header[14] | (header[15] << 8);
    uint8_t bitsPerPixel = header[16];

    // Skip image ID
    file.seekg(idLength, std::ios::cur);

    // Only support uncompressed RGB/RGBA
    if (imageType != 2) {
        throw std::runtime_error("Only uncompressed TGA supported");
    }

    TextureData texture;
    texture.width = width;
    texture.height = height;
    texture.name = path;

    size_t bytesPerPixel = bitsPerPixel / 8;
    if (bytesPerPixel == 3) {
        texture.format = TextureFormat::RGBA8_UNORM;
    } else if (bytesPerPixel == 4) {
        texture.format = TextureFormat::RGBA16_FLOAT;
    } else {
        throw std::runtime_error("Unsupported TGA bit depth");
    }

    // Read pixel data
    size_t dataSize = width * height * bytesPerPixel;
    texture.data.resize(dataSize);
    file.read(reinterpret_cast<char *>(texture.data.data()), dataSize);

    // TGA stores pixels as BGR/BGRA, convert to RGB/RGBA
    for (size_t i = 0; i < dataSize; i += bytesPerPixel) {
        std::swap(texture.data[i], texture.data[i + 2]);
    }

    return texture;
}

TextureData TextureLoader::LoadPNG(const std::string &path) {
    int width, height, channels;
    unsigned char *data = stbi_load(path.c_str(), &width, &height, &channels, STBI_rgb_alpha);
    if (!data) {
        throw std::runtime_error("Failed to load PNG: " + path);
    }

    TextureData texture;
    texture.width = width;
    texture.height = height;
    texture.name = path;

    switch (channels) {
        case 1:
        case 2:
        case 3:
            throw std::runtime_error("No Support for png with %s channels");
        case 4: texture.format = TextureFormat::RGBA8_UNORM;
            break;
    }

    size_t dataSize = width * height * channels;
    texture.data.resize(dataSize);
    memcpy(texture.data.data(), data, dataSize);

    stbi_image_free(data);
    return texture;
}

TextureData TextureLoader::LoadJPG(const std::string &path) {
    int width, height, channels;
    unsigned char *data = stbi_load(path.c_str(), &width, &height, &channels, STBI_rgb_alpha);
    if (!data) {
        throw std::runtime_error("Failed to load JPG: " + path);
    }

    TextureData texture;
    texture.width = width;
    texture.height = height;
    texture.name = path;

    switch (channels) {
        case 1:
        case 2:
        case 3:
            throw std::runtime_error("No Support for JPG with %s channels");
            break;
        case 4: texture.format = TextureFormat::RGBA8_UNORM;
            break;
    }

    size_t dataSize = width * height * channels;
    texture.data.resize(dataSize);
    memcpy(texture.data.data(), data, dataSize);

    stbi_image_free(data);
    return texture;
}

TextureData TextureLoader::LoadDDS(const std::string &path) {
    //TODO: Implement
    throw std::runtime_error("DDS loading not implemented");
}

TextureData TextureLoader::LoadHDR(const std::string &path) {
    //TODO: Implement
    throw std::runtime_error("HDR loading requires stb_image library");
}

void TextureLoader::GenerateMipmaps(TextureData &texture) {
    // Simple box filter mipmap generation
    // TODO: use better filtering
    //  Full mipmap generation would require:
    //  1. Allocating space for all mip levels
    //  2. Downsampling each level using proper filtering
    //  3. Storing in the data array with proper offsets

    uint32_t width = texture.width;
    uint32_t height = texture.height;

    // Calculate number of mip levels
    uint32_t mipLevels = 1;
    uint32_t mipWidth = width;
    uint32_t mipHeight = height;
    while (mipWidth > 1 || mipHeight > 1) {
        mipWidth = std::max(1u, mipWidth / 2);
        mipHeight = std::max(1u, mipHeight / 2);
        mipLevels++;
    }

    texture.mipLevels = mipLevels;
}

TextureData TextureLoader::CreateSolidColor(uint32_t width, uint32_t height,
                                            uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    TextureData texture;
    texture.width = width;
    texture.height = height;
    texture.format = TextureFormat::RGBA8_UNORM;
    texture.name = "solid_color";

    size_t dataSize = width * height * 4;
    texture.data.resize(dataSize);

    for (size_t i = 0; i < dataSize; i += 4) {
        texture.data[i + 0] = r;
        texture.data[i + 1] = g;
        texture.data[i + 2] = b;
        texture.data[i + 3] = a;
    }

    return texture;
}

// used to create a checkerboard pattern image
TextureData TextureLoader::CreateCheckerboard(uint32_t width, uint32_t height,
                                              uint32_t checkerSize) {
    TextureData texture;
    texture.width = width;
    texture.height = height;
    texture.format = TextureFormat::RGBA8_UNORM;
    texture.name = "checkerboard";

    size_t dataSize = width * height * 4;
    texture.data.resize(dataSize);

    for (uint32_t y = 0; y < height; y++) {
        for (uint32_t x = 0; x < width; x++) {
            size_t index = (y * width + x) * 4;

            bool checker = ((x / checkerSize) + (y / checkerSize)) % 2 == 0;

            texture.data[index + 0] = checker ? 0 : 255;
            texture.data[index + 1] = 0;
            texture.data[index + 2] = checker ? 0 : 220;
            texture.data[index + 3] = 255;
        }
    }

    return texture;
}
