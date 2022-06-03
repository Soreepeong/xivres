#ifndef XIVRES_TEXTURE_H_
#define XIVRES_TEXTURE_H_

#include "common.h"
#include "util.byte_order.h"

// https://github.com/NotAdam/Lumina/blob/master/src/Lumina/Data/Files/TexFile.cs
namespace xivres::texture {
	enum class attribute : uint32_t {
		DiscardPerFrame = 0x1,
		DiscardPerMap = 0x2,
		Managed = 0x4,
		UserManaged = 0x8,
		CpuRead = 0x10,
		LocationMain = 0x20,
		NoGpuRead = 0x40,
		AlignedSize = 0x80,
		EdgeCulling = 0x100,
		LocationOnion = 0x200,
		ReadWrite = 0x400,
		Immutable = 0x800,
		TextureRenderTarget = 0x100000,
		TextureDepthStencil = 0x200000,
		TextureType1D = 0x400000,
		TextureType2D = 0x800000,
		TextureType3D = 0x1000000,
		TextureTypeCube = 0x2000000,
		TextureTypeMask = 0x3C00000,
		TextureSwizzle = 0x4000000,
		TextureNoTiled = 0x8000000,
		TextureNoSwizzle = 0x80000000,
	};

	inline attribute operator| (attribute l, attribute r) {
		return static_cast<attribute>(static_cast<uint32_t>(l) | static_cast<uint32_t>(r));
	}

	inline attribute operator& (attribute l, attribute r) {
		return static_cast<attribute>(static_cast<uint32_t>(l) & static_cast<uint32_t>(r));
	}

	inline bool operator!(attribute lss) {
		return static_cast<uint32_t>(lss) == 0;
	}
	
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
		LE<attribute> Attribute;
		LE<format> Type;
		LE<uint16_t> Width;
		LE<uint16_t> Height;
		LE<uint16_t> Depth;
		LE<uint16_t> MipmapCount;
		LE<uint32_t> LodOffsets[3];

		[[nodiscard]] bool has_attribute(attribute a) const {
			return !!(Attribute & a);
		}

		[[nodiscard]] uint32_t header_and_mipmap_offsets_size() const {
			if (has_attribute(attribute::AlignedSize))
				return static_cast<uint32_t>(align(sizeof header + MipmapCount * sizeof uint32_t));
			else
				return sizeof header + MipmapCount * sizeof uint32_t;
		}
	};

	[[nodiscard]] size_t calc_raw_data_length(format type, size_t width, size_t height, size_t depth, size_t mipmapIndex = 0);

	[[nodiscard]] size_t calc_raw_data_length(const header& header, size_t mipmapIndex = 0);
}

#endif