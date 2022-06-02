#include "../include/xivres/packed_stream.h"
#include "../include/xivres/unpacked_stream.h"

xivres::unpacked_stream xivres::packed_stream::GetUnpackedStream(std::span<uint8_t> obfuscatedHeaderRewrite) const {
	return xivres::unpacked_stream(std::static_pointer_cast<const packed_stream>(shared_from_this()), obfuscatedHeaderRewrite);
}

std::unique_ptr<xivres::unpacked_stream> xivres::packed_stream::GetUnpackedStreamPtr(std::span<uint8_t> obfuscatedHeaderRewrite) const {
	return std::make_unique<xivres::unpacked_stream>(std::static_pointer_cast<const packed_stream>(shared_from_this()), obfuscatedHeaderRewrite);
}
