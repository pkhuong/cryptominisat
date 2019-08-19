/******************************************
Copyright (c) 2016, Mate Soos

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
***********************************************/

#ifndef __SEARCHER_H__
#define __SEARCHER_H__

#include <array>

#include "propengine.h"
#include "solvertypes.h"
#include "time_mem.h"
#include "hyperengine.h"
#include "MersenneTwister.h"
#include "simplefile.h"
#include "searchstats.h"
#include "gqueuedata.h"

#ifdef CMS_TESTING_ENABLED
#include "gtest/gtest_prod.h"
#endif


namespace CMSat {

class Solver;
class SQLStats;
class VarReplacer;
class EGaussian;
class DistillerLong;

using std::string;
using std::cout;
using std::endl;

struct OTFClause
{
    Lit lits[3];
    unsigned size;
};

struct VariableVariance
{
    double avgDecLevelVarLT = 0;
    double avgTrailLevelVarLT= 0;
    double avgDecLevelVar = 0;
    double avgTrailLevelVar = 0;
};

class Searcher : public HyperEngine
{
    public:
        Searcher(const SolverConf* _conf, Solver* solver, std::atomic<bool>* _must_interrupt_inter);
        virtual ~Searcher();
        ///////////////////////////////
        // Solving
        //
        lbool solve(
            uint64_t max_confls
        );
        void finish_up_solve(lbool status);
        void reduce_db_if_needed();
        bool clean_clauses_if_needed();
        void check_calc_satzilla_features(bool force = false);
        void check_calc_vardist_features(bool force = false);
        void dump_search_loop_stats(double myTime);
        bool must_abort(lbool status);
        uint64_t luby_loop_num = 0;
        MTRand mtrand; ///< random number generator


        vector<lbool>  model;
        vector<Lit>    decisions_reaching_model; // the decisions needed to reach current model
        bool           decisions_reaching_model_valid = false;
        vector<Lit>   conflict;     ///<If problem is unsatisfiable (possibly under assumptions), this vector represent the final conflict clause expressed in the assumptions.
        template<bool update_bogoprops>
        PropBy propagate();

        ///////////////////////////////
        // Stats
        //Restart print status
        uint64_t lastRestartPrint = 0;
        uint64_t lastRestartPrintHeader = 0;
        void     print_restart_stat();
        void     print_iteration_solving_stats();
        void     print_restart_header();
        void     print_restart_stat_line() const;
        void     print_restart_stats_base() const;
        void     print_clause_stats() const;
        uint64_t sumRestarts() const;
        const SearchHist& getHistory() const;

        size_t hyper_bin_res_all(const bool check_for_set_values = true);
        std::pair<size_t, size_t> remove_useless_bins(bool except_marked = false);

        ///Returns 0 if not inside, 1 if TRUE and 2 if FALSE
        lbool var_inside_assumptions(const uint32_t var) const
        {
            #ifdef SLOW_DEBUG
            assert(var < nVars());
            #endif
            return varData[var].assumption;
        }
        lbool lit_inside_assumptions(const Lit lit) const
        {
            #ifdef SLOW_DEBUG
            assert(lit.var() < nVars());
            #endif
            if (varData[lit.var()].assumption == l_Undef) {
                return l_Undef;
            } else {
                lbool val = varData[lit.var()].assumption;
                return val ^ lit.sign();
            }
        }
        template<bool do_insert_var_order = true, bool update_bogoprops = false>
        void cancelUntil(uint32_t level); ///<Backtrack until a certain level.

        SQLStats* sqlStats = NULL;
        void consolidate_watches(const bool full);

        //Gauss
        #ifdef USE_GAUSS
        void clear_gauss_matrices();
        enum class gauss_ret {g_cont, g_nothing, g_false};
        gauss_ret gauss_jordan_elim();
        vector<EGaussian*> gmatrices;
        vector<GaussQData> gqueuedata;

        uint32_t sum_gauss_called = 0;
        uint32_t sum_gauss_confl = 0;
        uint32_t sum_gauss_prop = 0;
        uint32_t sum_gauss_unit_truths = 0;
        uint32_t sum_gauss_entered_mtx = 0;
        uint32_t get_sum_gauss_called() const;
        uint32_t get_sum_gauss_confl() const;
        uint32_t get_sum_gauss_prop() const;
        uint32_t get_sum_gauss_unit_truths() const;
        #endif

        double get_cla_inc() const
        {
            return cla_inc;
        }

        //assumptions
        void check_assumptions_sanity();
        void unfill_assumptions_set();
        bool check_order_heap_sanity() const;


        template<bool update_bogoprops>
        void bump_cl_act(Clause* cl);
        void simple_create_learnt_clause(
            PropBy confl,
            vector<Lit>& out_learnt,
            bool True_confl
        );

        #ifdef STATS_NEEDED
        void dump_restart_sql(rst_dat_type type);
        uint64_t last_dumped_conflict_rst_data_for_var = std::numeric_limits<uint64_t>::max();
        #endif

        bool pick_polarity(const uint32_t var);
        void bump_var_importance(uint32_t var);

    protected:
        Solver* solver;
        lbool search();

        ///////////////
        // Variables
        ///////////////
        void new_var(const bool bva, const uint32_t orig_outer) override;
        void new_vars(const size_t n) override;
        void save_on_var_memory();
        void updateVars(
            const vector<uint32_t>& outerToInter
            , const vector<uint32_t>& interToOuter
        );


        ///////////////
        // Reading and writing simplified CNF file
        ///////////////
        void save_state(SimpleOutFile& f, const lbool status) const;
        void load_state(SimpleInFile& f, const lbool status);
        void write_long_cls(
            const vector<ClOffset>& clauses
            , SimpleOutFile& f
            , const bool red
        ) const;
        void read_long_cls(
            SimpleInFile& f
            , const bool red
        );
        uint64_t read_binary_cls(
            SimpleInFile& f
            , bool red
        );
        void write_binary_cls(
            SimpleOutFile& f
            , bool red
        ) const;

        //Misc
        void add_in_partial_solving_stats();


        ///////////////
        // Assumptions
        ///////////////
        struct AssumptionPair {
            AssumptionPair(const Lit _outer, const Lit _outside):
                lit_outer(_outer)
                , lit_orig_outside(_outside)
            {
            }

            Lit lit_outer;
            Lit lit_orig_outside; //not outer, but outside(!)

            bool operator==(const AssumptionPair& other) const {
                return other.lit_outer == lit_outer &&
                other.lit_orig_outside == lit_orig_outside;
            }

            bool operator<(const AssumptionPair& other) const
            {
                //Yes, we need reverse in terms of inverseness
                return ~lit_outer < ~other.lit_outer;
            }
        };
        void fill_assumptions_set();
        void update_assump_conflict_to_orig_outside(vector<Lit>& out_conflict);

        //Note that this array can have the same internal variable more than
        //once, in case one has been replaced with the other. So if var 1 =  var 2
        //and var 1 was set to TRUE and var 2 to be FALSE, then we'll have var 1
        //insided this array twice, once it needs to be set to TRUE and once FALSE
        vector<AssumptionPair> assumptions;

        /////////////////////
        // Learning
        /////////////////////
        vector<Lit> learnt_clause;
        vector<Lit> decision_clause;
        template<bool update_bogoprops>
        Clause* analyze_conflict(
            PropBy confl //The conflict that we are investigating
            , uint32_t& out_btlevel  //backtrack level
            , uint32_t &glue         //glue of the learnt clause
            , uint32_t &old_glue     //glue of the unminimised learnt clause
        );
        bool  handle_conflict(PropBy confl);// Handles the conflict clause
        void  update_history_stats(size_t backtrack_level, uint32_t glue);
        template<bool update_bogoprops>
        void  attach_and_enqueue_learnt_clause(Clause* cl, bool enq = true);
        void  print_learning_debug_info() const;
        void  print_learnt_clause() const;
        template<bool update_bogoprops>
        void  add_otf_subsume_long_clauses();
        template<bool update_bogoprops>
        void  add_otf_subsume_implicit_clause();
        Clause* handle_last_confl_otf_subsumption(
            Clause* cl
            , const uint32_t glue
            , const uint32_t old_glue
            , const uint32_t old_decision_level
            , const bool decision_cl
        );
        void update_clause_glue_from_analysis(Clause* cl);
        template<bool update_bogoprops>
        void minimize_learnt_clause();
        void watch_based_learnt_minim();
        void minimize_using_permdiff();
        void print_fully_minimized_learnt_clause() const;
        size_t find_backtrack_level_of_learnt();
        Clause* otf_subsume_last_resolved_clause(Clause* last_resolved_long_cl);
        void print_debug_resolution_data(const PropBy confl);
        template<bool update_bogoprops>
        Clause* create_learnt_clause(PropBy confl);
        int pathC;
        uint64_t more_red_minim_limit_binary_actual;
        void create_decision_clause();
        #if defined(STATS_NEEDED) || defined(FINAL_PREDICTOR)
        AtecedentData<uint16_t> antec_data;
        #endif

        /////////////////////
        // Branching
        /////////////////////
        double var_inc_vsids;
        double var_decay_vsids;
        void insert_var_order(const uint32_t x, branch type);
        void insert_var_order(const uint32_t x);
        vector<uint32_t> implied_by_learnts; //for glue-based extra var activity bumping
        void update_branch_params();
        template<bool update_bogoprops>
        lbool new_decision();
        Lit pickBranchLit();
        uint32_t pick_random_var();
        uint32_t pick_var_vsids_maple();
        uint32_t pick_var_vmtf();
        double maple_step_size;
        void vsids_decay_var_act();
        template<bool update_bogoprops>
        void vsids_bump_var_act(uint32_t v, double mult = 1.0);
        void update_var_decay_vsids();
        double backup_random_var_freq = -1; ///<if restart has full random var branch, we save old value here
        void check_var_in_branch_strategy(uint32_t var) const;
        void set_branch_strategy(const uint32_t iteration_num);
        void build_branch_strategy_setup(branch which);
        void clear_branch_strategy_setup(branch which);
        void rebuild_all_branch_strategy_setups();
        void clear_all_branch_strategy_setups();
        uint32_t branch_strategy_num = 0;
        uint32_t next_change_branch_strategy = 0;
        void adjust_branch_strategy();

        /////////////////////
        // Search Stats
        /////////////////////
        const SearchStats& get_stats() const;
        size_t mem_used() const;
        void reset_temp_cl_num();
        void  resetStats(); //For connection with Solver
        SearchHist hist;
        double   startTime; ///<When solve() was started
        SearchStats stats;

        /////////////////////
        // Clause database reduction
        /////////////////////
        uint64_t next_lev1_reduce;
        uint64_t next_lev2_reduce;
        uint64_t next_lev3_reduce;

        ///////////////
        // Restart parameters
        ///////////////
        struct SearchParams
        {
            SearchParams()
            {
                clear();
            }

            void clear()
            {
                needToStopSearch = false;
                conflictsDoneThisRestart = 0;
            }

            bool needToStopSearch;
            uint64_t conflictsDoneThisRestart;
            uint64_t max_confl_to_do;
            Restart rest_type = Restart::never;
        };
        SearchParams params;
        int64_t max_confl_phase;
        int64_t max_confl_this_phase;
        void  check_need_restart();
        uint64_t max_confl_per_search_solve_call;
        uint32_t num_search_called = 0;
        double luby(double y, int x);
        void adjust_restart_strategy();
        void setup_restart_strategy();

        //////////////
        // Debug
        //////////////
        void print_solution_varreplace_status() const;

        //////////////
        // Conflict minimisation
        bool litRedundant(Lit p, uint32_t abstract_levels);
        void recursiveConfClauseMin();
        void normalClMinim();
        MyStack<Lit> analyze_stack;
        uint32_t abstractLevel(const uint32_t x) const;

        //OTF subsumption during learning
        vector<ClOffset> otf_subsuming_long_cls;
        vector<OTFClause> otf_subsuming_short_cls;
        void check_otf_subsume(const ClOffset offset, Clause& cl);
        void create_otf_subsuming_implicit_clause(const Clause& cl);
        void create_otf_subsuming_long_clause(Clause& cl, ClOffset offset);
        template<bool update_bogoprops>
        Clause* add_literals_from_confl_to_learnt(const PropBy confl, const Lit p);
        void debug_print_resolving_clause(const PropBy confl) const;
        template<bool update_bogoprops>
        void add_lit_to_learnt(Lit lit);
        void analyze_final_confl_with_assumptions(const Lit p, vector<Lit>& out_conflict);
        size_t tmp_learnt_clause_size;
        cl_abst_type tmp_learnt_clause_abst;
        bool subset(const vector<Lit>& A, const Clause& B); //Used for on-the-fly subsumption. Does A subsume B? Uses 'seen' to do its work

        ////////////
        // Transitive on-the-fly self-subsuming resolution
        void   minimise_redundant_more_more(vector<Lit>& cl);
        void   binary_based_morem_minim(vector<Lit>& cl);


        friend class Gaussian;
        friend class DistillerLong;
        #ifdef CMS_TESTING_ENABLED
        FRIEND_TEST(SearcherTest, pickpolar_rnd);
        FRIEND_TEST(SearcherTest, pickpolar_pos);
        FRIEND_TEST(SearcherTest, pickpolar_neg);
        FRIEND_TEST(SearcherTest, pickpolar_auto);
        FRIEND_TEST(SearcherTest, pickpolar_auto_not_changed_by_simp);
        #endif

        //Clause activites
        double cla_inc;
        template<bool update_bogoprops> void decayClauseAct();

        //SQL
        void dump_search_sql(const double myTime);
        void set_clause_data(
            Clause* cl
            , const uint32_t glue
            , const uint32_t old_glue
            , const uint32_t old_decision_level);
        #ifdef STATS_NEEDED
        PropStats lastSQLPropStats;
        SearchStats lastSQLGlobalStats;
        void dump_sql_clause_data(
            const uint32_t glue
            , const uint32_t old_glue
            , const uint32_t old_decision_level
            , const uint64_t clid
            , const bool decision_cl
            , const bool ternary_resol_cl
        );
        int dump_this_many_cldata_in_stream = 0;
        void sql_dump_last_in_solver();
        vector<uint32_t> level_used_for_cl;
        vector<uint32_t> vars_used_for_cl;
        vector<unsigned char> level_used_for_cl_arr;
        void dump_var_for_learnt_cl(uint32_t v, uint64_t clid);
        #endif


        //Other
        void print_solution_type(const lbool status) const;
        uint64_t next_distill = 0;

        //Last time we clean()-ed the clauses, the number of zero-depth assigns was this many
        size_t   lastCleanZeroDepthAssigns;
};

inline uint32_t Searcher::abstractLevel(const uint32_t x) const
{
    return ((uint32_t)1) << (varData[x].level & 31);
}

inline const SearchStats& Searcher::get_stats() const
{
    return stats;
}

inline const SearchHist& Searcher::getHistory() const
{
    return hist;
}

inline void Searcher::add_in_partial_solving_stats()
{
    stats.cpu_time = cpuTime() - startTime;
}

inline void Searcher::insert_var_order(const uint32_t x)
{
    insert_var_order(x, branch_strategy);
}

inline void Searcher::insert_var_order(const uint32_t x, branch type)
{
    #ifdef SLOW_DEUG
    assert(varData[x].removed == Removed::none
        && "All variables should be decision vars unless removed");
    #endif

    switch(type) {
        case branch::vsids:
            if (!order_heap_vsids.inHeap(x)) {
                order_heap_vsids.insert(x);
            }
            break;
        case branch::maple:
            if (!order_heap_maple.inHeap(x)) {
                order_heap_maple.insert(x);
            }
            break;
        case branch::vmtf:
            // For VMTF we need to update the 'queue.unassigned' pointer in case this
            // variables sits after the variable to which 'queue.unassigned' currently
            // points.  See our SAT'15 paper for more details on this aspect.
            //
            if ( vmtf_queue.vmtf_bumped < vmtf_btab[x]) vmtf_update_queue_unassigned (x);
            break;

        case branch::rnd:
            if (order_heap_rnd_inside[x] == 0) {
                order_heap_rnd_inside[x] = 1;
                order_heap_rnd.push_back(x);
            }
            break;
    }
}

template<bool update_bogoprops>
inline void Searcher::bump_cl_act(Clause* cl)
{
    if (update_bogoprops)
        return;

    assert(!cl->getRemoved());

    double new_val = cla_inc + (double)cl->stats.activity;
    cl->stats.activity = (float)new_val;
    if (max_cl_act < new_val) {
        max_cl_act = new_val;
    }


    if (cl->stats.activity > 1e20F ) {
        // Rescale. For STATS_NEEDED we rescale ALL
        #if !defined(STATS_NEEDED) && !defined (FINAL_PREDICTOR)
        for(ClOffset offs: longRedCls[2]) {
            cl_alloc.ptr(offs)->stats.activity *= static_cast<float>(1e-20);
        }
        #else
        for(auto& lrcs: longRedCls) {
            for(ClOffset offs: lrcs) {
                cl_alloc.ptr(offs)->stats.activity *= static_cast<float>(1e-20);
            }
        }
        #endif
        cla_inc *= 1e-20;
        max_cl_act *= 1e-20;
        assert(cla_inc != 0);
    }
}

template<bool update_bogoprops>
inline void Searcher::decayClauseAct()
{
    if (update_bogoprops)
        return;

    cla_inc *= (1 / conf.clause_decay);
}

inline bool Searcher::pick_polarity(const uint32_t var)
{
    switch(conf.polarity_mode) {
        case PolarityMode::polarmode_neg:
            return false;

        case PolarityMode::polarmode_pos:
            return true;

        case PolarityMode::polarmode_rnd:
            return mtrand.randInt(1);

        case PolarityMode::polarmode_automatic:
            return varData[var].polarity;

        #ifdef WEIGHTED_SAMPLING
        case PolarityMode::polarmode_weighted: {
            double rnd = mtrand.randDblExc();
            return rnd < varData[var].weight;
        }
        #endif

        default:
            assert(false);
    }

    return true;
}

template<bool update_bogoprops>
inline void Searcher::vsids_bump_var_act(uint32_t var, double mult)
{
    if (update_bogoprops) {
        return;
    }

    var_act_vsids[var] += var_inc_vsids * mult;
    if (max_vsids_act < var_act_vsids[var]) {
        max_vsids_act = var_act_vsids[var];
    }

    #ifdef SLOW_DEBUG
    bool rescaled = false;
    #endif
    if (var_act_vsids[var] > 1e100) {
        // Rescale:

        for (double& act : var_act_vsids) {
            act *= 1e-100;
        }
        max_vsids_act *= 1e-100;

        #ifdef SLOW_DEBUG
        rescaled = true;
        #endif

        //Reset var_inc
        var_inc_vsids *= 1e-100;
    }

    // Update order_heap with respect to new activity:
    if (order_heap_vsids.inHeap(var)) {
        order_heap_vsids.decrease(var);
    }

    #ifdef SLOW_DEBUG
    if (rescaled) {
        assert(order_heap_vsids.heap_property());
    }
    #endif
}

#ifdef USE_GAUSS
inline uint32_t Searcher::get_sum_gauss_unit_truths() const
{
    return sum_gauss_unit_truths;
}

inline uint32_t Searcher::get_sum_gauss_called() const
{
    return sum_gauss_called;
}

inline uint32_t Searcher::get_sum_gauss_confl() const
{
    return sum_gauss_confl;
}

inline uint32_t Searcher::get_sum_gauss_prop() const
{
    return sum_gauss_prop;
}
#endif



} //end namespace

#endif //__SEARCHER_H__
