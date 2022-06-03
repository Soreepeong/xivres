#ifndef XIVRES_TEXTUREPACKEDFILESTREAM_H_
#define XIVRES_TEXTUREPACKEDFILESTREAM_H_

#include "packed_stream.h"

namespace xivres {
	class texture_passthrough_packer : public passthrough_packer<packed::type::texture> {
		static constexpr auto MaxMipmapCountPerTexture = 16;

		std::mutex m_mtx;

		std::vector<uint8_t> m_mergedHeader;
		std::vector<packed::mipmap_block_locator> m_blockLocators;
		std::vector<uint32_t> m_mipmapOffsetsWithRepeats;
		std::vector<uint32_t> m_mipmapSizes;

	public:
		using passthrough_packer<packed::type::texture>::passthrough_packer;

		[[nodiscard]] std::streamsize size() override;

	protected:
		void ensure_initialized() override;

		std::streamsize translate_read(std::streamoff offset, void* buf, std::streamsize length) override;
	};

	class texture_compressing_packer : public compressing_packer<packed::type::texture> {
	public:
		[[nodiscard]] std::unique_ptr<stream> pack(const stream& strm, int compressionLevel) const override;
	};
}

#endif
