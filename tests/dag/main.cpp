#include "vkrg/dag.h"
#include "gtest/gtest.h"

TEST(GraphIteratorTest, GraphIteratorTest0)
{
	//graph 1
	int node_vals[] = { 0, 1, 2, 3, 4, 5, 6, 7, 8};
	int in_degree[] =  {3, 2, 2, 1, 1, 2, 1, 1, 0};
	int out_degree[] = {0, 1, 1, 1, 1, 2, 3, 1, 3};

	int edges[13][2] = {
		{1, 0},
		{2, 0},
		{3, 0},
		{4, 1},
		{6, 1},
		{6, 2},
		{5, 2},
		{5, 3},
		{8, 4},
		{8, 6},
		{6, 5},
		{7, 5},
		{8, 7}
	};
	vkrg::DirectionalGraph<int> g;
	ASSERT_TRUE(g.Begin() == g.End());
	
	vkrg::DirectionalGraph<int>::NodeIterator nodes[9];
	for (uint32_t i = 0; i < _countof(node_vals); i++)
	{
		nodes[i] = g.AddNode(node_vals[i]);
	}

	for (uint32_t i = 0; i < _countof(edges); i++)  
	{
		g.AddEdge(nodes[edges[i][0]], nodes[edges[i][1]]);
		// duplicated edges doesn't effect graphs' result 
		g.AddEdge(nodes[edges[i][0]], nodes[edges[i][1]]);
	}

	vkrg::DirectionalGraph<int>::NodeIterator iter = g.Begin();
	for (uint32_t i = 0; i < _countof(node_vals); i++)
	{
		ASSERT_EQ(iter.GetOutDegree(), out_degree[i]);
		ASSERT_EQ(iter.GetInDegree(), in_degree[i]);
		ASSERT_EQ(iter.GetId(), *iter);
		iter++;
	}
	ASSERT_TRUE(iter == g.End());
	
	g.Sort();

	for (uint32_t i = 0; i < _countof(node_vals); i++)
	{
		ASSERT_EQ(*nodes[i],  node_vals[i]);
		ASSERT_EQ(*nodes[i], nodes[i].GetId());
	}
}

TEST(GraphIteratorTest, GraphIteratorTestAdjucent)
{
	//graph 1
	int node_vals[] = { 0, 1, 2, 3, 4, 5, 6, 7, 8 };
	int in_degree[] = { 3, 2, 2, 1, 1, 2, 1, 1, 0 };
	int out_degree[] = { 0, 1, 1, 1, 1, 2, 3, 1, 3 };

	int edges[13][2] = {
		{1, 0},
		{2, 0},
		{3, 0},
		{4, 1},
		{6, 1},
		{6, 2},
		{5, 2},
		{5, 3},
		{8, 4},
		{8, 6},
		{6, 5},
		{7, 5},
		{8, 7}
	};
	std::vector<std::vector<int>> in_adj
	{
		{1, 2, 3},
		{4, 6},
		{5, 6},
		{5},
		{8},
		{6, 7},
		{8},
		{8},
		{}
	};

	std::vector<std::vector<int>> out_adj
	{
		{},
		{0},
		{0},
		{0},
		{1},
		{2, 3},
		{1, 2, 5},
		{5},
		{4, 6, 7}
	};

	vkrg::DirectionalGraph<int> g;
	ASSERT_TRUE(g.Begin() == g.End());

	vkrg::DirectionalGraph<int>::NodeIterator nodes[9];
	for (uint32_t i = 0; i < _countof(node_vals); i++)
	{
		nodes[i] = g.AddNode(node_vals[i]);
	}

	for (uint32_t i = 0; i < _countof(edges); i++)
	{
		g.AddEdge(nodes[edges[i][0]], nodes[edges[i][1]]);
		// duplicated edges doesn't effect graphs' result 
		g.AddEdge(nodes[edges[i][0]], nodes[edges[i][1]]);
	}

	for (uint32_t i = 0;i < 9; i++)
	{
		std::vector<uint32_t> adjs;
		auto iter = g.IterateAdjucentIn(nodes[i]);
		for (uint32_t j = 0; j < nodes[i].GetInDegree(); j++)
		{
			adjs.push_back(*iter);
			iter++;
		}

		std::sort(adjs.begin(), adjs.end());

		ASSERT_EQ(in_adj[i].size(), adjs.size());
		for (uint32_t j = 0; j < adjs.size();j++)
		{
			ASSERT_EQ(adjs[j], in_adj[i][j]);
		}
	}

	for (uint32_t i = 0; i < 9; i++)
	{
		std::vector<uint32_t> adjs;
		auto iter = g.IterateAdjucentOut(nodes[i]);
		for (uint32_t j = 0; j < nodes[i].GetOutDegree(); j++)
		{
			adjs.push_back(*iter);
			iter++;
		}

		std::sort(adjs.begin(), adjs.end());

		ASSERT_EQ(out_adj[i].size(), adjs.size());
		for (uint32_t j = 0; j < adjs.size(); j++)
		{
			ASSERT_EQ(adjs[j], out_adj[i][j]);
		}
	}

	g.Sort();
	
	// sorting wouldn't change result
	for (uint32_t i = 0; i < 9; i++)
	{
		std::vector<uint32_t> adjs;
		auto iter = g.IterateAdjucentIn(nodes[i]);
		for (uint32_t j = 0; j < nodes[i].GetInDegree(); j++)
		{
			adjs.push_back(*iter);
			iter++;
		}

		std::sort(adjs.begin(), adjs.end());

		ASSERT_EQ(in_adj[i].size(), adjs.size());
		for (uint32_t j = 0; j < adjs.size(); j++)
		{
			ASSERT_EQ(adjs[j], in_adj[i][j]);
		}
	}

	// sorting wouldn't change result
	for (uint32_t i = 0; i < 9; i++)
	{
		std::vector<uint32_t> adjs;
		auto iter = g.IterateAdjucentOut(nodes[i]);
		for (uint32_t j = 0; j < nodes[i].GetOutDegree(); j++)
		{
			adjs.push_back(*iter);
			iter++;
		}

		std::sort(adjs.begin(), adjs.end());

		ASSERT_EQ(out_adj[i].size(), adjs.size());
		for (uint32_t j = 0; j < adjs.size(); j++)
		{
			ASSERT_EQ(adjs[j], out_adj[i][j]);
		}
	}

}

bool ValidateGraphSortingResult(vkrg::DirectionalGraph<int>& g, std::vector<std::vector<int>>& edges)
{
	uint32_t idx = 0;
	std::unordered_map<uint32_t, uint32_t> idxs;
	for (auto iter = g.Begin(); iter != g.End(); iter++)
	{
		idxs[*iter] = idx++;
	}

	for (auto& e : edges)
	{
		if (!idxs.count(e[0]) || !(idxs.count(e[1])) || idxs[e[0]] > idxs[e[1]])
		{
			return false;
		}
	}
	
	return true;
}

TEST(GraphSortTest, GraphSortTestSuccess0)
{
	//graph 1
	int node_vals[] = { 0, 1, 2, 3, 4, 5, 6, 7, 8 };

	std::vector<std::vector<int>> edges = {
		{1, 0},
		{2, 0},
		{3, 0},
		{4, 1},
		{6, 1},
		{6, 2},
		{5, 2},
		{5, 3},
		{8, 4},
		{8, 6},
		{6, 5},
		{7, 5},
		{8, 7}
	};
	vkrg::DirectionalGraph<int> g;

	vkrg::DirectionalGraph<int>::NodeIterator nodes[9];
	for (uint32_t i = 0; i < _countof(node_vals); i++)
	{
		nodes[i] = g.AddNode(node_vals[i]);
	}

	for (uint32_t i = 0; i < edges.size(); i++)
	{
		g.AddEdge(nodes[edges[i][0]], nodes[edges[i][1]]);
		// duplicated edges doesn't effect graphs' result 
		g.AddEdge(nodes[edges[i][0]], nodes[edges[i][1]]);
	}
	ASSERT_TRUE(g.Sort());

	ASSERT_TRUE(ValidateGraphSortingResult(g, edges));
}

TEST(GraphSortTest, GraphSortTestSuccess1)
{
	int node_vals[] = { 0, 1, 2, 3, 4};

	std::vector<std::vector<int>> edges = {
		{4, 3},
		{3, 2},
		{2, 1},
		{1, 0}
	};

	vkrg::DirectionalGraph<int> g;

	vkrg::DirectionalGraph<int>::NodeIterator nodes[9];
	for (uint32_t i = 0; i < _countof(node_vals); i++)
	{
		nodes[i] = g.AddNode(node_vals[i]);
	}

	for (uint32_t i = 0; i < edges.size(); i++)
	{
		g.AddEdge(nodes[edges[i][0]], nodes[edges[i][1]]);
		// duplicated edges doesn't effect graphs' result 
		g.AddEdge(nodes[edges[i][0]], nodes[edges[i][1]]);
	}
	ASSERT_TRUE(g.Sort());

	auto iter = g.Begin();
	for (uint32_t i = 0;i <= 4;i++, iter++)
	{
		ASSERT_EQ(*iter, 4 - i);
	}
}

TEST(GraphSortTest, GraphSortTestFail0)
{
	int node_vals[] = { 0, 1, 2, 3 };
	std::vector<std::vector<int>> edges =
	{
		{0, 1},
		{1, 2},
		{2, 3},
		{3, 1}
	};

	vkrg::DirectionalGraph<int> g;

	vkrg::DirectionalGraph<int>::NodeIterator nodes[9];
	for (uint32_t i = 0; i < _countof(node_vals); i++)
	{
		nodes[i] = g.AddNode(node_vals[i]);
	}

	for (uint32_t i = 0; i < edges.size(); i++)
	{
		g.AddEdge(nodes[edges[i][0]], nodes[edges[i][1]]);
		// duplicated edges doesn't effect graphs' result 
		g.AddEdge(nodes[edges[i][0]], nodes[edges[i][1]]);
	}
	ASSERT_FALSE(g.Sort());
}

TEST(GraphSortTest, GraphSortTestFail1)
{
	int node_vals[] = { 0, 1, 2, 3 };
	std::vector<std::vector<int>> edges =
	{
		{0, 1},
		{1, 2},
		{2, 3},
		{3, 0}
	};

	vkrg::DirectionalGraph<int> g;

	vkrg::DirectionalGraph<int>::NodeIterator nodes[9];
	for (uint32_t i = 0; i < _countof(node_vals); i++)
	{
		nodes[i] = g.AddNode(node_vals[i]);
	}

	for (uint32_t i = 0; i < edges.size(); i++)
	{
		g.AddEdge(nodes[edges[i][0]], nodes[edges[i][1]]);
		// duplicated edges doesn't effect graphs' result 
		g.AddEdge(nodes[edges[i][0]], nodes[edges[i][1]]);
	}
	ASSERT_FALSE(g.Sort());
}

int main() {
	testing::InitGoogleTest();
	RUN_ALL_TESTS();
}