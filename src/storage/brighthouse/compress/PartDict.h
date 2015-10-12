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

#ifndef __COMPRESSION_PARTDICT_H
#define __COMPRESSION_PARTDICT_H

#include <memory.h>
#include "DataFilt.h"
#include "RangeCoder.h"
#include "defs.h"
#include "common/bhassert.h"

// Partial Dictionary model of distribution of _uint64 values
// Only the most frequent values are stored in the dictionary, so as to optimize
// the length of compressed data plus dictionary.
template<class T> class PartDict : public DataFilt<T>
{
public:
#ifndef SOLARIS
	static const uint MAXLEN = 65536;
	//static const uint MAXTOTAL = ArithCoder::MAX_TOTAL;
	static const uint MAXTOTAL = RangeCoder::MAX_TOTAL;

	static const uint MINOCCUR = 4;							// how many times a value must occur to be frequent value
	static const uint MAXFREQ = (MAXTOTAL+20)/MINOCCUR;		// max no. of frequent values = max size of dictionary; must be smaller than USHRT_MAX
#endif
private:
	struct HashTab
	{
		static const uint nbuck;
		static const uint topbit = 15;
		static const uint mask;
		struct AKey {
			T key;
			uint count;			// no. of occurences of the 'key'; 0 for empty bucket
			uint low;			// low=(uint)-1  when the key is outside dictionary (ESC)
			//uint low, high;		// coding range of the key; high=0 when the key is outside the dictionary (symbol ESC)
			int next;			// next element in the bucket or -1
		};

		AKey *keys; //, *stop;
		int* buckets;			// indices into 'keys'; -1 means empty bucket
		int nkeys;				// no. of elements (keys) currently in the table 'keys'
		int nusebuck;			// no. of currently used buckets (just for statistics)

		// hash function
		uint fun(T key);
		void insert(T key);
		int find(T key);		// returns index of 'key' in 'keys' or -1

		void Clear();
		HashTab();
		~HashTab();
	};

	// structures for compression
	HashTab hash;
	typename HashTab::AKey** freqkey;	// pointers to frequent values (occuring more than once) in array 'hash.keys'; sorted in decending order
	uint nfreq;							// current size of 'freqkey'; = size of dictionary
	static int compare(const void* p1, const void* p2);		// for sorting of 'freqkey' array

	// for decompression
	struct ValRange {
		T val;
		uint low, count;
	};
	ValRange *freqval;
	ushort* cnt2val;			// cnt2val[c] is an index (into freqval) of frequent value for count 'c'

	// for merging data during decompression
	T* decoded;
	bool* isesc;
	uint lenall, lenrest;

	uint esc_count, esc_usecnt, esc_low, esc_high;

	void Clear();

	//uint GetTotal()   { BHASSERT(compress || decompress, "should be 'compress || decompress'"); return esc_high; }
	bool GetRange(T val, uint& low, uint& count);			// returns true when ESC (then low and count are of ESC)
	bool GetVal(uint c, T& val, uint& low, uint& count);	// returns true when ESC (then low and count are of ESC)

	void Create(DataSet<T>* dataset);

	// prediction of compressed data size, in BITS (rare symbols are assumed to be encoded uniformly)
	uint Predict(DataSet<T>* ds);

	void Save(RangeCoder* coder, T maxval);			// saves frequent values and their ranges
	void Load(RangeCoder* coder, T maxval);

public:
	PartDict();
	virtual ~PartDict();
	virtual char const* GetName()		{ return "dict"; }

	virtual bool Encode(RangeCoder* coder, DataSet<T>* dataset);
	virtual void Decode(RangeCoder* coder, DataSet<T>* dataset);
	virtual void Merge(DataSet<T>* dataset);

	//void Encode(RangeCoder* dest, _uint64* data, uint& len);
	//void Decode(RangeCoder* src, uint& len);
	//void Merge(_uint64* data);		// 'data' must be an array of 'lenrest' elements; upon exit contains 'lenall' elem.
};


template<class T> inline void PartDict<T>::HashTab::insert(T key)
{
	//AKey* p = keys + fun(key);
	//while(p->count && (p->key != key))
	//	if(++p == stop) p = keys;
	//if(p->count++) return NULL;
	//p->key = key;
	//return p;

	uint b = fun(key);
	int k = buckets[b];
	if(k < 0) nusebuck++;
	else while((k >= 0) && (keys[k].key != key))
		k = keys[k].next;

	if(k < 0) {
#ifdef SOLARIS
        BHASSERT(nkeys < 65536, "should be 'nkeys < PartDict::MAXLEN'");
#else
        BHASSERT(nkeys < PartDict::MAXLEN, "should be 'nkeys < PartDict::MAXLEN'");
#endif
		AKey& ak = keys[nkeys];
		ak.key = key;
		ak.count = 1;
		ak.low = (uint)-1;
		//ak.high = 0;
		ak.next = buckets[b];			// TODO: time - insert new keys at the END of the list
		buckets[b] = nkeys++;
	}
	else keys[k].count++;
}

template<class T> inline int PartDict<T>::HashTab::find(T key)
{
	//AKey* p = keys + fun(key);
	//while(p->count && (p->key != key))
	//	if(++p == stop) p = keys;
	//return p->count ? p : NULL;

	uint b = fun(key);
	int k = buckets[b];
	while((k >= 0) && (keys[k].key != key))
		k = keys[k].next;
	return k;
}


template<> inline unsigned int PartDict<unsigned long long>::HashTab::fun(unsigned long long key) {
	uint x = ((uint)key ^ *((uint*)&key+1));
	BHASSERT(topbit < sizeof(uint)*8, "should be 'topbit < sizeof(uint)*8'");
	return (x&mask)^(x>>topbit);
}
template<> inline unsigned int PartDict<unsigned int>::HashTab::fun(unsigned int key) {
	BHASSERT(topbit < sizeof(uint)*8, "should be 'topbit < sizeof(uint)*8'");
	return (key&mask)^(key>>topbit);
}
template<> inline unsigned int PartDict<unsigned short>::HashTab::fun(unsigned short key) {
	BHASSERT(key <= mask, "should be 'key <= mask'");
	return key;
}
template<> inline unsigned int PartDict<unsigned char>::HashTab::fun(unsigned char key) {
	BHASSERT(key <= mask, "should be 'key <= mask'");
	return key;
}


template<class T> inline bool PartDict<T>::GetRange(T val, uint& low, uint& count)
{
	//BHASSERT(compress, "'compress' should be true");
	int k = hash.find(val);
	//HashTab::AKey* p = hash.find(val);
	// OK under assumption that PartDict is built on the whole data to be compressed
	BHASSERT(k >= 0, "should be 'k >= 0'");
	//high = hash.keys[k].high;
	low = hash.keys[k].low;
	if(low == (uint)-1) {			// ESCape
		low = esc_low;
		count = esc_usecnt;
		//high = esc_high;
		return true;
	}
	count = hash.keys[k].count;
	//low = hash.keys[k].low;
	//BHASSERT(low < high, "should be 'low < high'");
	return false;
}

template<class T> inline bool PartDict<T>::GetVal(uint c, T& val, uint& low, uint& count)
{
	//BHASSERT(decompress, "'decompress' should be true");
	if(c >= esc_low) {		// ESCape
		low = esc_low;
		count = esc_usecnt;
		//high = esc_high;
		return true;
	}
	ValRange& vr = freqval[cnt2val[c]];
	val = vr.val;
	low = vr.low;
	count = vr.count;
	//high = vr.high;
	return false;
}



#endif
