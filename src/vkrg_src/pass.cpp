#include "vkrg/pass.h"
#include <unordered_map>

vkrg::ptr<std::unordered_map<std::string, vkrg::RenderPassPrototypeCallback>> _prototype_map;

namespace vkrg {
	

	opt<ptr<RenderPass>> RenderPassFactory::CreateRenderPass(const std::string& prototype, const std::string& name)
	{
		if (!_prototype_map->count(prototype))
		{
			return std::nullopt;
		}
		return (*_prototype_map)[prototype](name);
	}

	void vkrg::RenderPassFactory::RegisterPrototypeFunction(const std::string& name, RenderPassPrototypeCallback callback)
	{
		if (_prototype_map == nullptr)
		{
			_prototype_map = std::make_shared<std::unordered_map<std::string, RenderPassPrototypeCallback>>();
		}
		_prototype_map->insert(std::make_pair(name, callback));
	}

	RenderPass::RenderPass(const std::string& name) : name(name)
	{

	}

}
