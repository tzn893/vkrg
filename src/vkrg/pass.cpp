#include "vkrg/pass.h"
#include <unordered_map>

vkrg::ptr<std::unordered_map<std::string, vkrg::ExecutablePassPassPrototypeCallback>> _prototype_map;

namespace vkrg {
	

	opt<ptr<ExecutablePass>> ExecutablePassPassFactory::CreateGraphPass(const std::string& prototype, const std::string& name)
	{
		if (!_prototype_map->count(prototype))
		{
			return std::nullopt;
		}
		return (*_prototype_map)[prototype](name);
	}

	void vkrg::ExecutablePassPassFactory::RegisterPrototypeFunction(const std::string& name, ExecutablePassPassPrototypeCallback callback)
	{
		if (_prototype_map == nullptr)
		{
			_prototype_map = std::make_shared<std::unordered_map<std::string, ExecutablePassPassPrototypeCallback>>();
		}
		_prototype_map->insert(std::make_pair(name, callback));
	}

	ExecutablePass::ExecutablePass(const std::string& name) : GraphPass(name) {}

	ResourcePass::ResourcePass(const std::string& name, ResourceSlice slice, ResourcePassPrototypeInfo& info)
		: GraphPass(name), m_info(info), m_slice(slice)
	{}

	const char* ResourcePass::GetResourceName()
	{
		return m_info.resource.c_str();
	}

	const char* ResourcePass::GetPrototypeName()
	{
		return m_info.prototype.c_str();
	}

	GraphPass::GraphPass(const std::string& name) : name(name) {}

}
