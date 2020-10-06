// Copyright (C) 2018-2020 - DevSH Graphics Programming Sp. z O.O.
// This file is part of the "Nabla Engine".
// For conditions of distribution and use, see copyright notice in nabla.h

#ifndef __NBL_VIDEO_C_DERIVATIVE_MAP_CREATOR_H_INCLUDED__
#define __NBL_VIDEO_C_DERIVATIVE_MAP_CREATOR_H_INCLUDED__

#include "irr/core/IReferenceCounted.h"

namespace irr
{
namespace video
{

class IVideoDriver;
class IRenderableVirtualTexture;

class CDerivativeMapCreator final : public core::IReferenceCounted
{
	protected:
		~CDerivativeMapCreator();

	public:
		CDerivativeMapCreator(video::IVideoDriver* _driver);
		core::smart_refctd_ptr<video::IGPUImageView> createDerivMapFromBumpMap(video::IGPUImageView* _bumpMap, float _heightFactor, bool _texWrapRepeat = false) const;

	private:
		uint32_t createComputeShader(const char*) const;

		uint32_t m_deriv_map_gen_cs;
		uint32_t m_bumpMapSampler;
		video::IVideoDriver* m_driver;
};

}
}

#endif//__IRR_C_DERIVATIVE_MAP_CREATOR_H_INCLUDED__