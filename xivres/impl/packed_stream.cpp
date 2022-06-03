#include "../include/xivres/packed_stream.h"
#include "../include/xivres/unpacked_stream.h"

xivres::unpacked_stream xivres::packed_stream::get_unpacked(std::span<uint8_t> obfuscatedHeaderRewrite) const {
	return {std::static_pointer_cast<const packed_stream>(shared_from_this()), obfuscatedHeaderRewrite};
}

std::unique_ptr<xivres::unpacked_stream> xivres::packed_stream::make_unpacked_ptr(std::span<uint8_t> obfuscatedHeaderRewrite) const {
	return std::make_unique<unpacked_stream>(std::static_pointer_cast<const packed_stream>(shared_from_this()), obfuscatedHeaderRewrite);
}
