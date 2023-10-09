#pragma once
#include <gvk.h>
#include <memory.h>
#include <string>
#include <optional>
#include <stdint.h>
#include <tuple>
#include <fstream>
#include <algorithm>

namespace vkrg 
{
	template<typename T>
	using ptr = std::shared_ptr<T>;

	template<typename T>
	using opt = std::optional<T>;

	template<typename ...Args>
	using tpl = std::tuple<Args...>;
}


#define vkrg_assert(expr) if(!(expr)) {__debugbreak();exit(-1);}

#define vkrg_fequal(a, b) abs((a) - (b)) < 1e-4