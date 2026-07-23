/* IPinfo free country.csv offline lookup (IPv4). */
#ifndef ENGINE_SERVER_IPCOUNTRY_H
#define ENGINE_SERVER_IPCOUNTRY_H

#include <vector>

class IStorage;

class CIpCountryDb
{
	struct SRange
	{
		unsigned m_Start;
		unsigned m_End;
		char m_aCountry[4];
	};
	std::vector<SRange> m_vRanges;

	static bool ParseIPv4(const char *pStr, unsigned *pOut);
	static bool ParseCidr(const char *pStr, unsigned *pStart, unsigned *pEnd);
	static int SplitCsv(char *pLine, char **apCols, int MaxCols);
	bool LookupU32(unsigned Ip, char *pCountry, int CountrySize) const;

public:
	void Clear() { m_vRanges.clear(); }
	bool Empty() const { return m_vRanges.empty(); }
	int NumRanges() const { return (int)m_vRanges.size(); }

	// Loads IPinfo free country.csv (start_ip,end_ip,...,country)
	// or lite csv (network,...,country_code). IPv6 rows skipped.
	bool Load(IStorage *pStorage, const char *pFilename);

	// pIp like "1.2.3.4". Returns false if unknown / not IPv4 / empty db.
	bool Lookup(const char *pIp, char *pCountry, int CountrySize) const;

	// ponytail: synthetic ranges only — fails if binary search regresses
	static void SelfCheck();
};

#endif
