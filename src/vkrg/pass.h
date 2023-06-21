#pragma once
#include "vkrg/common.h"
#include "vkrg/compiler.h"

namespace vkrg
{
	class RenderPass
	{
	public:
		RenderPass(const std::string& name);

		virtual void Compile(RenderPassPrototypeInfoCollector& collector) = 0;

		virtual void Execute() = 0;

		virtual const char* GetPrototypeName() = 0;

		const char* GetName()
		{
			return name.c_str();
		}

	private:
		std::string name;
	};

	template<typename T>
	struct RenderPassReflectionPrototypeName
	{
		constexpr static const char* name = "unknown";
	};

	using RenderPassPrototypeCallback = std::function<ptr<RenderPass>(const std::string&)>;

	struct RenderPassFactory
	{
		static opt<ptr<RenderPass>> CreateRenderPass(const std::string& prototype, const std::string& name);

		static void RegisterPrototypeFunction(const std::string& name,RenderPassPrototypeCallback callback);

		template<typename TPass>
		static void RegisterPrototype(RenderPassPrototypeCallback callback)
		{
			static_assert(std::is_base_of<RenderPass, TPass>::value, "registered render pass must be derived from RenderPass class");
			RegisterPrototypeFunction(RenderPassReflectionPrototypeName<TPass>::name, callback);
		}
	};

	template<typename TPass>
	struct RenderPassCallbackRegister
	{
		RenderPassCallbackRegister(RenderPassPrototypeCallback callback)
		{
			RenderPassFactory::RegisterPrototype<TPass>(callback);
		}
	};


#define REGISTER_RENDER_PASS_CREATE(TPass) \
vkrg::ptr<vkrg::RenderPass> Create##TPass##RenderPass(const std::string& name) {return std::make_shared<TPass>(name);}\
static vkrg::RenderPassCallbackRegister<TPass> _render_pass_callback_##TPass##_register(Create##TPass##RenderPass);\
const char* TPass::GetPrototypeName() {return RenderPassReflectionPrototypeName<TPass>::name;}

#define REGISTER_RENDER_PASS_PROTOTYPE(TPass) template<>\
	struct vkrg::RenderPassReflectionPrototypeName<TPass>\
	{\
		constexpr static const char* name = ""#TPass;\
	};

}
