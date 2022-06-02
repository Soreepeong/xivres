#ifndef _XIVRES_TEXTURE_H_
#define _XIVRES_TEXTURE_H_

#include "util.byte_order.h"

#include "common.h"

namespace xivres::texture {
	enum class format : uint32_t {
		Unknown = 0,
		L8 = 4400,
		A8 = 4401,
		A4R4G4B4 = 5184,
		A1R5G5B5 = 5185,
		A8R8G8B8 = 5200,
		X8R8G8B8 = 5201,
		R32F = 8528,
		G16R16F = 8784,
		G32R32F = 8800,
		A16B16G16R16F = 9312,
		A32B32G32R32F = 9328,
		DXT1 = 13344,
		DXT3 = 13360,
		DXT5 = 13361,
		D16 = 16704,
	};

	struct header {
		LE<uint16_t> Unknown1;
		LE<uint16_t> HeaderSize;
		LE<texture::format> Type;
		LE<uint16_t> Width;
		LE<uint16_t> Height;
		LE<uint16_t> Depth;
		LE<uint16_t> MipmapCount;
		char Unknown2[0xC]{};
	};

	size_t calc_raw_data_length(texture::format type, size_t width, size_t height, size_t depth, size_t mipmapIndex = 0);

	size_t calc_raw_data_length(const texture::header& header, size_t mipmapIndex = 0);
}

#endif