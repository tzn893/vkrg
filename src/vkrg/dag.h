#pragma once
#include <vkrg/common.h>
#include <queue>
#include <unordered_set>

namespace vkrg
{
	/// <summary>
	/// A simple directional acyclic graph implementation.
	/// This class is designed to be as simple as possible.
	/// It contains minimal functions for a render graph implementation
	/// </summary>
	/// <typeparam name="T"> the element's type for nodes</typeparam>
	template<typename T>
	class DirectionalGraph
	{
		friend class DirectionalGraph::NodeIterator;

		void AddNotRepeatedElement(std::vector<uint32_t>& target, uint32_t element)
		{
			if (std::find(target.begin(), target.end(), element) != target.end())
			{
				return;
			}
			target.push_back(element);
		}

		uint32_t	FindCurrentIndex(uint32_t id)
		{
			vkrg_assert(id != invalid_id);
			return m_idToIdx[id];
		}

		constexpr static uint32_t invalid_id = 0xffffffff;

	public:

		DirectionalGraph()
		{
			m_nodes.push_back(Node{ T(), invalid_id });
		}

		/// <summary>
		/// Sort the graph by topological order, return false if sorting fails.
		/// The NodeIterator access graph's nodes by unique id, so sorting won't cause existing iterator's failure
		/// </summary>
		/// <returns> whether the sorting operation success or not</returns>
		bool		 Sort()
		{
			std::queue<uint32_t> next_nodes;
			std::vector<uint32_t> degrees(m_nodeCount);

			for (uint32_t i = 0;i < m_nodeCount; i++)
			{
				auto& adj = m_adjInList[i];
				if (adj.empty())
				{
					next_nodes.push(i);
				}
				degrees[i] = adj.size();
			}

			// cycle in graph
			if (next_nodes.empty())
			{
				return false;
			}

			uint32_t idx = 0;
			std::vector<uint32_t> new_id_to_idx(m_nodeCount);
			
			while (!next_nodes.empty())
			{
				uint32_t id = next_nodes.front();
				next_nodes.pop();
				new_id_to_idx[id] = idx++;
				
				auto& adj = m_adjOutList[id];
				for (auto adj_id : adj)
				{
					degrees[adj_id]--;
					if (degrees[adj_id] == 0)
					{
						next_nodes.push(adj_id);
					}
				}
			}

			// cycle in graph
			if (idx != m_nodeCount)
			{
				return false;
			}

			std::vector<Node>	new_node_list(m_nodeCount + 1);
			for (uint32_t id = 0;id < m_nodeCount;id++)
			{
				uint32_t old_idx = m_idToIdx[id];
				uint32_t new_idx = new_id_to_idx[id];
				new_node_list[new_idx] = m_nodes[old_idx];
			}
			new_node_list[m_nodeCount] = Node{ T(), invalid_id };

			m_nodes = new_node_list;
			m_idToIdx = new_id_to_idx;

			return true;
		}


		bool		 SortByPriority(std::function<bool(const T& lhs, const T& rhs)> _cmp)
		{
			auto cmp = [&](uint32_t lhs, uint32_t rhs)
			{
				uint32_t idx_lhs = m_idToIdx[lhs];
				uint32_t idx_rhs = m_idToIdx[rhs];

				return _cmp(m_nodes[idx_lhs].val, m_nodes[idx_rhs].val);
			};

			std::priority_queue<uint32_t, std::vector<uint32_t>, decltype(cmp)> next_nodes(cmp);
			std::vector<uint32_t> degrees(m_nodeCount);

			for (uint32_t i = 0; i < m_nodeCount; i++)
			{
				auto& adj = m_adjInList[i];
				if (adj.empty())
				{
					next_nodes.push(i);
				}
				degrees[i] = adj.size();
			}

			// cycle in graph
			if (next_nodes.empty())
			{
				return false;
			}

			uint32_t idx = 0;
			std::vector<uint32_t> new_id_to_idx(m_nodeCount);

			while (!next_nodes.empty())
			{
				uint32_t id = next_nodes.top();
				next_nodes.pop();
				new_id_to_idx[id] = idx++;

				auto& adj = m_adjOutList[id];
				for (auto adj_id : adj)
				{
					degrees[adj_id]--;
					if (degrees[adj_id] == 0)
					{
						next_nodes.push(adj_id);
					}
				}
			}

			// cycle in graph
			if (idx != m_nodeCount)
			{
				return false;
			}

			std::vector<Node>	new_node_list(m_nodeCount + 1);
			for (uint32_t id = 0; id < m_nodeCount; id++)
			{
				uint32_t old_idx = m_idToIdx[id];
				uint32_t new_idx = new_id_to_idx[id];
				new_node_list[new_idx] = m_nodes[old_idx];
			}
			new_node_list[m_nodeCount] = Node{ T(), invalid_id };

			m_nodes = new_node_list;
			m_idToIdx = new_id_to_idx;

			return true;
		}

		/// <summary>
		/// return node count in the graph
		/// </summary>
		/// <returns> count of the nodes</returns>
		uint32_t	 NodeCount()
		{
			return m_nodeCount;
		}

		class NodeIterator
		{
			friend class DirectionalGraph;
		public:
			NodeIterator()
			{
				graph = NULL;
				id = invalid_id;
			}

			NodeIterator(DirectionalGraph* graph, uint32_t id)
			{
				this->graph = graph;
				this->id = id;
			}

			NodeIterator(const NodeIterator& iter)
			{
				this->graph = iter.graph;
				this->id = iter.id;
			}

			NodeIterator(NodeIterator&& iter)
			{
				std::swap(iter.graph, graph);
				std::swap(iter.id, id);
			}

			NodeIterator& operator=(const NodeIterator& iter)
			{
				this->graph = iter.graph;
				this->id = iter.id;
				return *this;
			}

			NodeIterator& operator=(NodeIterator&& iter)
			{
				std::swap(this->graph, iter.graph);
				std::swap(this->id, iter.id);
				return *this;
			}

			T& operator*()
			{
				return graph->m_nodes[graph->FindCurrentIndex(id)].val;
			}

			const T& operator*() const
			{
				return const_cast<NodeIterator<T>*>(this)->operator*();
			}

			T* operator->()
			{
				return &graph->m_nodes[graph->FindCurrentIndex(id)].val;
			}

			const T* operator->() const
			{
				return const_cast<NodeIterator<T>*>(this)->operator->();
			}

			NodeIterator& operator++()
			{
				uint32_t idx = graph->FindCurrentIndex(id);

				uint32_t next_id = graph->m_nodes[idx + 1].id;
				id = next_id;

				return *this;
			}

			NodeIterator& operator--() 
			{
				uint32_t idx = graph->FindCurrentIndex(id);
				gvk_assert(idx != 0);

				uint32_t next_id = graph->m_nodes[idx - 1].id;
				id = next_id;
				return *this;
			}

			NodeIterator operator++(int)
			{
				return this->operator++();
			}

			NodeIterator operator--(int)
			{
				return this->operator--();
			}

			bool		 operator<(const NodeIterator& iter) const
			{
				return graph->FindCurrentIndex(id) < graph->FindCurrentIndex(iter.id);
			}

			bool		 operator>(const NodeIterator& iter) const
			{
				return !this->operator<(iter);
			}

			bool operator==(const NodeIterator& rhs) const
			{
				return rhs.graph == graph && rhs.id == id;
			}

			bool operator!=(const NodeIterator& lhs) const
			{
				return !this->operator==(lhs);
			}

			int32_t operator-(const NodeIterator& other) const
			{
				return FindCurrentIndex(id) - FindCurrentIndex(other.id);
			}

			int32_t DistanceFromBegin() const
			{
				return this->operator-(graph->Begin());
			}

			uint32_t GetId() const
			{
				return id;
			}

			bool Invalid() const
			{
				return id == invalid_id;
			}

			uint32_t GetInDegree() const
			{
				vkrg_assert(!Invalid());
				return graph->m_adjInList[id].size();
			}

			uint32_t GetOutDegree() const
			{
				vkrg_assert(!Invalid());
				return graph->m_adjOutList[id].size();
			}

		private:
			DirectionalGraph* graph;
			uint32_t id;
		};

		class NodeAdjucentIterator
		{
		public:
			NodeAdjucentIterator()
			{
				graph = NULL;
				p_id = NULL;
				begin = NULL;
				end = NULL;
			}

			NodeAdjucentIterator(DirectionalGraph* graph, uint32_t* begin, uint32_t* end)
			{
				this->graph = graph;
				p_id = begin;
				this->begin = begin;
				this->end = end;
			}

			NodeAdjucentIterator(const NodeAdjucentIterator& iter)
			{
				this->graph = iter.graph;
				this->p_id = iter.p_id;
				this->begin = iter.begin;
				this->end = iter.end;
			}

			NodeAdjucentIterator(NodeAdjucentIterator&& iter)
			{
				std::swap(iter.graph, graph);
				std::swap(iter.p_id, p_id);
				std::swap(iter.begin, begin);
				std::swap(iter.end, end);
			}

			NodeAdjucentIterator& operator=(const NodeAdjucentIterator& iter)
			{
				this->graph = iter.graph;
				this->p_id = iter.p_id;
				this->begin = iter.begin;
				this->end = iter.end;

				return *this;
			}

			NodeAdjucentIterator& operator=(NodeAdjucentIterator&& iter)
			{
				std::swap(iter.graph, graph);
				std::swap(iter.p_id, p_id);
				std::swap(iter.begin, begin);
				std::swap(iter.end, end);

				return *this;
			}

			T& operator*()
			{
				vkrg_assert(p_id != end);
				return graph->m_nodes[graph->FindCurrentIndex(*p_id)].val;
			}

			const T& operator*() const
			{
				return const_cast<NodeIterator<T>*>(this)->operator*();
			}

			NodeAdjucentIterator& operator++()
			{
				gvk_assert(p_id != end);
				p_id++;
				return *this;
			}

			NodeAdjucentIterator& operator--()
			{
				gvk_assert(p_id != begin);
				p_id--;
				return *this;
			}

			NodeAdjucentIterator operator++(int)
			{
				return this->operator++();
			}

			NodeAdjucentIterator operator--(int)
			{
				return this->operator--();
			}

			bool operator==(const NodeIterator& rhs) const
			{
				return rhs.graph == graph && rhs.p_id == p_id && rhs.begin == begin && rhs.end == end;
			}

			bool operator!=(const NodeIterator& lhs) const
			{
				return !this->operator==(lhs);
			}

			uint32_t GetId()
			{
				vkrg_assert(p_id != end);
				return *p_id;
			}

			bool IsEnd()
			{
				return p_id == end;
			}

			bool IsBegin()
			{
				return p_id == begin;
			}

			NodeIterator CaseToNode()
			{
				vkrg_assert(p_id != end);
				return NodeIterator(graph, *p_id);
			}

		private:
			uint32_t* p_id;
			uint32_t* begin;
			uint32_t* end;
			DirectionalGraph* graph;
		};

		/// <summary>
		/// Get the iterator point to the beginning of graph's node list
		/// </summary>
		/// <returns> iterator point to the beginning</returns>
		NodeIterator Begin()
		{
			if (m_nodeCount == 0)
			{
				return NodeIterator(this, invalid_id);
			}
			return NodeIterator(this, m_nodes[0].id);
		}

		/// <summary>
		/// Get the iterator point to the end of graph's node list
		/// </summary>
		/// <returns> iterator point to the end</returns>
		NodeIterator End()
		{
			return NodeIterator(this, invalid_id);
		}

		/// <summary>
		/// add a directional edge between two existing node in graph
		/// </summary>
		/// <param name="out"> the out node's iterator</param>
		/// <param name="in"> the in node's iterator</param>
		void		 AddEdge(const NodeIterator& out, const NodeIterator& in)
		{
			vkrg_assert(out.id < m_nodeCount);
			vkrg_assert(in.id < m_nodeCount);
			vkrg_assert(out.graph == this);
			vkrg_assert(in.graph == this);
			AddNotRepeatedElement(m_adjOutList[out.id], in.id);
			AddNotRepeatedElement(m_adjInList[in.id], out.id);
		}

		NodeAdjucentIterator IterateAdjucentIn(const NodeIterator& node)
		{
			vkrg_assert(node.id <  m_nodeCount);
			auto& adj = m_adjInList[node.id];
			
			uint32_t* begin = adj.data();
			uint32_t* end = adj.data() + adj.size();
			return NodeAdjucentIterator(this, begin, end);
		}

		NodeAdjucentIterator IterateAdjucentOut(const NodeIterator& node)
		{
			vkrg_assert(node.id < m_nodeCount);
			auto& adj = m_adjOutList[node.id];

			uint32_t* begin = adj.data();
			uint32_t* end = adj.data() + adj.size();
			return NodeAdjucentIterator(this, begin, end);
		}

		/// <summary>
		/// add a element to graph's node list
		/// </summary>
		/// <param name="node"> the element to add</param>
		/// <returns> node iterator point to the new node</returns>
		NodeIterator AddNode(const T& node)
		{
			Node n;
			n.val = node;
			n.id = m_nodeCount;
			m_nodes.insert(m_nodes.end() - 1, n);
			// m_idToIdx.push_back(n.id);
			m_idToIdx.insert(m_idToIdx.end(), n.id);

			m_adjOutList.push_back(std::vector<uint32_t>{});
			m_adjInList.push_back(std::vector<uint32_t>{});

			m_nodeCount = m_nodeCount + 1;

			return NodeIterator(this, n.id);
		}

		/// <summary>
		/// return wether a visiter start from 'from' node reach the 'to' node
		/// </summary>
		/// <param name="from">node visiter start</param>
		/// <param name="to">node visiter end</param>
		/// <returns>wether this visiter could reach</returns>
		bool  CanReach(const NodeIterator& from, const NodeIterator& to)
		{
			vkrg_assert(from.graph == this);
			vkrg_assert(to.graph == this);

			std::unordered_set<uint32_t> visited;
			return __CanReachRecursive(from, to, visited);
		}

		bool __CanReachRecursive(const NodeIterator& from, const NodeIterator& to, std::unordered_set<uint32_t>& visited)
		{
			visited.insert(from.GetId());
			if (from == to) return true;

			NodeAdjucentIterator outFrom = IterateAdjucentOut(from);

			while (!outFrom.IsEnd())
			{
				if (!visited.count(outFrom.GetId()))
				{
					if (__CanReachRecursive(outFrom.CaseToNode(), to, visited))
					{
						return true;
					}
				}

				outFrom++;
			}
			return false;
		}

		/// <summary>
		/// clear all added nodes and edges from graph
		/// </summary>
		void Clear()
		{
			m_nodes.clear();
			m_idToIdx.clear();
			m_adjOutList.clear();
			m_adjInList.clear();
			m_nodeCount = 0;
		}

	private:
		struct Node
		{
			T val;
			uint32_t id;
		};
		
		std::vector<Node> m_nodes;
		std::vector<uint32_t> m_idToIdx;
		std::vector<std::vector<uint32_t>> m_adjOutList;
		std::vector<std::vector<uint32_t>> m_adjInList;

		uint32_t m_nodeCount = 0;
	};


	
}

