#pragma once
#include "vkrg/common.h"
#include "vkrg/prototypes.h"

namespace vkrg
{
	class GraphPass
	{
	public:
		GraphPass(const std::string& name);

		virtual VKRG_RENDER_PASS_TYPE GetType() = 0;

		virtual const char* GetPrototypeName() = 0;

		const char* GetName()
		{
			return name.c_str();
		}

		bool IsResourcePass()
		{
			auto type = GetType();
			return type == VKRG_RP_TYPE_RESOURCE_INPUT || type == VKRG_RP_TYPE_RESOURCE_OUTPUT ||
				type == VKRG_RP_TYPE_RESOURCE_IN_OUT;
		}

		bool IsExecutablePass()
		{
			auto type = GetType();
			return type == VKRG_RP_TYPE_COMPUTE_PASS || type == VKRG_RP_TYPE_RENDER_PASS;
		}

	private:
		std::string name;
	};


	class ExecutablePass : public GraphPass
	{
	public:
		ExecutablePass(const std::string& name);

		virtual void GeneratePrototypeInfo(ExecutablePassPrototypeInfoCollector& collector) = 0;

		virtual void Execute() = 0;
	};


	class ResourcePass : public GraphPass
	{
	public:
		ResourcePass(const std::string& name, ResourceSlice slice, ResourcePassPrototypeInfo& info);

		VKRG_RENDER_PASS_TYPE GetType() { return m_info.type; }

		const char*			  GetResourceName();

		const char*			  GetPrototypeName();
		

	private:
		ResourcePassPrototypeInfo m_info;
		ResourceSlice m_slice;
	};

	template<typename T>
	struct ExecutablePassReflectionPrototypeName
	{
		constexpr static const char* name = "unknown";
	};

	using ExecutablePassPassPrototypeCallback = std::function<ptr<ExecutablePass>(const std::string&)>;

	struct ExecutablePassPassFactory
	{
		static opt<ptr<ExecutablePass>> CreateGraphPass(const std::string& prototype, const std::string& name);

		static void RegisterPrototypeFunction(const std::string& name,ExecutablePassPassPrototypeCallback callback);

		template<typename TPass>
		static void RegisterPrototype(ExecutablePassPassPrototypeCallback callback)
		{
			static_assert(std::is_base_of<ExecutablePass, TPass>::value, "registered render pass must be derived from RenderPass class");
			RegisterPrototypeFunction(ExecutablePassReflectionPrototypeName<TPass>::name, callback);
		}
	};

	template<typename TPass>
	struct ExecutablePassCallbackRegister
	{
		ExecutablePassCallbackRegister(ExecutablePassPassPrototypeCallback callback)
		{
			ExecutablePassPassFactory::RegisterPrototype<TPass>(callback);
		}
	};


#define REGISTER_EXECUTABLE_PASS_CREATE(TPass) \
vkrg::ptr<vkrg::ExecutablePass> Create##TPass##ExePass(const std::string& name) {return std::make_shared<TPass>(name);}\
static vkrg::ExecutablePassCallbackRegister<TPass> _render_pass_callback_##TPass##_register(Create##TPass##ExePass);\
const char* TPass::GetPrototypeName() {return ExecutablePassReflectionPrototypeName<TPass>::name;}

#define REGISTER_EXECUTABLE_PASS_PROTOTYPE(TPass) template<>\
	struct vkrg::ExecutablePassReflectionPrototypeName<TPass>\
	{\
		constexpr static const char* name = ""#TPass;\
	};

}
