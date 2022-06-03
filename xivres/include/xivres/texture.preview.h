#ifdef _WINDOWS_
#ifndef XIVRES_INTERNAL_TEXTUREPREVIEW_WINDOWS_H_
#define XIVRES_INTERNAL_TEXTUREPREVIEW_WINDOWS_H_

#include "texture.stream.h"

namespace xivres::texture {
	void preview(const xivres::texture::stream& texStream, std::wstring title = L"Preview");
}

#endif
#endif
