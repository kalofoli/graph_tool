// graph-tool -- a general graph modification and manipulation thingy
//
// Copyright (C) 2006-2016 Tiago de Paula Peixoto <tiago@skewed.de>
//
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either version 3
// of the License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program. If not, see <http://www.gnu.org/licenses/>.

#ifndef GRAPH_BLOCKMODEL_OVERLAP_HH
#define GRAPH_BLOCKMODEL_OVERLAP_HH

#include "config.h"
#include <tuple>

#include "graph_state.hh"
#include "graph_blockmodel_util.hh"
#include "graph_blockmodel_overlap_util.hh"

namespace graph_tool
{

using namespace boost;
using namespace std;

typedef vprop_map_t<int32_t>::type vmap_t;
typedef eprop_map_t<int32_t>::type emap_t;

typedef vprop_map_t<int64_t>::type vimap_t;
typedef vprop_map_t<vector<int64_t>>::type vvmap_t;

typedef mpl::vector2<std::true_type, std::false_type> use_hash_tr;

#define OVERLAP_BLOCK_STATE_params                                             \
    ((g, &, never_filtered_never_reversed, 1))                                 \
    ((use_hash,, use_hash_tr, 1))                                              \
    ((_abg, &, boost::any&, 0))                                                \
    ((node_index,, vimap_t, 0))                                                \
    ((half_edges,, vvmap_t, 0))                                                \
    ((mrs,, emap_t, 0))                                                        \
    ((mrp,, vmap_t, 0))                                                        \
    ((mrm,, vmap_t, 0))                                                        \
    ((wr,, vmap_t, 0))                                                         \
    ((b,, vmap_t, 0))                                                          \
    ((bclabel,, vmap_t, 0))                                                    \
    ((pclabel,, vmap_t, 0))                                                    \
    ((deg_corr,, bool, 0))

GEN_STATE_BASE(OverlapBlockStateBase, OVERLAP_BLOCK_STATE_params)

template <class... Ts>
class OverlapBlockState
    : public OverlapBlockStateBase<Ts...>
{
public:
    GET_PARAMS_USING(OverlapBlockStateBase<Ts...>, OVERLAP_BLOCK_STATE_params)
    GET_PARAMS_TYPEDEF(Ts, OVERLAP_BLOCK_STATE_params)

    template <class RNG, class... ATs,
              typename std::enable_if_t<sizeof...(ATs) == sizeof...(Ts)>* = nullptr>
    OverlapBlockState(RNG& rng, ATs&&... args)
        : OverlapBlockStateBase<Ts...>(std::forward<ATs>(args)...),
          _bg(boost::any_cast<std::reference_wrapper<bg_t>>(__abg)),
          _c_mrs(_mrs.get_checked()),
          _emat(_g, _b, _bg, rng),
          _overlap_stats(_g, _b, _half_edges, _node_index, num_vertices(_bg))
    {
        for (auto r : vertices_range(_bg))
            _wr[r] = _overlap_stats.get_block_size(r);
    }

    OverlapBlockState(const OverlapBlockState& other)
        : OverlapBlockStateBase<Ts...>
             (static_cast<const OverlapBlockStateBase<Ts...>&>(other)),
          _bg(other._bg),
          _c_mrs(other._c_mrs),
          _emat(other._emat),
          _overlap_stats(other._overlap_stats)
    {
        if (other.is_partition_stats_enabled())
            enable_partition_stats();
    }

    void remove_vertex(size_t v)
    {
        size_t r = _b[v];

        auto u = _overlap_stats.get_out_neighbour(v);
        if (u != _overlap_stats._null)
        {
            size_t s = _b[u];

            auto e = *out_edges(v, _g).first;
            auto& me = _emat.get_bedge(e);

            _mrs[me] -= 1;

            assert(_mrs[me] >= 0);

            _mrp[r] -= 1;
            _mrm[s] -= 1;

            if (_mrs[me] == 0)
                _emat.remove_me(r, s, me, _bg);
        }

        u = _overlap_stats.get_in_neighbour(v);
        if (u != _overlap_stats._null)
        {
            size_t s = _b[u];

            auto e = *in_edge_iteratorS<g_t>().get_edges(v, _g).first;
            auto& me = _emat.get_bedge(e);

            _mrs[me] -= 1;

            _mrp[s] -= 1;
            _mrm[r] -= 1;

            if (_mrs[me] == 0)
                _emat.remove_me(s, r, me, _bg);
        }

        _overlap_stats.remove_half_edge(v, r, _b, _g);
        _wr[r] = _overlap_stats.get_block_size(r);

        if (!_egroups.empty())
            _egroups.remove_vertex(v, size_t(r), _g);
    }

    void add_vertex(size_t v, size_t r)
    {
        typedef typename graph_traits<bg_t>::edge_descriptor bedge_t;

        auto u = _overlap_stats.get_out_neighbour(v);
        if (u != _overlap_stats._null)
        {
            size_t s = r;
            if (u != v)
                s = _b[u];

            auto me = _emat.get_me(r, s);

            if (me == bedge_t())
            {
                me = add_edge(r, s, _bg).first;
                _emat.put_me(r, s, me);
                _c_mrs[me] = 0;
            }

            auto e = *out_edges(v, _g).first;
            _emat.get_bedge(e) = me;

            assert(me == _emat.get_me(r, s));

            _mrs[me] += 1;
            _mrp[r] += 1;
            _mrm[s] += 1;
        }

        u = _overlap_stats.get_in_neighbour(v);
        if (u != _overlap_stats._null)
        {
            size_t s = _b[u];

            auto me = _emat.get_me(s, r);

            if (me == bedge_t())
            {
                me = add_edge(s, r, _bg).first;
                _emat.put_me(s, r, me);
                _c_mrs[me] = 0;
            }

            auto e = *in_edge_iteratorS<g_t>().get_edges(v, _g).first;
            _emat.get_bedge(e) = me;

            assert(me == _emat.get_me(s, r));

            _mrs[me] += 1;

            _mrp[s] += 1;
            _mrm[r] += 1;
        }

        _overlap_stats.add_half_edge(v, r, _b, _g);
        _wr[r] = _overlap_stats.get_block_size(r);

        _b[v] = r;

        if (!_egroups.empty())
            _egroups.add_vertex(v, r, _eweight, _g);
    }

    // move a vertex from its current block to block nr
    void move_vertex(size_t v, size_t nr)
    {
        size_t r = _b[v];
        if (r == nr)
            return;

        if (_bclabel[r] != _bclabel[nr])
            throw ValueException("cannot move vertex across clabel barriers");

        if (is_partition_stats_enabled())
            get_partition_stats(v).move_vertex(v, _b[v], nr, _deg_corr, _g);

        remove_vertex(v);
        add_vertex(v, nr);
    }

    template <class VMap>
    void set_partition(VMap&& b)
    {
        for (auto v : vertices_range(_g))
            move_vertex(v, b[v]);
    }

    void set_partition(boost::any& ab)
    {
        vmap_t& b = boost::any_cast<vmap_t&>(ab);
        set_partition<typename vmap_t::unchecked_t>(b.get_unchecked());
    }

    size_t virtual_remove_size(size_t v)
    {
        return _overlap_stats.virtual_remove_size(v, _b[v]);
    }

    // compute the entropy difference of a virtual move of vertex from block r to nr
    template <class MEntries>
    double virtual_move_sparse(size_t v, size_t nr, bool multigraph,
                               MEntries& m_entries) const
    {
        size_t r = _b[v];

        if (r == nr)
            return 0.;

        m_entries.clear();
        move_entries(v, nr, _b, _eweight, _mrs, _emat.get_bedge_map(), _g, _bg,
                     m_entries);

        size_t kout = out_degreeS()(v, _g);
        size_t kin = 0;
        if (is_directed::apply<g_t>::type::value)
            kin = in_degreeS()(v, _g);

        double dS = entries_dS(m_entries, _mrs, _emat, _bg);

        int dwr = _wr[r] - _overlap_stats.virtual_remove_size(v, r, kin, kout);
        int dwnr = _overlap_stats.virtual_add_size(v, nr) - _wr[nr];

        if (_deg_corr)
            dS += _overlap_stats.virtual_move_dS(v, r, nr, _g, kin, kout);

        if (multigraph)
            dS += _overlap_stats.virtual_move_parallel_dS(v, r, nr, _b, _g);

        if (!is_directed::apply<g_t>::type::value)
            kin = kout;

        dS += vterm(_mrp[r]  - kout, _mrm[r]  - kin, _wr[r]  - dwr , _deg_corr, _bg);
        dS += vterm(_mrp[nr] + kout, _mrm[nr] + kin, _wr[nr] + dwnr, _deg_corr, _bg);
        dS -= vterm(_mrp[r]        , _mrm[r]       , _wr[r]        , _deg_corr, _bg);
        dS -= vterm(_mrp[nr]       , _mrm[nr]      , _wr[nr]       , _deg_corr, _bg);

        return dS;
    }

    double virtual_move_sparse(size_t v, size_t nr, bool multigraph)
    {
        return virtual_move_sparse(v, nr, multigraph, _m_entries);
    }

    template <class MEntries>
    double virtual_move_dense(size_t, size_t, bool, MEntries&) const
    {
        throw GraphException("Dense entropy for overlapping model not implemented!");
    }

    double virtual_move_dense(size_t v, size_t nr, bool multigraph)
    {
        return virtual_move_dense(v, nr, multigraph, _m_entries);
    }

    template <class MEntries>
    double virtual_move(size_t v, size_t nr, bool dense, bool multigraph,
                        bool partition_dl, bool deg_dl, bool edges_dl,
                        MEntries& m_entries)
    {
        size_t r = _b[v];

        if (_bclabel[r] != _bclabel[nr])
            return std::numeric_limits<double>::infinity();

        double dS;
        if (dense)
            dS = virtual_move_dense(v, nr, multigraph, m_entries);
        else
            dS = virtual_move_sparse(v, nr, multigraph, m_entries);

        if (partition_dl || deg_dl || edges_dl)
        {
            enable_partition_stats();
            auto& ps = get_partition_stats(v);
            if (partition_dl)
                dS += ps.get_delta_dl(v, r, nr, _g);
            if (_deg_corr && deg_dl)
                dS += ps.get_delta_deg_dl(v, r, nr, _eweight, _g);
            if (edges_dl)
                dS += ps.get_delta_edges_dl(v, r, nr, _g);
        }

        return dS;
    }

    double virtual_move(size_t v, size_t nr, bool dense, bool multigraph,
                        bool partition_dl, bool deg_dl, bool edges_dl)
    {
        return virtual_move(v, nr, dense, multigraph, partition_dl, deg_dl,
                            edges_dl, _m_entries);
    }

    double get_delta_dl(size_t v, size_t nr)
    {
        enable_partition_stats();
        return get_partition_stats(v).get_delta_dl(v, _b[v], nr, _g);
    }

    // Sample node placement
    template <class RNG>
    size_t sample_block(size_t v, double c, const vector<size_t>& block_list,
                        RNG& rng)
    {
        // attempt random block
        size_t s = uniform_sample(block_list, rng);

        if (!std::isinf(c))
        {
            size_t w = get_lateral_half_edge(v, rng);

            size_t u = _overlap_stats.get_out_neighbour(w);
            if (u >= num_vertices(_g))
                u = _overlap_stats.get_in_neighbour(w);

            size_t t = _b[u];
            double p_rand = 0;
            if (c > 0)
            {
                size_t B = num_vertices(_bg);
                if (is_directed::apply<g_t>::type::value)
                    p_rand = c * B / double(_mrp[t] + _mrm[t] + c * B);
                else
                    p_rand = c * B / double(_mrp[t] + c * B);
            }

            typedef std::uniform_real_distribution<> rdist_t;
            if (c == 0 || rdist_t()(rng) >= p_rand)
            {
                if (_egroups.empty())
                    _egroups.init(_b, _eweight, _g, _bg);
                const auto& e = _egroups.sample_edge(t, rng);
                s = _b[target(e, _g)];
                if (s == t)
                    s = _b[source(e, _g)];
            }
        }

        return s;
    }

    size_t sample_block(size_t v, double c, vector<size_t>& block_list,
                        rng_t& rng)
    {
        return sample_block<rng_t>(v, c, block_list, rng);
    }

    template <class RNG>
    size_t get_lateral_half_edge(size_t v, RNG& rng)
    {
        size_t vv = _overlap_stats.get_node(v);
        size_t w = _overlap_stats.sample_half_edge(vv, rng);
        return w;
    }

    template <class RNG>
    size_t random_neighbour(size_t v,  RNG& rng)
    {
        size_t w = get_lateral_half_edge(v, _overlap_stats, rng);

        size_t u = _overlap_stats.get_out_neighbour(w);
        if (u >= num_vertices(_g))
            u = _overlap_stats.get_in_neighbour(w);
        return u;
    }

    // Computes the move proposal probability
    template <class MEntries>
    double get_move_prob(size_t v, size_t r, size_t s, double c, bool reverse,
                         MEntries& m_entries)
    {
        typedef typename graph_traits<g_t>::vertex_descriptor vertex_t;
        size_t B = num_vertices(_bg);
        double p = 0;
        size_t w = 0;

        size_t kout = out_degreeS()(v, _g, _eweight);
        size_t kin = kout;
        if (is_directed::apply<g_t>::type::value)
            kin = in_degreeS()(v, _g, _eweight);

        size_t vi = _overlap_stats.get_node(v);
        auto& ns = _overlap_stats.get_half_edges(vi);

        for (size_t v: ns)
        {
            for (auto e : all_edges_range(v, _g))
            {
                vertex_t u = target(e, _g);
                if (is_directed::apply<g_t>::type::value && u == v)
                    u = source(e, _g);
                vertex_t t = _b[u];
                if (u == v)
                    t = r;
                size_t ew = _eweight[e];
                w += ew;

                int mts;
                if (t == r && s == size_t(_b[u]))
                    mts = _mrs[_emat.get_bedge(e)];
                else
                    mts = get_mrs(t, s, _mrs, _emat);
                int mtp = _mrp[t];
                int mst = mts;
                int mtm = mtp;

                if (is_directed::apply<g_t>::type::value)
                {
                    mst = get_mrs(s, t, _mrs, _emat);
                    mtm = _mrm[t];
                }

                if (reverse)
                {
                    int dts = m_entries.get_delta(t, s);
                    int dst = dts;
                    if (is_directed::apply<g_t>::type::value)
                        dst = m_entries.get_delta(s, t);

                    mts += dts;
                    mst += dst;

                    if (t == s)
                    {
                        mtp -= kout;
                        mtm -= kin;
                    }

                    if (t == r)
                    {
                        mtp += kout;
                        mtm += kin;
                    }
                }

                if (is_directed::apply<g_t>::type::value)
                {
                    p += ew * ((mts + mst + c) / (mtp + mtm + c * B));
                }
                else
                {
                    if (t == s)
                        mts *= 2;
                    p += ew * (mts + c) / (mtp + c * B);
                }
            }
        }
        if (w > 0)
            return p / w;
        else
            return 1. / B;
    }

    double get_move_prob(size_t v, size_t r, size_t s, double c, bool reverse)
    {
        return get_move_prob(v, r, s, c, reverse, _m_entries);
    }

    bool is_last(size_t v)
    {
        auto r = _b[v];
        return _overlap_stats.virtual_remove_size(v, r) == 0;
    }

    size_t node_weight(size_t)
    {
        return 1;
    }

    double sparse_entropy(bool multigraph, bool deg_entropy) const
    {
        double S = 0;
        for (auto e : edges_range(_bg))
            S += eterm(source(e, _bg), target(e, _bg), _mrs[e], _bg);
        for (auto v : vertices_range(_bg))
            S += vterm(_mrp[v], _mrm[v], _wr[v], _deg_corr, _bg);

        if (_deg_corr && deg_entropy)
        {
            typedef gt_hash_map<int, int> map_t;

            map_t in_hist, out_hist;
            size_t N = _overlap_stats.get_N();

            for (size_t v = 0; v < N; ++v)
            {
                in_hist.clear();
                out_hist.clear();

                const auto& half_edges = _overlap_stats.get_half_edges(v);
                for (size_t u : half_edges)
                {
                    in_hist[_b[u]] += in_degreeS()(u, _g);
                    out_hist[_b[u]] += out_degree(u, _g);
                }

                for (auto& k_c : in_hist)
                    S -= lgamma_fast(k_c.second + 1);
                for (auto& k_c : out_hist)
                    S -= lgamma_fast(k_c.second + 1);
            }
        }

        if (multigraph)
        {
            for(const auto& h : _overlap_stats.get_parallel_bundles())
            {
                for (const auto& kc : h)
                    S += lgamma_fast(kc.second + 1);
            }
        }
        return S;
    }

    double dense_entropy(bool)
    {
        throw GraphException("Dense entropy for overlapping model not implemented!");
    }

    double entropy(bool dense, bool multigraph, bool deg_entropy)
    {
        if (dense)
            return dense_entropy(multigraph);
        else
            return sparse_entropy(multigraph, deg_entropy);
    }

    double get_partition_dl()
    {
        if (!is_partition_stats_enabled())
            enable_partition_stats();
        double S = 0;
        for (auto& ps : _partition_stats)
            S += ps.get_partition_dl();
        return S;
    }

    double get_deg_dl(bool ent, bool dl_alt, bool xi_fast)
    {
        if (!is_partition_stats_enabled())
            enable_partition_stats();
        double S = 0;
        for (auto& ps : _partition_stats)
            S += ps.get_deg_dl(ent, dl_alt, xi_fast);
        return S;
    }

    double get_parallel_entropy()
    {
        double S = 0;
        for(auto& h : _overlap_stats.get_parallel_bundles())
        {
            for (auto& kc : h)
                S += lgamma_fast(kc.second + 1);
        }
        return S;
    }

    void enable_partition_stats()
    {
        if (_partition_stats.empty())
        {

            size_t E = num_vertices(_g) / 2;
            size_t B = 0;
            for (auto r : vertices_range(_bg))
                if (_wr[r] > 0)
                    B++;

            auto vi = std::max_element(vertices(_g).first, vertices(_g).second,
                                       [&](auto u, auto v)
                                       { return this->_pclabel[u] < this->_pclabel[v];});
            size_t C = _bclabel[*vi] + 1;

            vector<gt_hash_set<size_t>> vcs(C);
            vector<size_t> rc(num_vertices(_bg));
            for (auto v : vertices_range(_g))
            {
                vcs[_pclabel[v]].insert(_overlap_stats.get_node(v));
                rc[_b[v]] = _pclabel[v];
            }

            for (size_t c = 0; c < C; ++c)
                _partition_stats.emplace_back(_g, _b, vcs[c], E, B,
                                              _eweight, _overlap_stats,
                                              _bmap, _vmap);

            for (size_t r = 0; r < num_vertices(_bg); ++r)
                _partition_stats[rc[r]].get_r(r);
        }
    }

    void disable_partition_stats()
    {
        _partition_stats.clear();
    }

    bool is_partition_stats_enabled() const
    {
        return !_partition_stats.empty();
    }

    overlap_partition_stats_t& get_partition_stats(size_t v)
    {
        return _partition_stats[_pclabel[v]];
    }

    template <class Graph, class EMap>
    void get_be_overlap(Graph& g, EMap be)
    {
        for (auto ei : edges_range(_g))
        {
            auto u = source(ei, _g);
            auto v = target(ei, _g);

            auto s = vertex(_node_index[u], g);
            auto t = vertex(_node_index[v], g);

            for (auto e : out_edges_range(s, g))
            {
                if (!be[e].empty() || target(e, g) != t)
                    continue;
                if (is_directed::apply<Graph>::type::value || s < target(e, g))
                    be[e] = {_b[u], _b[v]};
                else
                    be[e] = {_b[v], _b[u]};
                break;
            }

            for (auto e : in_edges_range(t, g))
            {
                if (!be[e].empty() || source(e, g) != s)
                    continue;
                be[e] = {_b[u], _b[v]};
                break;
            }
        }
    }

    template <class Graph, class VMap>
    void get_bv_overlap(Graph& g, VMap bv, VMap bc_in, VMap bc_out,
                        VMap bc_total)
    {
        typedef gt_hash_map<int, int> map_t;
        vector<map_t> hist_in;
        vector<map_t> hist_out;

        for (auto v : vertices_range(_g))
        {
            if (out_degree(v, _g) > 0)
            {
                size_t s = _node_index[v];
                if (s >= hist_out.size())
                    hist_out.resize(s + 1);
                hist_out[s][_b[v]]++;
            }

            if (in_degreeS()(v, _g) > 0)
            {
                size_t t = _node_index[v];
                if (t >= hist_in.size())
                    hist_in.resize(t + 1);
                hist_in[t][_b[v]]++;
            }
        }

        hist_in.resize(num_vertices(g));
        hist_out.resize(num_vertices(g));

        set<size_t> rs;
        for (auto i : vertices_range(g))
        {
            rs.clear();
            for (auto iter = hist_out[i].begin(); iter != hist_out[i].end(); ++iter)
                rs.insert(iter->first);
            for (auto iter = hist_in[i].begin(); iter != hist_in[i].end(); ++iter)
                rs.insert(iter->first);
            // if (rs.empty())
            //     throw GraphException("Cannot have empty overlapping block membership!");
            for (size_t r : rs)
            {
                bv[i].push_back(r);

                auto iter_in = hist_in[i].find(r);
                if (iter_in != hist_in[i].end())
                    bc_in[i].push_back(iter_in->second);
                else
                    bc_in[i].push_back(0);

                auto iter_out = hist_out[i].find(r);
                if (iter_out != hist_out[i].end())
                    bc_out[i].push_back(iter_out->second);
                else
                    bc_out[i].push_back(0);

                bc_total[i].push_back(bc_in[i].back() +
                                      bc_out[i].back());
            }
        }
    }

    template <class Graph, class VVProp, class VProp>
    void get_overlap_split(Graph& g, VVProp bv, VProp b) const
    {
        gt_hash_map<vector<int>, size_t> bvset;

        for (auto v : vertices_range(g))
        {
            auto r = bv[v];
            auto iter = bvset.find(r);
            if (iter == bvset.end())
                iter = bvset.insert(make_pair(r, bvset.size())).first;
            b[v] = iter->second;
        }
    }


    void init_mcmc(double c, double dl)
    {
        if (!std::isinf(c))
        {
            if (_egroups.empty())
                _egroups.init(_b, _eweight, _g, _bg);
        }
        else
        {
            _egroups.clear();
        }

        if (dl)
            enable_partition_stats();
        else
            disable_partition_stats();
    }


//private:
    typedef typename
        std::conditional<is_directed::apply<g_t>::type::value,
                         GraphInterface::multigraph_t,
                         UndirectedAdaptor<GraphInterface::multigraph_t>>::type
        bg_t;
    bg_t& _bg;

    typename mrs_t::checked_t _c_mrs;

    typedef typename std::conditional<use_hash_t::value,
                                      EHash<g_t, bg_t>,
                                      EMat<g_t, bg_t>>::type
        emat_t;
    emat_t _emat;

    EGroups<g_t, mpl::false_> _egroups;

    overlap_stats_t _overlap_stats;
    std::vector<overlap_partition_stats_t> _partition_stats;
    std::vector<size_t> _bmap;
    std::vector<size_t> _vmap;

    SingleEntrySet<g_t> _m_entries;

    UnityPropertyMap<int,GraphInterface::edge_t> _eweight;
    UnityPropertyMap<int,GraphInterface::vertex_t> _vweight;
};

} // namespace graph_tool

#endif // GRAPH_BLOCKMODEL_OVERLAP_HH