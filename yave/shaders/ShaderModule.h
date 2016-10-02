/*******************************
Copyright (C) 2013-2016 gregoire ANGERAND

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
**********************************/
#ifndef YAVE_MATERIAL_SHADERMODULE_H
#define YAVE_MATERIAL_SHADERMODULE_H

#include <yave/yave.h>
#include <yave/DeviceLinked.h>

#include "SpirVData.h"

#include <spirv-cross/spirv.hpp>

namespace yave {

class ShaderModule : NonCopyable, public DeviceLinked {

	public:
		ShaderModule() = default;

		ShaderModule(DevicePtr dptr, const SpirVData& data);

		ShaderModule(ShaderModule&& other);
		ShaderModule& operator=(ShaderModule&& other);

		~ShaderModule();

		vk::ShaderModule get_vk_shader_module() const;
		const core::Vector<ShaderResource>& shader_resources() const;
		vk::ShaderStageFlagBits shader_stage() const;

	private:
		void swap(ShaderModule& other);

		vk::ShaderModule _module;
		vk::ShaderStageFlagBits _stage;
		core::Vector<ShaderResource> _resources;

};


}

#endif // YAVE_MATERIAL_SHADERMODULE_H