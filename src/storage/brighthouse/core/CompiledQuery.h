/* Copyright (C)  2005-2008 Infobright Inc.

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License version 2.0 as
published by the Free  Software Foundation.

This program is distributed in the hope that  it will be useful, but
WITHOUT ANY WARRANTY; without even  the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
General Public License version 2.0 for more details.

You should have received a  copy of the GNU General Public License
version 2.0  along with this  program; if not, write to the Free
Software Foundation,  Inc., 59 Temple Place, Suite 330, Boston, MA
02111-1307 USA  */

#ifndef _COMPILEDQUERY2_H_
#define _COMPILEDQUERY2_H_

#include <vector>
#include "common/CommonDefinitions.h"
#include "JustATable.h"
#include "TempTable.h"
#include "CQTerm.h"

//////////////////////////////////////////////////////////////////////////////////
//
//  CompiledQuery - for storing execution plan of a query (sequence of primitive operations) and output data definition
//	version 2.0, will eventually replace the current CompiledQuery.
//
/////////////////////////////////////////////////////////////////////////////////
class MysqlExpression;
class Item_bhfield;
class CompiledQuery
{
public:
	////////////////////// Definition of one step
	enum StepType { TABLE_ALIAS, TMP_TABLE, CREATE_CONDS, AND_F, OR_F, AND_DESC, OR_DESC,
		T_MODE, JOIN_T, LEFT_JOIN_ON, INNER_JOIN_ON, ADD_CONDS, APPLY_CONDS, ADD_COLUMN,
		ADD_ORDER, UNION, RESULT, STEP_ERROR, CREATE_VC };
	
	class CQStep
	{
	public:
		StepType	type;
		TabID		t1;
		TabID		t2;
		TabID		t3;
		AttrID		a1, a2;
		CondID		c1, c2, c3;
		CQTerm		e1, e2, e3;
		Operator	op;						// predicate: O_EQ, O_LESS etc.
		TMParameter tmpar;					// Table Mode Parameter
		JoinType	jt;
		ColOperation cop;
		char*		alias;
		std::vector<MysqlExpression*> mysql_expr;
		std::vector<int> virt_cols;
		std::vector<TabID> tables1;
		std::vector<TabID> tables2;
		_int64			n1, n2;						// additional parameter (e.g. descending order, TOP n, LIMIT n1..n2)

		CQStep(): type(TABLE_ALIAS),t1(NULL_VALUE_32),t2(NULL_VALUE_32),t3(NULL_VALUE_32),a1(NULL_VALUE_32),
			a2(NULL_VALUE_32),c1(NULL_VALUE_32),c2(NULL_VALUE_32),c3(NULL_VALUE_32),e1(),e2(),e3(),op(O_EQ),
			tmpar(TM_DISTINCT),jt(JO_INNER),cop(LISTING),alias(NULL), n1(NULL_VALUE_64), n2(NULL_VALUE_64) 
		{};

		CQStep(const CQStep & );

		~CQStep() { delete [] alias; }

		CQStep & operator=(const CQStep &);
		/*! \brief Swap contents with another instance of CQStep.
		 * \param s - another instance of CQStep.
		 */
		void swap(CQStep& s);
		void Print(Query* query);						// display the step
	private:
		int N(int x) const { if(x == NULL_VALUE_32) return 0; else return x; };	// if x is NullValue returns 0 otherwise x
	};
	////////////////////// Initialization

	CompiledQuery();
	CompiledQuery(CompiledQuery const &);
	CompiledQuery& operator=(CompiledQuery const &);
	~CompiledQuery();

	////////////////////// Add a new step to the execution plan

	void TableAlias(TabID& t_out, const TabID& n, const char *tab_name = NULL, int id = -1);
	void TmpTable(TabID& t_out, const TabID& t1, bool for_subq = false);
	void CreateConds(CondID &c_out, const TabID& t1, CQTerm e1, Operator pr, CQTerm e2, CQTerm e3=CQTerm(), bool is_or_subtree = false, char like_esc = '\\');
	void CreateConds(CondID &c_out, const TabID& t1, const CondID& c1, bool is_or_subtree = false);
	void And(const CondID& c1, const TabID& t, const CondID& c2);
	void Or(const CondID& c1, const TabID& t, const CondID& c2);
	void And(const CondID& c1, const TabID& t, CQTerm e1, Operator pr, CQTerm e2=CQTerm(), CQTerm e3=CQTerm());
	void Or(const CondID& c1, const TabID& t, CQTerm e1, Operator pr, CQTerm e2=CQTerm(), CQTerm e3=CQTerm());
	void SwitchOperator(Operator &op);
	void Mode(const TabID& t1, TMParameter mode, _int64 n1 = 0, _int64 n2 = 0);
	void Join(const TabID& t1, const TabID& t2);
	void LeftJoinOn(const TabID& temp_table, std::vector<TabID>& left_tables, std::vector<TabID>& right_tables, const CondID& cond_id);
	void InnerJoinOn(const TabID& temp_table, std::vector<TabID>& left_tables, std::vector<TabID>& right_tables, const CondID& cond_id);
	void AddConds(const TabID& t1, const CondID& c1, CondType cond_type);
	void ApplyConds(const TabID& t1);
	void AddColumn(AttrID& a_out, const TabID& t1, CQTerm e1, ColOperation op, char const alias[] = 0, bool distinct = false);

	/*! \brief Create compilation step CREATE_VC for mysql expression
	 * \param a_out - id of created virtual column
	 * \param t1 - id of TempTable for which virtual column is created
	 * \param expr - the expression to be contained in the created column
	 * \param src_table - src_table == t1 if expression works on aggregation results, == NULL_VALUE_32 otherwise
	 * \return void
	 */
	void CreateVirtualColumn(AttrID& a_out, const TabID& t1, MysqlExpression* expr, const TabID& src_table = TabID(NULL_VALUE_32));

	/*! \brief Create compilation step CREATE_VC for subquery
	 * \param a_out - id of created virtual column
	 * \param t1 - id of TempTable for which virtual column is created
	 * \param subquery - id of TempTable representing subquery
	 * \param on_result - indicator of which MultiIndex should be passed to Virtual column: one representing source data of t1 (\e false) or
	 * one representing output columns of t1 (\e true), e.g. in case of subquery in HAVING
	 * \return void
	 */
	void CreateVirtualColumn(AttrID& a_out, const TabID& t1, const TabID& subquery, bool on_result);
	void CreateVirtualColumn(AttrID& a_out, const TabID& t1, std::vector<int>& vcs, const AttrID& vc_id);
	void CreateVirtualColumn(int& a_out, const TabID& t1, const TabID& table_alias, const AttrID& col_number);
	void Add_Order(const TabID& t1, const AttrID& a1, int d=0);			// d=1 for descending
	void Add_Order(const TabID& p_t, ptrdiff_t p_c) {
		int a_c(static_cast<int>(p_c)); if(p_c == a_c) this->Add_Order(p_t, AttrID(-abs(a_c)), a_c < 0);
	}
	void Union(TabID &t_out, const TabID& t2, const TabID& t3, int all=0);
	void Result(const TabID& t1);

	////////////////////// Informations

	int NoTabs()		{ return no_tabs; }
	int NoAttrs(const TabID& tab)	{ return no_attrs[-tab.n - 1] ; }
	int NoConds()		{ return no_conds; }
	int NoVirtualColumns(const TabID& tt) { /*assert(no_virt_cols.find(tt) != no_virt_cols.end());*/ return no_virt_cols[tt]; }
	void Print(Query*);			// display the CompiledQuery

	int NoSteps()			{ return (int)steps.size(); }
	CQStep& Step(int i)		{ return steps[i]; }

	bool CountColumnOnly(const TabID& table);

	/*! \brief verify is given table alias is used in the query defined by a temp table
	 * \param tab_id alias of a searched table
	 * \param tmp_table alias of the temp table to be searched through
	 * returns true if tab_id == tmp_table or tab_id is one of the source tables used in tmp_table
	 */
	bool ExistsInTempTable(const TabID& tab_id, const TabID& tmp_table);

	/*! \brief Returns set of all dimensions used by output columns (ADD_COLUMN step) for a given TempTable.
	 * \param table_id - id of given TempTable
	 * \param ta - vector of pointers to JustATables necessary to get access to virtual column inside relevant TempTable
	 * \return set<int>
	 */
	std::set<int> GetUsedDims(const TabID& table_id, std::vector<JustATablePtr>& ta);

	/*! \brief Finds the first TempTable in compilation steps that contains given table as source table
	 * \param tab_id - id of search table
	 * \param tmp_table - id of TempTable to start search from
	 * \param is_group_by - flag set to true if searched table (one that is returned) represents GROUP BY query
	 * \return returns id of TempTable containing tab_id as source table
	 */
	TabID FindSourceOfParameter(const TabID& tab_id, const TabID& tmp_table, bool& is_group_by);

	/*! \brief Finds if TempTable represents query with GroupBy clause
	 * \param tab_id - TempTable to be checked
	 * \return true if it is GroupBy query, false otherwise
	 */
	bool IsGroupByQuery(const TabID& tab_id);

	bool IsResultTable(const TabID& t);
	bool IsOrderedBy(const TabID& t);
	_int64 FindLimit(int table);				// return LIMIT value for table, or -1
	bool FindDistinct(int table);				// return true if there is DISTINCT flag for this query
	bool NoAggregationOrderingAndDistinct(int table);
	void TakeNoVirtColumns(CompiledQuery* q) {no_virt_cols = q->no_virt_cols;}
	std::pair<_int64, _int64> GetGlobalLimit();
	std::vector<int>& GetColumnsPerAlias(int t) { return columns_per_table[t];}
	std::map<int, std::vector<int> >& GetTableIds() {return id2aliases;}
	std::set<int> GetColumnsPerTable(int id);

	int GetNoDims(const TabID& tab_id);
	TabID GetTableOfCond(const CondID& cond_id);

	typedef std::multimap<TabID, CompiledQuery::CQStep> tabstepsmap;
	void BuildTableIDStepsMap();
	void AddGroupByStep(CQStep s)  {steps_group_by_cols.push_back(s);}

private: // NOTE: new objects' IDs (e.g. while declaring new aliases, filters etc.) are reserved and obtained by these functions.
	TabID NextTabID()		{ no_tabs++; no_attrs.push_back(0); return TabID(-no_tabs); }
	AttrID NextVCID(const TabID& tt) { return AttrID(no_virt_cols[tt]++); }
	AttrID NextAttrID(const TabID& tab) { no_attrs[-tab.n - 1]++;
								   return AttrID( -no_attrs[-tab.n - 1] ); }
	CondID NextCondID()	{ return CondID(no_conds++); }	// IDs of tables and attributes start with -1 and decrease;
	
	/*! \brief Checks if given TabID represents TempTable or just some table alias
	 * \param t - TabID of table to be checked
	 * \return true if t is an alias of TempTable, false otherwise
	 */
	bool IsTempTable(const TabID& t);
	int FindRootTempTable(int x);
	// IDs of params and filters start with 0 and increase.
	int no_tabs, no_conds;		// counters: which IDs are not in use?
	std::map<TabID,int> no_virt_cols;

	std::vector<int> no_attrs;	// repository of attr_ids for each table
	std::vector<CompiledQuery::CQStep> steps;			// repository of steps to be executed
	// Extra containers to store specific steps (tmp_tables, group by, tabid2steps map) 
	// so that search can be faster than looking them in the original container (steps).
	std::vector<CompiledQuery::CQStep> steps_tmp_tables;
	std::vector<CompiledQuery::CQStep> steps_group_by_cols;
	std::multimap<TabID, CompiledQuery::CQStep> TabIDSteps;

	std::map<int, std::vector<int> > columns_per_table;
	std::map<int, std::vector<int> > id2aliases;
};

inline void SwitchOperator(Operator &op)
{
	if(op==O_LESS)
		op=O_MORE;
	else if(op==O_MORE)
		op=O_LESS;
	else if(op==O_LESS_EQ)
		op=O_MORE_EQ;
	else if(op==O_MORE_EQ)
		op=O_LESS_EQ;
}

#endif
