/* Copyright (C)  2005-2009 Infobright Inc.

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

#include "SorterWrapper.h"
#include "MIIterator.h"
#include "PackOrderer.h"

using namespace std;

SorterWrapper::SorterWrapper(MultiIndex &_mind, _int64 _limit) : cur_mit(&_mind)
{
	no_of_rows = _mind.NoTuples();
	limit = _limit;
	s = NULL;
	mi_encoder = NULL;
	buf_size = 0;
	cur_val = NULL;
	input_buf = NULL;
	no_values_encoded = 0;
	rough_sort_by = -1;
}

SorterWrapper::~SorterWrapper()
{
	delete s;
	delete mi_encoder;
	delete [] input_buf;
}

void SorterWrapper::AddSortedColumn(VirtualColumn *col, int sort_order, bool in_output)
{
	input_cols.push_back(SorterColumnDescription(col, sort_order, in_output));
}

void SorterWrapper::InitOrderByVector(vector<int> &order_by)
{
	// Identify columns to order by, exclude unnecessary ones
	for(int i = 0; i < input_cols.size(); i++)
		order_by.push_back(-1);			// -1 means "no column"

	for(int i = 0; i < input_cols.size(); i++) {
		int ord = input_cols[i].sort_order;
		if(ord < 0)
			ord = -ord;
		if(ord != 0) {
			assert(order_by[ord - 1] == -1);
			order_by[ord - 1] = i;
		}
	}
	for(int i = 0; i < input_cols.size(); i++) // check whether all sorting columns are really needed
		if(order_by[i] != -1) {
			assert(i == 0 || order_by[i - 1] != -1);	// assure continuity
			if(input_cols[order_by[i]].col->IsDistinct()) {
				// do not sort by any other column:
				for(int j = i + 1; j < input_cols.size(); j++)
					if(order_by[j] != -1) {
						input_cols[order_by[j]].sort_order = 0;
						order_by[j] = -1;
					}
					break;
			}
		}
}

void SorterWrapper::InitSorter(MultiIndex &mind)
{
	DimensionVector implicit_dims(mind.NoDimensions());	// these dimensions will be implicit
	DimensionVector one_pack_dims(mind.NoDimensions());	// these dimensions contain only one used pack (potentially implicit)

	// Prepare optimization guidelines
	for(int dim = 0; dim < mind.NoDimensions(); dim++) {
		int no_packs = mind.MaxNoPacks(dim);
		if(no_packs == 1) 
			one_pack_dims[dim] = true;		// possibly implicit if only one pack
		if(no_packs > (limit == -1 ? no_of_rows : limit))
			implicit_dims[dim] = true;		// implicit if there is more opened packs than output rows
	}

	// Identify columns to order by
	vector<int> order_by;
	InitOrderByVector(order_by);

	// Determine encodings
	for(int i = 0; i < input_cols.size(); i++) {
		int flags = 0;
		if(input_cols[i].sort_order != 0)
			flags |= ColumnBinEncoder::ENCODER_MONOTONIC;
		if(input_cols[i].sort_order < 0)
			flags |= ColumnBinEncoder::ENCODER_DESCENDING;
		if(input_cols[i].in_output) {
			flags |= ColumnBinEncoder::ENCODER_DECODABLE;
			if(input_cols[i].sort_order == 0)				// decodable, but not comparable (store values)
				flags |= ColumnBinEncoder::ENCODER_NONCOMPARABLE;
		}
		scol.push_back(ColumnBinEncoder(flags));
		scol[i].PrepareEncoder(input_cols[i].col);
		// neither in_output nor sorting keys => disable them, we'll not use it in any way
		if(!input_cols[i].in_output && input_cols[i].sort_order == 0)
			scol[i].Disable();
	}

	// Identify implicitly encoded columns
	for(int dim = 0; dim < mind.NoDimensions(); dim++)
		if(one_pack_dims[dim] && !implicit_dims[dim]) {		// potentially implicit, check sizes
			int col_sizes_for_dim = 0;
			for(int i = 0; i < input_cols.size(); i++) {
				if( input_cols[i].sort_order == 0 && 
					input_cols[i].col->GetDim() == dim &&
					scol[i].IsEnabled()) { 
					// check only non-ordered columns from the proper dimension
					col_sizes_for_dim += scol[i].GetPrimarySize();
				}
			}
			if(col_sizes_for_dim > MultiindexPositionEncoder::DimByteSize(&mind, dim))
				implicit_dims[dim]= true;		// actual data are larger than row number
		}

	DimensionVector implicit_now(mind.NoDimensions());	// these dimensions will be made implicit
	for(int i = 0; i < input_cols.size(); i++) {
		if(input_cols[i].sort_order == 0 && scol[i].IsEnabled()) { 
			DimensionVector local_dims(mind.NoDimensions());
			input_cols[i].col->MarkUsedDims(local_dims);
			if(local_dims.NoDimsUsed() > 0 && implicit_dims.Includes(local_dims)) {		
				// all dims of the column are marked as implicit
				implicit_now.Plus(local_dims);
				scol[i].SetImplicit();
			}
		}
	}
	if(implicit_now.NoDimsUsed() > 0) 		// nontrivial implicit columns present
		mi_encoder = new MultiindexPositionEncoder(&mind, implicit_now);

	// Define data offsets in sort buffers
	int j = 0;
	int total_bytes = 0;
	int key_bytes = 0;
	rough_sort_by = -1;
	while(j < input_cols.size() && order_by[j] != -1) {
		scol[order_by[j]].SetPrimaryOffset(key_bytes);
		key_bytes += scol[order_by[j]].GetPrimarySize();
		total_bytes += scol[order_by[j]].GetPrimarySize();
		j++;
	}
	for(int i = 0; i < scol.size(); i++) {
		if(scol[i].IsEnabled()) {
			if(input_cols[i].sort_order == 0) {
				scol[i].SetPrimaryOffset(total_bytes);
				total_bytes += scol[i].GetPrimarySize();
			}
			else if(scol[i].GetSecondarySize() > 0) {		// secondary place needed for sorted columns?
				scol[i].SetSecondaryOffset(total_bytes);
				total_bytes += scol[i].GetSecondarySize();
			}
			if(rough_sort_by == -1 && scol[i].IsNontrivial() && (input_cols[i].sort_order == -1 || input_cols[i].sort_order == 1))
				rough_sort_by = i;
		}
	}
	if(rough_sort_by > -1 && (input_cols[rough_sort_by].col->GetDim() == -1 ||		// switch off this optimization for harder cases
								scol[rough_sort_by].IsString() ||
								input_cols[rough_sort_by].col->Type().IsFloat() ||
								input_cols[rough_sort_by].col->Type().IsLookup() ||
								input_cols[rough_sort_by].col->GetMultiIndex()->OrigSize(input_cols[rough_sort_by].col->GetDim()) < 65536
		))	
		rough_sort_by = -1;
	if(mi_encoder) {
		mi_encoder->SetPrimaryOffset(total_bytes);
		total_bytes += mi_encoder->GetPrimarySize();
	}

	// Everything prepared - create sorter
	if(total_bytes > 0) {			// the sorter is nontrivial
		s = Sorter3::CreateSorter(no_of_rows, key_bytes, total_bytes, limit);
		input_buf = new unsigned char [total_bytes];
		if(mi_encoder)
			rccontrol.lock(s->conn->GetThreadID()) << s->Name() << " initialized for " << no_of_rows << " rows, " << key_bytes << "+" << total_bytes - key_bytes - mi_encoder->GetPrimarySize() << "+" << mi_encoder->GetPrimarySize() << " bytes each." << unlock;
		else
			rccontrol.lock(s->conn->GetThreadID()) << s->Name() << " initialized for " << no_of_rows << " rows, " << key_bytes << "+" << total_bytes - key_bytes << " bytes each." << unlock;
	}
}

void SorterWrapper::SortRoughly(std::vector<PackOrderer> &po)
{
	if(rough_sort_by > -1) {		// nontrivial rough_sort_by => order based on one dimension
		int dim = input_cols[rough_sort_by].col->GetDim();
		bool asc = (input_cols[rough_sort_by].sort_order > 0);		// ascending sort
		// start with best packs to possibly roughly exclude others
		po[dim].Init(input_cols[rough_sort_by].col, (asc ? PackOrderer::MaxAsc : PackOrderer::MinDesc));
	}
}

bool SorterWrapper::InitPackrow(MIIterator &mit)		// return true if the packrow may be skipped
{
	if(s == NULL)
		return false;		// trivial sorter (constant values)

	// rough sort: exclude packs which are for sure out of scope
	if(rough_sort_by > -1 && limit > -1 && no_values_encoded >= limit) {
		// nontrivial rough_sort_by => numerical case only
		VirtualColumn *vc = input_cols[rough_sort_by].col;
		if(vc->GetNoNulls(mit) == 0) {
			bool asc = (input_cols[rough_sort_by].sort_order > 0);		// ascending sort
			_int64 local_min = (asc ? vc->GetMinInt64(mit) : MINUS_INF_64);
			_int64 local_max = (asc ? PLUS_INF_64 : vc->GetMaxInt64(mit));
			if(scol[rough_sort_by].ImpossibleValues(local_min, local_max))
				return true;		// exclude
		}
	}

	// Not excluded: lock packs
	for(int i = 0; i < scol.size(); i++)
		if(scol[i].IsEnabled() && scol[i].IsNontrivial()) 	// constant num. columns are trivial
				scol[i].LockSourcePacks(mit);

	return false;
}

bool SorterWrapper::PutValues(MIIterator &mit)
{
	if(s == NULL)
		return false;		// trivial sorter (constant values)
	if(mi_encoder)
		mi_encoder->Encode(input_buf, mit);
	bool update_stats = (rough_sort_by > -1 && limit > -1 && no_values_encoded <= limit);		// otherwise statistics are either not used or already prepared
	for(int i = 0; i < scol.size(); i++)
		if(scol[i].IsEnabled() && scol[i].IsNontrivial())	// constant num. columns are trivial
			scol[i].Encode(input_buf, mit, NULL, update_stats);
	no_values_encoded++;
	return s->PutValue(input_buf);
}

bool SorterWrapper::FetchNextRow()
{
	if(s == NULL) {
		no_of_rows--;
		return (no_of_rows >= 0);		// trivial sorter (constant values, cur_val is always NULL)
	}
	cur_val = s->GetNextValue();
	if(cur_val == NULL)
		return false;
	if(mi_encoder)
		mi_encoder->GetValue(cur_val, cur_mit);	// decode the MIIterator (for implicit/virtual values)
	return true;
}
