#include "../include/xivres/texture.h"

#include <stdexcept>

size_t xivres::texture::calc_raw_data_length(format type, size_t width, size_t height, size_t depth, size_t mipmapIndex /*= 0*/) {
	width = (std::max<size_t>)(1, width >> mipmapIndex);
	height = (std::max<size_t>)(1, height >> mipmapIndex);
	depth = (std::max<size_t>)(1, depth >> mipmapIndex);
	switch (type) {
		case format::L8:
		case format::A8:
			return width * height * depth;

		case format::A4R4G4B4:
		case format::A1R5G5B5:
			return width * height * depth * 2;

		case format::A8R8G8B8:
		case format::X8R8G8B8:
		case format::R32F:
		case format::G16R16F:
			return width * height * depth * 4;

		case format::A16B16G16R16F:
		case format::G32R32F:
			return width * height * depth * 8;

		case format::A32B32G32R32F:
			return width * height * depth * 16;

		case format::DXT1:
			return depth * (std::max<size_t>)(1, ((width + 3) / 4)) * (std::max<size_t>)(1, ((height + 3) / 4)) * 8;

		case format::DXT3:
		case format::DXT5:
			return depth * (std::max<size_t>)(1, ((width + 3) / 4)) * (std::max<size_t>)(1, ((height + 3) / 4)) * 16;

		case format::D16:
		case format::Unknown:
		default:
			throw std::invalid_argument("Unsupported type");
	}
}

size_t xivres::texture::calc_raw_data_length(const header& header, size_t mipmapIndex /*= 0*/) {
	return calc_raw_data_length(header.Type, header.Width, header.Height, header.Depth, mipmapIndex);
}
