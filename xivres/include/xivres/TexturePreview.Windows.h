#ifdef _WINDOWS_
#ifndef _XIVRES_INTERNAL_TEXTUREPREVIEW_WINDOWS_H_
#define _XIVRES_INTERNAL_TEXTUREPREVIEW_WINDOWS_H_

#include "TextureStream.h"

namespace xivres::util {
	void ShowTextureStream(const xivres::TextureStream& texStream, std::wstring title = L"Preview");
}

#endif
#endif
