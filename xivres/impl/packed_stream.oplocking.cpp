#include "../include/xivres/packed_stream.oplocking.h"

#include "../include/xivres/packed_stream.model.h"
#include "../include/xivres/packed_stream.standard.h"
#include "../include/xivres/packed_stream.texture.h"
#include "../include/xivres/util.unicode.h"

xivres::oplocking_packed_stream::oplocking_packed_stream(xivres::path_spec pathSpec, std::filesystem::path path)
	: packed_stream(std::move(pathSpec))
	, m_path(std::move(path)) {

	const auto ext = util::unicode::convert<std::string>(m_path.extension().wstring(), &util::unicode::lower);
	if (ext == ".tex")
		m_packedType = packed::type::texture;
	else if (ext == ".mdl")
		m_packedType = packed::type::model;
	else
		m_packedType = packed::type::standard;
}

xivres::packed::type xivres::oplocking_packed_stream::get_packed_type() const {
	return m_packedType;
}

std::streamsize xivres::oplocking_packed_stream::size() const {
	if (!open())
		return 0;

	return m_packedStream->size();
}

std::streamsize xivres::oplocking_packed_stream::read(std::streamoff offset, void* buf, std::streamsize length) const {
	if (!open())
		return 0;

	return m_packedStream->read(offset, buf, length);
}

void xivres::oplocking_packed_stream::close() {
	m_packedStream.reset();
	m_oplockingStream.reset();
}

bool xivres::oplocking_packed_stream::open() const {
	if (m_oplockingStream && !m_oplockingStream->done() && m_packedStream)
		return true;

	if (!m_oplockingStream || m_oplockingStream->done()) {
		if (!exists(m_path))
			return false;

		m_oplockingStream = std::make_shared<oplocking_file_stream>(m_path, false);
		if (m_oplockingStream->done())
			return false;
	}

	if (m_oplockingStream && !m_oplockingStream->done()) {
		switch (m_packedType) {
			case packed::type::standard:
				m_packedStream = std::make_shared<passthrough_packed_stream<standard_passthrough_packer>>(path_spec(), m_oplockingStream);
				break;
			case packed::type::model:
				m_packedStream = std::make_shared<passthrough_packed_stream<model_passthrough_packer>>(path_spec(), m_oplockingStream);
				break;
			case packed::type::texture:
				m_packedStream = std::make_shared<passthrough_packed_stream<texture_passthrough_packer>>(path_spec(), m_oplockingStream);
				break;
			default:
				return false;
		}
	}

	return true;
}
