#include "ipcountry.h"

#include <base/system.h>
#include <engine/storage.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

bool CIpCountryDb::ParseIPv4(const char *pStr, unsigned *pOut)
{
	if(!pStr || !pOut)
		return false;
	unsigned a = 0, b = 0, c = 0, d = 0;
	char Tail = 0;
	if(sscanf(pStr, "%u.%u.%u.%u%c", &a, &b, &c, &d, &Tail) < 4)
		return false;
	if(a > 255 || b > 255 || c > 255 || d > 255)
		return false;
	*pOut = (a << 24) | (b << 16) | (c << 8) | d;
	return true;
}

bool CIpCountryDb::ParseCidr(const char *pStr, unsigned *pStart, unsigned *pEnd)
{
	char aBuf[64];
	str_copy(aBuf, pStr, sizeof(aBuf));
	char *pSlash = 0;
	for(char *p = aBuf; *p; p++)
	{
		if(*p == '/')
		{
			pSlash = p;
			break;
		}
	}
	if(!pSlash)
		return false;
	*pSlash = 0;
	unsigned Prefix = (unsigned)atoi(pSlash + 1);
	if(Prefix > 32 || !ParseIPv4(aBuf, pStart))
		return false;
	unsigned Mask = Prefix == 0 ? 0 : (0xffffffffu << (32 - Prefix));
	*pStart &= Mask;
	*pEnd = *pStart | ~Mask;
	return true;
}

int CIpCountryDb::SplitCsv(char *pLine, char **apCols, int MaxCols)
{
	int n = 0;
	char *p = pLine;
	while(n < MaxCols)
	{
		if(*p == '"')
		{
			p++;
			apCols[n++] = p;
			while(*p && *p != '"')
				p++;
			if(*p == '"')
				*p++ = 0;
			if(*p == ',')
				p++;
			continue;
		}
		apCols[n++] = p;
		while(*p && *p != ',')
			p++;
		if(!*p)
			break;
		*p++ = 0;
	}
	return n;
}

bool CIpCountryDb::LookupU32(unsigned Ip, char *pCountry, int CountrySize) const
{
	if(m_vRanges.empty() || !pCountry || CountrySize < 2)
		return false;
	int Lo = 0, Hi = (int)m_vRanges.size() - 1;
	while(Lo <= Hi)
	{
		int Mid = (Lo + Hi) / 2;
		const SRange &R = m_vRanges[Mid];
		if(Ip < R.m_Start)
			Hi = Mid - 1;
		else if(Ip > R.m_End)
			Lo = Mid + 1;
		else
		{
			str_copy(pCountry, R.m_aCountry, CountrySize);
			return true;
		}
	}
	return false;
}

bool CIpCountryDb::Load(IStorage *pStorage, const char *pFilename)
{
	Clear();
	if(!pStorage || !pFilename || !pFilename[0])
		return false;

	IOHANDLE File = pStorage->OpenFile(pFilename, IOFLAG_READ, IStorage::TYPE_ALL);
	if(!File)
	{
		// match localization: fall back to ./data/... when storage has no data dir
		char aAlt[512];
		if(str_comp_nocase_num(pFilename, "./data/", 7) != 0 &&
			str_comp_nocase_num(pFilename, "data/", 5) != 0)
		{
			str_format(aAlt, sizeof(aAlt), "./data/%s", pFilename);
			File = pStorage->OpenFile(aAlt, IOFLAG_READ, IStorage::TYPE_ALL);
		}
	}
	if(!File)
	{
		dbg_msg("ipcountry", "can't open '%s'", pFilename);
		return false;
	}

	const int FileSize = (int)io_length(File);
	if(FileSize <= 0 || FileSize > 512 * 1024 * 1024)
	{
		io_close(File);
		dbg_msg("ipcountry", "bad size for '%s'", pFilename);
		return false;
	}

	char *pData = (char *)malloc(FileSize + 1);
	if(!pData)
	{
		io_close(File);
		return false;
	}
	io_read(File, pData, FileSize);
	io_close(File);
	pData[FileSize] = 0;

	char *pLine = pData;
	char *pNext = 0;
	int StartCol = -1, EndCol = -1, NetCol = -1, CountryCol = -1;
	bool HeaderDone = false;

	m_vRanges.reserve(1 << 20);

	for(; pLine; pLine = pNext)
	{
		pNext = 0;
		for(char *p = pLine; *p; p++)
		{
			if(*p == '\n')
			{
				*p = 0;
				pNext = p + 1;
				break;
			}
			if(*p == '\r')
				*p = 0;
		}
		if(!pLine[0])
			continue;

		char aLine[512];
		str_copy(aLine, pLine, sizeof(aLine));
		char *apCols[16];
		int NumCols = SplitCsv(aLine, apCols, 16);
		if(NumCols < 2)
			continue;

		if(!HeaderDone)
		{
			HeaderDone = true;
			for(int i = 0; i < NumCols; i++)
			{
				if(!str_comp_nocase(apCols[i], "start_ip"))
					StartCol = i;
				else if(!str_comp_nocase(apCols[i], "end_ip"))
					EndCol = i;
				else if(!str_comp_nocase(apCols[i], "network"))
					NetCol = i;
				else if(!str_comp_nocase(apCols[i], "country") ||
					!str_comp_nocase(apCols[i], "country_code"))
					CountryCol = i;
			}
			if(CountryCol < 0 || (StartCol < 0 && NetCol < 0))
			{
				free(pData);
				dbg_msg("ipcountry", "unrecognized header in '%s'", pFilename);
				return false;
			}
			continue;
		}

		if(CountryCol >= NumCols)
			continue;
		const char *pCountry = apCols[CountryCol];
		if(!pCountry || str_length(pCountry) < 2)
			continue;

		unsigned Start = 0, End = 0;
		bool Ok = false;
		if(NetCol >= 0 && NetCol < NumCols)
		{
			if(str_find(apCols[NetCol], ":"))
				continue; // ponytail: IPv6 skipped — flag language fallback
			Ok = ParseCidr(apCols[NetCol], &Start, &End);
		}
		else if(StartCol >= 0 && EndCol >= 0 && StartCol < NumCols && EndCol < NumCols)
		{
			if(str_find(apCols[StartCol], ":") || str_find(apCols[EndCol], ":"))
				continue;
			Ok = ParseIPv4(apCols[StartCol], &Start) && ParseIPv4(apCols[EndCol], &End);
		}
		if(!Ok || Start > End)
			continue;

		SRange R;
		R.m_Start = Start;
		R.m_End = End;
		R.m_aCountry[0] = str_uppercase(pCountry[0]);
		R.m_aCountry[1] = str_uppercase(pCountry[1]);
		R.m_aCountry[2] = 0;
		m_vRanges.push_back(R);
	}

	free(pData);

	// ensure sorted by start for binary search (IPinfo files usually are)
	for(size_t i = 1; i < m_vRanges.size(); i++)
	{
		if(m_vRanges[i].m_Start < m_vRanges[i - 1].m_Start)
		{
			dbg_msg("ipcountry", "ranges not sorted, sorting %d entries", (int)m_vRanges.size());
			// insertion-sort would be bad; use qsort via C
			struct Helper
			{
				static int Cmp(const void *a, const void *b)
				{
					const SRange *A = (const SRange *)a;
					const SRange *B = (const SRange *)b;
					if(A->m_Start < B->m_Start)
						return -1;
					if(A->m_Start > B->m_Start)
						return 1;
					return 0;
				}
			};
			qsort(m_vRanges.data(), m_vRanges.size(), sizeof(SRange), Helper::Cmp);
			break;
		}
	}

	dbg_msg("ipcountry", "loaded %d IPv4 ranges from '%s'", (int)m_vRanges.size(), pFilename);
	return !m_vRanges.empty();
}

bool CIpCountryDb::Lookup(const char *pIp, char *pCountry, int CountrySize) const
{
	unsigned Ip = 0;
	if(!ParseIPv4(pIp, &Ip))
		return false;
	return LookupU32(Ip, pCountry, CountrySize);
}

void CIpCountryDb::SelfCheck()
{
	CIpCountryDb Db;
	SRange A = {0x01000100u, 0x010003ffu, {'C', 'N', 0}};
	SRange B = {0x01000400u, 0x010007ffu, {'A', 'U', 0}};
	Db.m_vRanges.push_back(A);
	Db.m_vRanges.push_back(B);
	char aCc[4];
	dbg_assert(Db.Lookup("1.0.1.50", aCc, sizeof(aCc)) && !str_comp(aCc, "CN"), "ipcountry cn");
	dbg_assert(Db.Lookup("1.0.5.1", aCc, sizeof(aCc)) && !str_comp(aCc, "AU"), "ipcountry au");
	dbg_assert(!Db.Lookup("1.0.8.1", aCc, sizeof(aCc)), "ipcountry miss");
}
