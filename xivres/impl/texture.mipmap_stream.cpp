#include "../include/xivres/texture.mipmap_stream.h"
#include "../include/xivres/texture.stream.h"
#include "../include/xivres/util.dxt.h"

xivres::texture::mipmap_stream::mipmap_stream(size_t width, size_t height, size_t depths, format type)
	: Width(static_cast<uint16_t>(width))
	, Height(static_cast<uint16_t>(height))
	, Depth(static_cast<uint16_t>(depths))
	, Type(type)
	, SupposedMipmapLength(static_cast<std::streamsize>(calc_raw_data_length(type, width, height, depths))) {
	if (Width != width || Height != height || Depth != depths)
		throw std::invalid_argument("dimensions can hold only uint16 ranges");
}

std::streamsize xivres::texture::mipmap_stream::size() const {
	return SupposedMipmapLength;
}

std::shared_ptr<xivres::texture::stream> xivres::texture::mipmap_stream::to_single_texture_stream() {
	auto res = std::make_shared<texture::stream>(Type, Width, Height);
	res->set_mipmap(0, 0, std::dynamic_pointer_cast<mipmap_stream>(this->shared_from_this()));
	return res;
}

xivres::texture::wrapped_mipmap_stream::wrapped_mipmap_stream(header header, size_t mipmapIndex, std::shared_ptr<const stream> underlying)
	: mipmap_stream(
		(std::max)(1, header.Width >> mipmapIndex),
		(std::max)(1, header.Height >> mipmapIndex),
		(std::max)(1, header.Depth >> mipmapIndex),
		header.Type)
	, m_underlying(std::move(underlying)) {}

xivres::texture::wrapped_mipmap_stream::wrapped_mipmap_stream(size_t width, size_t height, size_t depths, format type, std::shared_ptr<const stream> underlying)
	: mipmap_stream(width, height, depths, type)
	, m_underlying(std::move(underlying)) {}

std::streamsize xivres::texture::wrapped_mipmap_stream::read(std::streamoff offset, void* buf, std::streamsize length) const {
	if (offset + length > SupposedMipmapLength)
		length = SupposedMipmapLength - offset;

	const auto read = m_underlying->read(offset, buf, length);
	std::fill_n(static_cast<char*>(buf) + read, length - read, 0);
	return length;
}

xivres::texture::memory_mipmap_stream::memory_mipmap_stream(size_t width, size_t height, size_t depths, format type)
	: mipmap_stream(width, height, depths, type)
	, m_data(SupposedMipmapLength) {

}

xivres::texture::memory_mipmap_stream::memory_mipmap_stream(size_t width, size_t height, size_t depths, format type, std::vector<uint8_t> data)
	: mipmap_stream(width, height, depths, type)
	, m_data(std::move(data)) {
	m_data.resize(SupposedMipmapLength);
}

std::streamsize xivres::texture::memory_mipmap_stream::read(std::streamoff offset, void* buf, std::streamsize length) const {
	const auto available = (std::min)(static_cast<std::streamsize>(m_data.size() - offset), length);
	std::copy_n(&m_data[static_cast<size_t>(offset)], available, static_cast<char*>(buf));
	return available;
}

std::shared_ptr<xivres::texture::memory_mipmap_stream> xivres::texture::memory_mipmap_stream::as_argb8888(const mipmap_stream& strm) {
	const auto pixelCount = calc_raw_data_length(format::L8, strm.Width, strm.Height, strm.Depth);
	const auto cbSource = (std::min)(static_cast<size_t>(strm.size()), calc_raw_data_length(strm.Type, strm.Width, strm.Height, strm.Depth));

	std::vector<uint8_t> result(pixelCount * sizeof util::RGBA8888);
	const auto rgba8888view = util::span_cast<LE<util::RGBA8888>>(result);
	uint32_t pos = 0, read = 0;
	uint8_t buf8[8192];
	switch (strm.Type) {
		case format::L8:
		{
			if (cbSource < pixelCount)
				throw std::runtime_error("Truncated data detected");
			while (const auto len = static_cast<uint32_t>((std::min<uint64_t>)(cbSource - read, sizeof buf8))) {
				strm.read_fully(read, buf8, len);
				read += len;
				for (size_t i = 0; i < len; ++pos, ++i)
					rgba8888view[pos] = util::RGBA8888(buf8[i], buf8[i], buf8[i], 255);
			}
			break;
		}

		case format::A8:
		{
			if (cbSource < pixelCount)
				throw std::runtime_error("Truncated data detected");
			while (const auto len = static_cast<uint32_t>((std::min<uint64_t>)(cbSource - read, sizeof buf8))) {
				strm.read_fully(read, buf8, len);
				read += len;
				for (size_t i = 0; i < len; ++pos, ++i)
					rgba8888view[pos] = util::RGBA8888(255, 255, 255, buf8[i]);
			}
			break;
		}

		case format::A4R4G4B4:
		{
			if (cbSource < pixelCount * sizeof util::RGBA4444)
				throw std::runtime_error("Truncated data detected");
			const auto view = util::span_cast<LE<util::RGBA4444>>(buf8);
			while (const auto len = static_cast<uint32_t>((std::min<uint64_t>)(cbSource - read, sizeof buf8))) {
				strm.read_fully(read, buf8, len);
				read += len;
				for (size_t i = 0, count = len / sizeof util::RGBA4444; i < count; ++pos, ++i)
					rgba8888view[pos] = util::RGBA8888((*view[i]).R * 17, (*view[i]).G * 17, (*view[i]).B * 17, (*view[i]).A * 17);
			}
			break;
		}

		case format::A1R5G5B5:
		{
			if (cbSource < pixelCount * sizeof util::RGBA5551)
				throw std::runtime_error("Truncated data detected");
			const auto view = util::span_cast<LE<util::RGBA5551>>(buf8);
			while (const auto len = static_cast<uint32_t>((std::min<uint64_t>)(cbSource - read, sizeof buf8))) {
				strm.read_fully(read, buf8, len);
				read += len;
				for (size_t i = 0, count = len / sizeof util::RGBA5551; i < count; ++pos, ++i)
					rgba8888view[pos] = util::RGBA8888((*view[i]).R * 255 / 31, (*view[i]).G * 255 / 31, (*view[i]).B * 255 / 31, (*view[i]).A * 255);
			}
			break;
		}

		case format::A8R8G8B8:
			if (cbSource < pixelCount * sizeof util::RGBA8888)
				throw std::runtime_error("Truncated data detected");
			strm.read_fully(0, std::span(rgba8888view));
			break;

		case format::X8R8G8B8:
			if (cbSource < pixelCount * sizeof util::RGBA8888)
				throw std::runtime_error("Truncated data detected");
			strm.read_fully(0, std::span(rgba8888view));
			for (auto& item : rgba8888view) {
				const auto native = *item;
				item = util::RGBA8888(native.R, native.G, native.B, 0xFF);
			}
			break;

		case format::A16B16G16R16F:
		{
			if (cbSource < pixelCount * sizeof util::RGBAF16)
				throw std::runtime_error("Truncated data detected");
			strm.read_fully(0, std::span(rgba8888view));
			const auto view = util::span_cast<util::RGBAF16>(buf8);
			while (const auto len = static_cast<uint32_t>((std::min<uint64_t>)(cbSource - read, sizeof buf8))) {
				strm.read_fully(read, buf8, len);
				read += len;
				for (size_t i = 0, count = len / sizeof util::RGBAF16; i < count; ++pos, ++i) {
					rgba8888view[pos] = util::RGBA8888(
						static_cast<uint32_t>(std::round(255 * view[i].R)),
						static_cast<uint32_t>(std::round(255 * view[i].G)),
						static_cast<uint32_t>(std::round(255 * view[i].B)),
						static_cast<uint32_t>(std::round(255 * view[i].A))
					);
				}
			}
			break;
		}

		case format::A32B32G32R32F:
		{
			if (cbSource < pixelCount * sizeof util::RGBAF32)
				throw std::runtime_error("Truncated data detected");
			strm.read_fully(0, std::span(rgba8888view));
			const auto view = util::span_cast<util::RGBAF32>(buf8);
			while (const auto len = static_cast<uint32_t>((std::min<uint64_t>)(cbSource - read, sizeof buf8))) {
				strm.read_fully(read, buf8, len);
				read += len;
				for (size_t i = 0, count = len / sizeof util::RGBAF32; i < count; ++pos, ++i)
					rgba8888view[pos] = util::RGBA8888(
						static_cast<uint32_t>(std::round(255 * view[i].R)),
						static_cast<uint32_t>(std::round(255 * view[i].G)),
						static_cast<uint32_t>(std::round(255 * view[i].B)),
						static_cast<uint32_t>(std::round(255 * view[i].A))
					);
			}
			break;
		}

		case format::DXT1:
		{
			if (cbSource * 2 < pixelCount)
				throw std::runtime_error("Truncated data detected");
			while (const auto len = static_cast<uint32_t>((std::min<uint64_t>)(cbSource - read, sizeof buf8))) {
				strm.read_fully(read, buf8, len);
				read += len;
				for (size_t i = 0, count = len; i < count; i += 8, pos += 8) {
					DecompressBlockDXT1(
						pos / 2 % strm.Width,
						pos / 2 / strm.Width * 4,
						strm.Width, &buf8[i], &rgba8888view[0]);
				}
			}
			break;
		}

		case format::DXT3:
		{
			if (cbSource * 4 < pixelCount)
				throw std::runtime_error("Truncated data detected");
			while (const auto len = static_cast<uint32_t>((std::min<uint64_t>)(cbSource - read, sizeof buf8))) {
				strm.read_fully(read, buf8, len);
				read += len;
				for (size_t i = 0, count = len; i < count; i += 16, pos += 16) {
					DecompressBlockDXT1(
						pos / 4 % strm.Width,
						pos / 4 / strm.Width * 4,
						strm.Width, &buf8[i], &rgba8888view[0]);
					for (size_t dy = 0; dy < 4; dy += 1) {
						for (size_t dx = 0; dx < 4; dx += 2) {
							const auto native1 = *rgba8888view[dy * strm.Width + dx];
							rgba8888view[dy * strm.Width + dx] = util::RGBA8888(native1.R, native1.G, native1.B, 17 * (buf8[i + dy * 2 + dx / 2] & 0xF));

							const auto native2 = *rgba8888view[dy * strm.Width + dx + 1];
							rgba8888view[dy * strm.Width + dx + 1] = util::RGBA8888(native2.R, native2.G, native2.B, 17 * (buf8[i + dy * 2 + dx / 2] >> 4));
						}
					}
				}
			}
			break;
		}

		case format::DXT5:
		{
			if (cbSource * 4 < pixelCount)
				throw std::runtime_error("Truncated data detected");
			while (const auto len = static_cast<uint32_t>((std::min<uint64_t>)(cbSource - read, sizeof buf8))) {
				strm.read_fully(read, buf8, len);
				read += len;
				for (size_t i = 0, count = len; i < count; i += 16, pos += 16) {
					DecompressBlockDXT5(
						pos / 4 % strm.Width,
						pos / 4 / strm.Width * 4,
						strm.Width, &buf8[i], &rgba8888view[0]);
				}
			}
			break;
		}

		case format::Unknown:
		default:
			throw std::runtime_error("Unsupported type");
	}

	return std::make_shared<memory_mipmap_stream>(strm.Width, strm.Height, strm.Depth, format::A8R8G8B8, std::move(result));
}

std::shared_ptr<const xivres::texture::mipmap_stream> xivres::texture::memory_mipmap_stream::as_argb8888_view(std::shared_ptr<const mipmap_stream> strm) {
	if (strm->Type == format::A8R8G8B8)
		return std::make_shared<wrapped_mipmap_stream>(strm->Width, strm->Height, strm->Depth, strm->Type, std::move(strm));

	return as_argb8888(*strm);
}
